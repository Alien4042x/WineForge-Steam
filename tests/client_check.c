/* WineForge-Internal: native-steam/passive-selector-rebase-controls-v1. */
#define main passive_checker_main
#include "../probe_anchors.c"
#undef main
#include <assert.h>

static void fixture(unsigned char *raw, unsigned char *image, uint16_t format,
                     uint64_t pointer) {
    memset(raw, 0, 2048);
    struct mach_header_64 *h = (void *)raw;
    h->magic = MH_MAGIC_64;
    h->ncmds = 2;
    h->sizeofcmds = sizeof(struct segment_command_64) + 2 * sizeof(struct section_64)
                   + sizeof(struct linkedit_data_command);
    struct segment_command_64 *seg = (void *)(h + 1);
    seg->cmd = LC_SEGMENT_64;
    seg->cmdsize = sizeof(*seg) + 2 * sizeof(struct section_64);
    seg->nsects = 2;
    struct section_64 *s = (void *)(seg + 1);
    strcpy(s[0].sectname, "__objc_methname");
    s[0].addr = s[0].offset = 0x300;
    s[0].size = 16;
    strcpy(s[1].sectname, "__objc_selrefs");
    s[1].addr = s[1].offset = 0x400;
    s[1].size = 8;
    memcpy(raw + 0x300, "terminate", 10);
    memcpy(raw + 0x400, &pointer, 8);
    struct linkedit_data_command *f = (void *)((unsigned char *)seg + seg->cmdsize);
    f->cmd = LC_DYLD_CHAINED_FIXUPS;
    f->cmdsize = sizeof(*f);
    f->dataoff = 0x500;
    f->datasize = 128;
    uint32_t offset = 28, count = 1, segment = 8, size = 24;
    memcpy(raw + 0x504, &offset, 4);
    memcpy(raw + 0x51c, &count, 4);
    memcpy(raw + 0x520, &segment, 4);
    memcpy(raw + 0x524, &size, 4);
    memcpy(raw + 0x52a, &format, 2);
    memcpy(image, raw, 2048);
}

int main(void) {
    unsigned char raw[2048], image[2048];
    uint64_t value;
    for (uint16_t format = 2; format <= 6; format += 4) {
        fixture(raw, image, format, UINT64_C(0x10000000000300));
        assert(normalize_selector_refs((void *)raw, sizeof(raw), image, sizeof(image)));
        memcpy(&value, image + 0x400, 8);
        assert(value == (uintptr_t)image + 0x300);
    }
    fixture(raw, image, 1, 0x300);
    assert(!normalize_selector_refs((void *)raw, sizeof(raw), image, sizeof(image)));
    fixture(raw, image, 6, UINT64_C(0x8000000000000300));
    assert(!normalize_selector_refs((void *)raw, sizeof(raw), image, sizeof(image)));
    fixture(raw, image, 6, UINT64_C(0x0000001000000300));
    assert(!normalize_selector_refs((void *)raw, sizeof(raw), image, sizeof(image)));
    fixture(raw, image, 6, 0x700);
    assert(!normalize_selector_refs((void *)raw, sizeof(raw), image, sizeof(image)));
    fixture(raw, image, 6, 0x300);
    memset(raw + 0x300, 'x', 16);
    assert(!normalize_selector_refs((void *)raw, sizeof(raw), image, sizeof(image)));
    fixture(raw, image, 6, 0x300);
    assert(!normalize_selector_refs((void *)raw, sizeof(raw), image, 0x400));
    fixture(raw, image, 6, 0x300);
    assert(!normalize_selector_refs((void *)raw, 0x510, image, sizeof(image)));
    fixture(raw, image, 6, 0x300);
    ((struct mach_header_64 *)raw)->ncmds = 1;
    assert(normalize_selector_refs((void *)raw, sizeof(raw), image, sizeof(image)));
    puts("selector rebases: valid 64/64-offset and malformed/unsupported controls PASS");
    return 0;
}
