/*
 * Minimal /usr/lib/system/libdyld.dylib replacement so that dyld's disk-fallback
 * (no shared cache) can proceed.  dyld requires an image whose install name is
 * exactly "/usr/lib/system/libdyld.dylib" that carries:
 *   - section __TPRO_CONST,__dyld_apis   (8 bytes; dyld writes &state here)
 *   - section __DATA_CONST,__helper      (8 bytes = a LibSystemHelpers vtable ptr,
 *                                          ptrauth-signed; version() must be >= 7)
 * dyld then calls setDefaultProgramVars() to point the process at our globals.
 *
 * The ptrauth type discriminator is derived from the class's MANGLED NAME, so we
 * replicate namespace `dyld4` + class `LibSystemHelpers` with the identical virtual
 * slot order and the identical ptrauth_vtable_pointer attribute.  This makes our
 * vptr authenticate correctly against dyld's static `const LibSystemHelpers*` calls.
 *
 * All methods use raw arm64 Darwin syscalls (no libSystem dependency).
 */
#include <stddef.h>
#include <stdint.h>

#define VIS_HIDDEN __attribute__((visibility("hidden")))

static inline long _svc0(long n) {
    register long x16 __asm__("x16") = n;
    register long x0  __asm__("x0");
    __asm__ volatile("svc #0x80" : "=r"(x0) : "r"(x16) : "memory", "cc");
    return x0;
}
static inline void _dbg(char c){    // write(2,&c,1)
    char buf[2]={c,0};
    register long x16 __asm__("x16") = 4;
    register long x0  __asm__("x0")  = 2;
    register const void* xp __asm__("x1") = buf;
    register long x2 __asm__("x2") = 1;
    __asm__ volatile("svc #0x80" :: "r"(x16),"r"(x0),"r"(xp),"r"(x2) : "memory","cc");
}

// ---- globals that dyld fills in via setDefaultProgramVars ----
extern "C" {
char*  __progname = (char*)"";
int    NXArgc     = 0;
char** NXArgv     = 0;
char** environ    = 0;
}

namespace mach_o {
struct Error { void* _opaque; Error() : _opaque(0) {} explicit Error(const char*) : _opaque(0) {} operator bool() const { return false; } const char* message() const { return ""; } };
}

