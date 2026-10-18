"""Offline contract tests for misc/cydiasubstrate_macos.m.

Two capability gaps were previously reproduced with a local probe:

  1. MSFindSymbol could not resolve a local (non-exported) LC_SYMTAB symbol
     because it addressed linker file offsets as `slide + fileoff`. Inside a
     dyld shared cache that names unrelated bytes; the correct conversion is
     `slide + LINKEDIT.vmaddr + (fileoff - LINKEDIT.fileoff)`.
  2. MSHookMessageEx only returned the original IMP and installed nothing.

These tests compile the exact production provider source on the macOS host
and exercise synthetic Mach-O images plus a small Objective-C fixture. They do
not touch a device, WindowServer, or any Apple binary.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROVIDER = ROOT / "misc/cydiasubstrate_macos.m"

FIXTURE_SOURCE = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#import <Foundation/Foundation.h>
#import <objc/runtime.h>

extern void *MSFindSymbol(const void *image, const char *name);
extern void MSHookMessageEx(Class cls, SEL sel, IMP imp, IMP *result);

#define BUF_SIZE 0x10000u

typedef struct {
    uint8_t *buf;
    struct mach_header_64 *mh;
    struct symtab_command *symtab;
    struct linkedit_data_command *exports;
} MachOFixture;

static void put_segment(struct segment_command_64 *seg, const char *name,
                        uint64_t vmaddr, uint64_t vmsize,
                        uint64_t fileoff, uint64_t filesize) {
    memset(seg, 0, sizeof(*seg));
    seg->cmd = LC_SEGMENT_64;
    seg->cmdsize = sizeof(*seg);
    strncpy(seg->segname, name, sizeof(seg->segname) - 1);
    seg->vmaddr = vmaddr;
    seg->vmsize = vmsize;
    seg->fileoff = fileoff;
    seg->filesize = filesize;
    seg->maxprot = seg->initprot = 5;
}

static uint32_t write_export_trie(uint8_t *dst) {
    // Root: terminalSize=0, one child labelled "_macws_exported_fixture",
    // child offset 27. Child: terminal flags=0, addr=0x200, no children.
    static const char label[] = "_macws_exported_fixture";
    const size_t root_len = 2 + sizeof(label) + 1;
    assert(root_len < 128);
    dst[0] = 0;
    dst[1] = 1;
    memcpy(dst + 2, label, sizeof(label));
    dst[2 + sizeof(label)] = (uint8_t)root_len;
    uint8_t *child = dst + root_len;
    child[0] = 3;       // flags + address payload length
    child[1] = 0;       // flags
    child[2] = 0x80;    // address 0x200 (uleb128)
    child[3] = 0x04;
    child[4] = 0;       // no children
    return (uint32_t)(root_len + 5);
}

// Build a two-segment image whose __LINKEDIT file offsets deliberately do
// not share the __TEXT `vmaddr - fileoff` identity. This is the layout a
// dyld shared-cache image exposes to a manual symbol walker.
static MachOFixture build_fixture(uint64_t linkedit_vmaddr,
                                  uint64_t linkedit_fileoff) {
    MachOFixture f = {0};
    f.buf = calloc(1, BUF_SIZE);
    assert(f.buf);
    f.mh = (struct mach_header_64 *)f.buf;
    struct segment_command_64 *text =
        (struct segment_command_64 *)(f.mh + 1);
    struct segment_command_64 *linkedit = text + 1;
    struct symtab_command *symtab =
        (struct symtab_command *)(linkedit + 1);
    struct linkedit_data_command *exports =
        (struct linkedit_data_command *)(symtab + 1);
    f.symtab = symtab;
    f.exports = exports;

    f.mh->magic = MH_MAGIC_64;
    f.mh->cputype = CPU_TYPE_ARM64;
    f.mh->filetype = MH_DYLIB;
    f.mh->ncmds = 4;
    f.mh->sizeofcmds = (uint32_t)((uint8_t *)(exports + 1) -
                                  (uint8_t *)f.mh);
    put_segment(text, "__TEXT", 0x1000, 0x1000, 0, 0x1000);
    put_segment(linkedit, "__LINKEDIT", linkedit_vmaddr, 0x4000,
                linkedit_fileoff, 0x2000);

    const uint64_t delta = linkedit_vmaddr - 0x1000;  // mapped offset
    uint8_t *le = f.buf + delta;                      // LINKEDIT mapping

    symtab->cmd = LC_SYMTAB;
    symtab->cmdsize = sizeof(*symtab);
    symtab->symoff = (uint32_t)(linkedit_fileoff + 0x40);
    symtab->nsyms = 1;
    symtab->stroff = (uint32_t)(linkedit_fileoff + 0x100);
    symtab->strsize = 0x100;

    struct nlist_64 *nl = (struct nlist_64 *)(le + 0x40);
    nl->n_un.n_strx = 1;
    nl->n_type = N_SECT;  // local defined symbol, no N_EXT
    nl->n_sect = 1;
    nl->n_value = 0x1120; // unslid vmaddr -> runtime buf + 0x120
    const char *name = "_macws_local_fixture";
    memcpy(le + 0x101, name, strlen(name) + 1);

    exports->cmd = LC_DYLD_EXPORTS_TRIE;
    exports->cmdsize = sizeof(*exports);
    exports->dataoff = (uint32_t)(linkedit_fileoff + 0x200);
    exports->datasize = write_export_trie(
        le + 0x200);
    return f;
}

@interface ProviderFixture : NSObject
- (int)value;
+ (int)classValue;
@end

@implementation ProviderFixture
- (int)value { return 3; }
+ (int)classValue { return 11; }
@end

@interface ProviderFixtureChild : ProviderFixture
@end

@implementation ProviderFixtureChild
@end

static int g_replace_calls;
static int (*g_orig_value)(id, SEL);

static int replacement_value(id self, SEL cmd) {
    (void)self; (void)cmd;
    g_replace_calls++;
    return 7;
}

static int replacement_via_orig(id self, SEL cmd) {
    return g_orig_value ? g_orig_value(self, cmd) + 1 : -1;
}

static int (*g_orig_class_value)(id, SEL);
static int replacement_class_value(id self, SEL cmd) {
    (void)self; (void)cmd;
    return 13;
}
static int replacement_class_via_orig(id self, SEL cmd) {
    return g_orig_class_value ? g_orig_class_value(self, cmd) + 2 : -1;
}

static void test_symbol_resolution(void) {
    // Shared-cache-shaped layout: fileoff (0x3000) != mapped offset (0x9000).
    MachOFixture cache = build_fixture(0xA000, 0x3000);
    void *local = MSFindSymbol(cache.mh, "_macws_local_fixture");
    printf("cache-local=%p expected=%p\n", local, cache.buf + 0x120);
    assert(local == cache.buf + 0x120);
    void *exported = MSFindSymbol(cache.mh, "_macws_exported_fixture");
    printf("cache-exported=%p expected=%p\n", exported, cache.buf + 0x200);
    assert(exported == cache.buf + 0x200);
    assert(MSFindSymbol(cache.mh, "_macws_absent_fixture") == NULL);
    free(cache.buf);

    // Ordinary contiguous Mach-O: regression guard for non-cache images.
    MachOFixture plain = build_fixture(0x4000, 0x3000);
    void *plain_local = MSFindSymbol(plain.mh, "_macws_local_fixture");
    printf("plain-local=%p expected=%p\n", plain_local, plain.buf + 0x120);
    assert(plain_local == plain.buf + 0x120);
    void *plain_exported = MSFindSymbol(plain.mh, "_macws_exported_fixture");
    assert(plain_exported == plain.buf + 0x200);
    free(plain.buf);

    // A header without __TEXT/__LINKEDIT must fail closed, not guess.
    struct mach_header_64 empty = {0};
    empty.magic = MH_MAGIC_64;
    empty.ncmds = 0;
    assert(MSFindSymbol(&empty, "_macws_local_fixture") == NULL);
}

static void test_message_hook(void) {
    Class parent = objc_getClass("ProviderFixture");
    Class child = objc_getClass("ProviderFixtureChild");
    assert(parent && child);
    SEL value = @selector(value);
    SEL classValue = @selector(classValue);
    ProviderFixture *parentInstance = [[ProviderFixture alloc] init];
    ProviderFixtureChild *childInstance =
        [[ProviderFixtureChild alloc] init];

    // Exact-class hook of an inherited method: child changes, parent does
    // not, and the returned original is callable.
    IMP original = NULL;
    MSHookMessageEx(child, value, (IMP)replacement_value, &original);
    assert(original);
    g_orig_value = (int (*)(id, SEL))original;
    assert([childInstance value] == 7);
    assert(g_replace_calls == 1);
    assert(g_orig_value(childInstance, value) == 3);
    assert([parentInstance value] == 3);

    // Re-hooking returns the implementation that was actually installed and
    // a replacement can forward through its original.
    IMP previous = NULL;
    MSHookMessageEx(child, value, (IMP)replacement_via_orig, &previous);
    assert(previous);
    assert(((int (*)(id, SEL))previous)(childInstance, value) == 7);
    assert([childInstance value] == 4);
    assert([parentInstance value] == 3);

    // NULL replacement fails closed: the original is still published and
    // the installed replacement is untouched.
    IMP unchanged = NULL;
    MSHookMessageEx(child, value, NULL, &unchanged);
    assert(unchanged);
    assert([childInstance value] == 4);

    // Missing selector fails closed with a NULL original.
    IMP missing = (IMP)(uintptr_t)0x1;
    MSHookMessageEx(child, sel_registerName("macwsAbsentFixtureSelector"),
                    (IMP)replacement_value, &missing);
    assert(missing == NULL);

    // Class methods: the metaclass is the hook target and keeps the same
    // original-callable contract.
    IMP originalClass = NULL;
    MSHookMessageEx(object_getClass((id)parent), classValue,
                    (IMP)replacement_class_value, &originalClass);
    assert(originalClass);
    g_orig_class_value = (int (*)(id, SEL))originalClass;
    assert([ProviderFixture classValue] == 13);
    assert(g_orig_class_value((id)parent, classValue) == 11);

    IMP previousClass = NULL;
    MSHookMessageEx(object_getClass((id)parent), classValue,
                    (IMP)replacement_class_via_orig, &previousClass);
    assert(previousClass);
    assert([ProviderFixture classValue] == 13);
    (void)previousClass;
}

int main(void) {
    test_symbol_resolution();
    test_message_hook();
    puts("provider-contract PASS: LINKEDIT local/export resolution + "
         "MSHookMessageEx install/original/exact-class");
    return 0;
}
'''


class CydiaSubstrateProviderContractTests(unittest.TestCase):
    def test_provider_source_and_behavior(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("C/ObjC compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="macws-provider-") as tmp:
            source = pathlib.Path(tmp) / "main.m"
            binary = pathlib.Path(tmp) / "provider-test"
            source.write_text(FIXTURE_SOURCE)
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", "-fno-objc-arc",
                 "-I", str(ROOT / "include"),
                 str(source), str(PROVIDER),
                 "-framework", "Foundation", "-ldl",
                 "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(
                result.returncode, 0,
                "provider test build failed:\n" + result.stdout +
                result.stderr)
            run = subprocess.run([str(binary)], capture_output=True,
                                 text=True, timeout=20)
            self.assertEqual(
                run.returncode, 0,
                "provider test failed:\n" + run.stdout + run.stderr)
            self.assertIn("provider-contract PASS", run.stdout)

    def test_provider_source_contract(self):
        source = PROVIDER.read_text()
        # LINKEDIT conversion must use both the segment vmaddr and fileoff.
        self.assertIn("linkedit_file_ptr(", source)
        self.assertIn("linkedit->fileoff", source)
        self.assertIn("linkedit->filesize", source)
        self.assertIn("symtab->symoff", source)
        self.assertIn("symtab->stroff", source)
        # Method hook must install through libobjc, not return-and-drop.
        self.assertIn("class_replaceMethod(", source)
        self.assertNotIn("(void)imp;", source)
        self.assertNotIn("NO-OP stub (method-swizzle not implemented)",
                         source)
        # Containment decision from R003 must remain intact.
        self.assertIn("MACWSHook: REFUSED", source)
        self.assertIn("macws_permit_for(", source)


if __name__ == "__main__":
    unittest.main()
