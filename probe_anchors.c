/* WineForge-Internal: native-steam/passive-upstream-anchor-check-v1.
 * Map segment data without PROT_EXEC. No dlopen of a Valve library, no calls
 * into Steam. Uses the original resolver only for its 16 named client anchors.
 */
#include "src/dylib/core/macho.h"
#include "src/dylib/resolver/resolver.h"
#include "src/dylib/resolver/sigdb.h"
#include "src/dylib/feats/compat.h"
#include "src/vendor/cJSON.h"
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

/* WineForge-Internal: native-steam/passive-selector-rebases-v1.
 * This is a data-only image, so dyld has not rebased its selector pointers.
 * Normalize only validated local selector strings. Never bind imports or run
 * Objective-C/Steam initializers. AOB fallback used to hide this omission on
 * older clients until a GOT displacement changed. */
static int selector_string(const struct mach_header_64 *header, size_t length,
                            uint64_t target) {
    const unsigned char *cursor = (const void *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const struct load_command *lc = (const void *)cursor;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const void *)lc;
            if (lc->cmdsize < sizeof(*seg) || seg->nsects >
                (lc->cmdsize - sizeof(*seg)) / sizeof(struct section_64)) return 0;
            const struct section_64 *section = (const void *)(seg + 1);
            for (uint32_t j = 0; j < seg->nsects; ++j) {
                const struct section_64 *s = &section[j];
                if (strncmp(s->sectname, "__objc_methname", 16) &&
                    strncmp(s->sectname, "__cstring", 16)) continue;
                if (target < s->addr || target - s->addr >= s->size ||
                    s->offset > length || s->size > length - s->offset) continue;
                size_t relative = (size_t)(target - s->addr);
                size_t available = (size_t)s->size - relative;
                if (available > 1024) available = 1024;
                return memchr((const unsigned char *)header + s->offset + relative,
                              0, available) != NULL;
            }
        }
        cursor += lc->cmdsize;
    }
    return 0;
}

static int normalize_selector_refs(const struct mach_header_64 *header,
                                    size_t length, unsigned char *image,
                                    size_t image_size) {
    const unsigned char *raw = (const void *)header;
    const unsigned char *cursor = (const void *)(header + 1);
    const unsigned char *starts = NULL;
    size_t starts_size = 0;
    uint32_t segment_count = 0;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const struct load_command *lc = (const void *)cursor;
        if (lc->cmd == LC_DYLD_CHAINED_FIXUPS) {
            if (starts || lc->cmdsize < sizeof(struct linkedit_data_command)) return 0;
            const struct linkedit_data_command *fixups = (const void *)lc;
            if (fixups->dataoff > length || fixups->datasize > length - fixups->dataoff ||
                fixups->datasize < 28) return 0;
            const unsigned char *data = raw + fixups->dataoff;
            uint32_t version, offset;
            memcpy(&version, data, 4);
            memcpy(&offset, data + 4, 4);
            if (version || offset > fixups->datasize || fixups->datasize - offset < 4) return 0;
            starts = data + offset;
            starts_size = fixups->datasize - offset;
            memcpy(&segment_count, starts, 4);
            if (segment_count > (starts_size - 4) / 4) return 0;
        }
        cursor += lc->cmdsize;
    }
    cursor = (const void *)(header + 1);
    uint32_t segment_index = 0;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const struct load_command *lc = (const void *)cursor;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const void *)lc;
            if (lc->cmdsize < sizeof(*seg) || seg->nsects >
                (lc->cmdsize - sizeof(*seg)) / sizeof(struct section_64)) return 0;
            const struct section_64 *section = (const void *)(seg + 1);
            for (uint32_t j = 0; j < seg->nsects; ++j) {
                const struct section_64 *s = &section[j];
                if (strncmp(s->sectname, "__objc_selrefs", 16)) continue;
                if ((s->addr | s->size) & 7 || s->size > 512 * 1024 ||
                    s->addr > image_size || s->size > image_size - s->addr) return 0;
                uint16_t format = 0;
                if (starts) {
                    uint32_t offset;
                    if (segment_index >= segment_count) return 0;
                    memcpy(&offset, starts + 4 + segment_index * 4, 4);
                    if (offset) {
                        uint32_t size;
                        uint64_t base;
                        if (offset > starts_size || starts_size - offset < 22) return 0;
                        memcpy(&size, starts + offset, 4);
                        memcpy(&format, starts + offset + 6, 2);
                        memcpy(&base, starts + offset + 8, 8);
                        if (size < 22 || size > starts_size - offset || base != seg->vmaddr ||
                            (format != 2 && format != 6)) return 0;
                    }
                }
                for (size_t k = 0; k < s->size; k += 8) {
                    uint64_t pointer;
                    memcpy(&pointer, image + s->addr + k, 8);
                    if (!pointer) continue;
                    uint64_t target = pointer;
                    if (format) {
                        /* DYLD_CHAINED_PTR_64 / DYLD_CHAINED_PTR_64_OFFSET,
                         * preferred image base zero. Refuse binds/tagged data. */
                        if ((pointer >> 63) || ((pointer >> 36) & 0x7fff)) return 0;
                        target = pointer & UINT64_C(0xfffffffff);
                    }
                    if (target >= image_size || !selector_string(header, length, target)) return 0;
                    pointer = (uintptr_t)image + target;
                    memcpy(image + s->addr + k, &pointer, 8);
                }
            }
            ++segment_index;
        }
        cursor += lc->cmdsize;
    }
    return 1;
}

