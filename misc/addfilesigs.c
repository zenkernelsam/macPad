/* addsigs: attach the dyld_shared_cache's embedded superblob to its vnode
 * via fcntl(F_ADDFILESIGS_RETURN) — same call dyld's SharedCacheRuntime
 * uses (SharedCacheRuntime.cpp:301). Prints errno/resulting end offset so
 * we can see whether iOS accepts the macOS cache's embedded adhoc blob.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/fcntl.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/var/mnt/rootfs/macdsc/dyld_shared_cache_arm64e";
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    /* dyld_cache_header: codeSignatureOffset @ 0x28, codeSignatureSize @ 0x30 */
    uint8_t hdr[0x40];
    if (pread(fd, hdr, sizeof(hdr), 0) != sizeof(hdr)) { perror("pread"); return 1; }
    uint64_t cso = *(uint64_t *)(hdr + 0x28);
    uint64_t css = *(uint64_t *)(hdr + 0x30);
    off_t fsize = lseek(fd, 0, SEEK_END);
    fprintf(stderr, "[*] size=0x%llx csOff=0x%llx csSize=0x%llx magic=%.16s\n",
            (unsigned long long)fsize, (unsigned long long)cso,
            (unsigned long long)css, hdr);
    if (css == 0) css = fsize - cso;   /* "zero means to end of file" */

    fsignatures_t sig;
    memset(&sig, 0, sizeof(sig));
    sig.fs_file_start = 0;
    sig.fs_blob_start = (void *)(uintptr_t)cso;
    sig.fs_blob_size  = (int)css;

    errno = 0;
    int r = fcntl(fd, F_ADDFILESIGS_RETURN, &sig);
    fprintf(stderr, "[*] fcntl(F_ADDFILESIGS_RETURN) r=%d errno=%d(%s) end_off=0x%llx\n",
            r, errno, strerror(errno), (unsigned long long)sig.fs_file_start);

    /* also try INFO variant to query existing blob state */
    fsignatures_t sig2;
    memset(&sig2, 0, sizeof(sig2));
    sig2.fs_file_start = 0;
    sig2.fs_blob_start = (void *)(uintptr_t)cso;
    sig2.fs_blob_size  = (int)css;
    errno = 0;
    r = fcntl(fd, F_ADDFILESIGS_INFO, &sig2);
    fprintf(stderr, "[*] fcntl(F_ADDFILESIGS_INFO) r=%d errno=%d(%s) end_off=0x%llx cdhash=",
            r, errno, strerror(errno), (unsigned long long)sig2.fs_file_start);
    for (int i = 0; i < 20; i++) fprintf(stderr, "%02x", sig2.fs_cdhash[i]);
    fprintf(stderr, "\n");
    return 0;
}
