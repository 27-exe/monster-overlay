// mhw_fix.c — LD_PRELOAD helper to reserve 0x27B90000-0x27C00000
// so MHW's VirtualAlloc(0x27B97760) succeeds under Wine.
// Compile: gcc -shared -fPIC -o mhw_fix.so mhw_fix.c -ldl
// Usage: LD_PRELOAD=./mhw_fix.so %command%  (in Steam launch options)

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>

// Run early, before Wine initializes its heap/DLL mappings.
__attribute__((constructor(101))) static void reserve_monster_region(void)
{
    const unsigned long addr = 0x27B90000UL;
    const size_t size = 0x20000UL; // 128 KB — enough for the MonsterList

    // Check if already free by trying a regular mmap
    void *probe = mmap((void *)addr, size,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                       -1, 0);
    if (probe == MAP_FAILED) {
        if (errno == EEXIST) {
            // Region occupied — try to unmask it with MAP_FIXED
            probe = mmap((void *)addr, size,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                         -1, 0);
            if (probe == MAP_FAILED) {
                fprintf(stderr, "mhw_fix: 0x%lx-0x%lx occupied, cannot reserve\n",
                        addr, addr + size);
                return;
            }
            fprintf(stderr, "mhw_fix: replaced existing mapping at 0x%lx\n", addr);
        } else {
            fprintf(stderr, "mhw_fix: mmap failed: %s\n", strerror(errno));
            return;
        }
    }

    // Unmap it — leaves a hole that Wine won't use
    munmap(probe, size);
    fprintf(stderr, "mhw_fix: reserved hole 0x%lx-0x%lx for MHW\n",
            addr, addr + size);
}
