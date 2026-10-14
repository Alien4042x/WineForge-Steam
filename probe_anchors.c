/* WineForge-Internal: native-steam/passive-upstream-anchor-check-v1.
 * Map segment data without PROT_EXEC. No dlopen of a Valve library, no calls
 * into Steam. Uses the original resolver only for its 16 named client anchors.
 */
#include "src/dylib/core/macho.h"
#include "src/dylib/resolver/resolver.h"
#include "src/dylib/resolver/sigdb.h"
#include "src/dylib/feats/compat.h"
#include <mach-o/fat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

static uint32_t big32(uint32_t n) { return __builtin_bswap32(n); }
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    int fd = open(argv[1], O_RDONLY); struct stat st;
    if (fd < 0 || fstat(fd, &st)) return 2;
    unsigned char *raw = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (raw == MAP_FAILED || st.st_size < 28) return 2;
    struct fat_header *fat = (void *)raw;
    if (big32(fat->magic) != FAT_MAGIC || big32(fat->nfat_arch) > 8) return 2;
    if (8 + big32(fat->nfat_arch) * sizeof(struct fat_arch) > (size_t)st.st_size) return 2;
    size_t offset = 0, length = 0;
    struct fat_arch *arch = (void *)(fat + 1);
    for (unsigned i = 0; i < big32(fat->nfat_arch); i++) {
        if (big32(arch[i].cputype) == CPU_TYPE_ARM64) {
            offset = big32(arch[i].offset); length = big32(arch[i].size);
        }
    }
    if (!length || offset > st.st_size || length > st.st_size - offset) return 2;
    const struct mach_header_64 *header = (void *)(raw + offset);
    if (length < sizeof(*header) || header->magic != MH_MAGIC_64 || header->sizeofcmds > length - sizeof(*header)) return 2;
    const unsigned char *cursor = (void *)(header + 1), *end = cursor + header->sizeofcmds;
    uint64_t maximum = 0;
    int valid_header_segment = 0;
    for (unsigned i = 0; i < header->ncmds; i++) {
        if (end - cursor < sizeof(struct load_command)) return 2;
        const struct load_command *lc = (void *)cursor;
        if (lc->cmdsize < sizeof(*lc) || lc->cmdsize > end - cursor) return 2;
        if (lc->cmd == LC_SEGMENT_64) {
            if (lc->cmdsize < sizeof(struct segment_command_64)) return 2;
            const struct segment_command_64 *s = (void *)cursor;
            if (s->vmaddr > 256*1024*1024 || s->vmsize > 256*1024*1024 - s->vmaddr || s->filesize > s->vmsize || s->fileoff > length || s->filesize > length-s->fileoff) return 2;
            if (s->vmaddr+s->vmsize > maximum) maximum = s->vmaddr+s->vmsize;
            if (!strncmp(s->segname, "__TEXT", sizeof(s->segname)) && !s->vmaddr && !s->fileoff && s->filesize >= header->sizeofcmds + sizeof(*header)) valid_header_segment = 1;
        }
        cursor += lc->cmdsize;
    }
    if (!maximum || !valid_header_segment) return 2;
    unsigned char *image = mmap(NULL, maximum, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (image == MAP_FAILED) return 2;
    cursor = (void *)(header + 1);
    for (unsigned i = 0; i < header->ncmds; i++) {
        const struct load_command *lc = (void *)cursor;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *s = (void *)cursor;
            memcpy(image+s->vmaddr, (unsigned char *)header+s->fileoff, s->filesize);
        }
        cursor += lc->cmdsize;
    }
    /* All mapped image pages remain data; the resolver never executes them. */
    if (mprotect(image, maximum, PROT_READ)) return 2;
    np_sigdb_t profile = {0}; np_resolve_result_t resolved = {0};
    if (np_load_profile(argv[2], &profile)) return 2;
    int expected = np_required_for_module(&profile, "steamclient.dylib");
    int count = np_resolve_signatures((void *)image, (intptr_t)image, &profile, "steamclient.dylib", &resolved);
    int layout = 0;
    if (expected == 16 && count == expected) {
        layout = np_compat_probe_tool_layout(np_lookup_address(&resolved, "CCompatManager::ResolveCompatToolForApp.local_redirect"), np_lookup_address(&resolved, "CCompatManager::GetOSListOverrideForApp.oslist_gate")) && np_compat_probe_enabled_off(np_lookup_address(&resolved, "CCompatManager::BIsCompatibilityToolEnabled"));
    }
    printf("anchors=%d/%d layout=%s client_code_executed=0\n", count, expected, count != expected ? "SKIPPED" : layout ? "PASS" : "REJECT");
    for (int i=0;i<resolved.used;i++) printf("site %s %llx\n", resolved.items[i].name, (unsigned long long)(resolved.items[i].site ? resolved.items[i].site-(uintptr_t)image : 0));
    np_free_resolution(&resolved); np_free_profile(&profile);
    munmap(image,maximum); munmap(raw,st.st_size);
    return expected == 16 && count == expected && layout ? 0 : 1;
}