namespace dyld4 {

struct ProgramVars {
    const void*   mh            = nullptr;
    int*          NXArgcPtr     = nullptr;
    const char*** NXArgvPtr     = nullptr;
    const char*** environPtr    = nullptr;
    const char**  __prognamePtr = nullptr;
};

typedef bool (*FuncLookup)(const char* name, void** addr);

// Replica of dyld4::LibSystemHelpers -- name+slot order must match exactly.
struct VIS_HIDDEN [[clang::ptrauth_vtable_pointer(process_independent, address_discrimination, type_discrimination)]] LibSystemHelpers
{
    virtual uintptr_t       version() const;
    virtual void*           malloc(size_t size) const;
    virtual void            free(void* p) const;
    virtual size_t          malloc_size(const void* p) const;
    virtual int             vm_allocate(unsigned int task, uintptr_t* address, uintptr_t size, int flags) const;
    virtual int             vm_deallocate(unsigned int task, uintptr_t address, uintptr_t size) const;
    virtual int             pthread_key_create_free(unsigned int* key) const;
    virtual void*           pthread_getspecific(unsigned int key) const;
    virtual int             pthread_setspecific(unsigned int key, const void* value) const;
    virtual void            __cxa_atexit(void (*func)(void*), void* arg, void* dso) const;
    virtual void            __cxa_finalize_ranges(const void* ranges, unsigned int count) const;
    virtual bool            isLaunchdOwned() const;
    virtual void            os_unfair_recursive_lock_lock_with_options(void* lock, int options) const;
    virtual void            os_unfair_recursive_lock_unlock(void* lock) const;
    virtual void            exit(int result) const __attribute__((__noreturn__));
    virtual const char*     getenv(const char* key) const;
    virtual int             mkstemp(char* templatePath) const;
    virtual void            os_unfair_recursive_lock_unlock_forked_child(void* lock) const;
    virtual void            setDyldPatchedObjCClasses() const;
    virtual void            run_async(void* (*func)(void*), void* context) const;
    virtual void            os_unfair_lock_lock_with_options(void* lock, int options) const;
    virtual void            os_unfair_lock_unlock(void* lock) const;
    virtual void            setDefaultProgramVars(ProgramVars& vars) const;
    virtual FuncLookup      legacyDyldFuncLookup() const;
    virtual mach_o::Error   setUpThreadLocals(const void* cache, const void* hdr) const;
};

// ---------------------------------------------------------------------------
// implementations
// ---------------------------------------------------------------------------
uintptr_t LibSystemHelpers::version() const { _dbg('v'); return 7; }

void* LibSystemHelpers::malloc(size_t) const { _dbg('M'); return 0; }
void  LibSystemHelpers::free(void*) const { _dbg('f'); }
size_t LibSystemHelpers::malloc_size(const void*) const { _dbg('z'); return 0; }

int LibSystemHelpers::vm_allocate(unsigned int, uintptr_t*, uintptr_t, int) const { _dbg('A'); return 1; }
int LibSystemHelpers::vm_deallocate(unsigned int, uintptr_t, uintptr_t) const { _dbg('D'); return 0; }

int   LibSystemHelpers::pthread_key_create_free(unsigned int* k) const { _dbg('K'); if(k)*k=0; return 0; }
void* LibSystemHelpers::pthread_getspecific(unsigned int) const { _dbg('g'); return 0; }
int   LibSystemHelpers::pthread_setspecific(unsigned int, const void*) const { _dbg('s'); return 0; }

void LibSystemHelpers::__cxa_atexit(void (*)(void*), void*, void*) const { _dbg('a'); }
void LibSystemHelpers::__cxa_finalize_ranges(const void*, unsigned int) const { _dbg('F'); }

bool LibSystemHelpers::isLaunchdOwned() const { _dbg('L'); return false; }

// single-threaded at dyld time -> no-op locks are safe
void LibSystemHelpers::os_unfair_recursive_lock_lock_with_options(void*, int) const { _dbg('R'); }
void LibSystemHelpers::os_unfair_recursive_lock_unlock(void*) const { _dbg('r'); }
void LibSystemHelpers::os_unfair_recursive_lock_unlock_forked_child(void*) const { _dbg('u'); }
void LibSystemHelpers::os_unfair_lock_lock_with_options(void*, int) const { _dbg('q'); }
void LibSystemHelpers::os_unfair_lock_unlock(void*) const { _dbg('Q'); }

void LibSystemHelpers::exit(int result) const { _dbg('x');
    register long x16 __asm__("x16") = 1; /* SYS_exit */
    register long x0  __asm__("x0")  = result;
    __asm__ volatile("svc #0x80" :: "r"(x16), "r"(x0) : "memory");
    for (;;) {}
}

const char* LibSystemHelpers::getenv(const char*) const { _dbg('E'); return 0; }
int         LibSystemHelpers::mkstemp(char*) const { _dbg('t'); return -1; }
void        LibSystemHelpers::setDyldPatchedObjCClasses() const { _dbg('P'); }
void        LibSystemHelpers::run_async(void* (*f)(void*), void* c) const { _dbg('y'); f(c); }

void LibSystemHelpers::setDefaultProgramVars(ProgramVars& vars) const { _dbg('V');
    vars.NXArgcPtr     = &NXArgc;
    vars.NXArgvPtr     = (const char***)&NXArgv;
    vars.environPtr    = (const char***)&environ;
    vars.__prognamePtr = (const char**)&__progname;
}

FuncLookup    LibSystemHelpers::legacyDyldFuncLookup() const { _dbg('l'); return 0; }
mach_o::Error LibSystemHelpers::setUpThreadLocals(const void*, const void*) const { _dbg('T'); return mach_o::Error(); }

// The helper object that lives in __DATA_CONST,__helper (8 bytes = vptr only).
__attribute__((section("__DATA_CONST,__helper"), used))
const LibSystemHelpers g_helper;

// __TPRO_CONST,__dyld_apis (8 bytes; dyld overwrites with &state).
__attribute__((section("__TPRO_CONST,__dyld_apis"), used))
void* g_dyldAPIs = 0;

} // namespace dyld4

// Plain-C anchor exported so that /usr/lib/libSystem.B.dylib can hold an
// LC_LOAD_DYLIB on this image (which makes dyld discover libdyldLoader).
extern "C" void* dyldshim_anchor = 0;