int main(int argc, char **argv) {
    int json_output = argc > 1 && !strcmp(argv[1], "--json");
    if (json_output) { --argc; ++argv; }
    if (argc != 3) return 2;
    int fd = open(argv[1], O_RDONLY); struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 28 || st.st_size > 128 * 1024 * 1024) return 2;
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
    if (!normalize_selector_refs(header, length, image, maximum)) return 2;
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
    if (json_output) {
        cJSON *result = cJSON_CreateObject();
        if (!result) return 2;
        cJSON_AddNumberToObject(result, "protocolVersion", 1);
        cJSON_AddBoolToObject(result, "compatible", expected == 16 && count == expected && layout);
        cJSON_AddNumberToObject(result, "signaturesResolved", count);
        cJSON_AddNumberToObject(result, "signaturesExpected", expected);
        cJSON_AddBoolToObject(result, "layoutValidated", layout);
        cJSON_AddBoolToObject(result, "clientCodeExecuted", 0);
        cJSON *missing = cJSON_AddArrayToObject(result, "missingSignatures");
        for (int i = 0; i < resolved.used; ++i) {
            if (resolved.items[i].site) continue;
            for (int j = 0; j < profile.sig_count; ++j) {
                if (strcmp(profile.signatures[j].name, resolved.items[i].name) ||
                    strcmp(profile.signatures[j].module, "steamclient.dylib") ||
                    profile.signatures[j].deprecated) continue;
                cJSON_AddItemToArray(missing, cJSON_CreateString(resolved.items[i].name));
                break;
            }
        }
        char *text = cJSON_PrintUnformatted(result);
        if (!text) return 2;
        puts(text);
        free(text);
        cJSON_Delete(result);
    } else {
        printf("anchors=%d/%d layout=%s client_code_executed=0\n", count, expected, count != expected ? "SKIPPED" : layout ? "PASS" : "REJECT");
        for (int i=0;i<resolved.used;i++) printf("site %s %llx\n", resolved.items[i].name, (unsigned long long)(resolved.items[i].site ? resolved.items[i].site-(uintptr_t)image : 0));
    }
    np_free_resolution(&resolved); np_free_profile(&profile);
    munmap(image,maximum); munmap(raw,st.st_size);
    return expected == 16 && count == expected && layout ? 0 : 1;
}
