/* Compile the actual embedded panel and guard; no Valve library or process. */
#include "../src/dylib/feats/webpatch.c"
#include "../src/dylib/hooks/hook_webpatch.c"
#include <assert.h>

int np_log_level = -1;
FILE *np_log_file;
static int enabled_reads;
void np_log_init(void) {}
void np_log_attach(void) {}
int np_log_first_hit(const void *anchor, unsigned long tag) { (void)anchor; (void)tag; return 0; }
int wf_integration_enabled(void) { enabled_reads++; return 0; }
char *wf_settings_bridge_json(void) { return NULL; }
const char *np_compat_fallback_tool_name(void) { return "wineforge-proton"; }
int np_compatsvc_routes(void) { return NP_COMPATSVC_READY; }

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--emit-component")) {
        const char *descriptor = "{\"protocolVersion\":1,\"installationID\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"token\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"baseURL\":\"http://127.0.0.1:31415/v1\"}";
        printf("globalThis.__WineForgeSteamSettingsPanelV1=%s(%s);\nvar Component=%s;\n",
               WF_SETTINGS_PANEL_JS, descriptor, NP_WF_OPTIONS_BODY("e", "i"));
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--check-ui")) {
        FILE *file = fopen(argv[2], "rb");
        if (!file) { perror("Steam UI read"); return 2; }
        if (fseek(file, 0, SEEK_END)) { perror("Steam UI seek"); fclose(file); return 2; }
        long end = ftell(file);
        if (end < 1 || end > 33554432 || fseek(file, 0, SEEK_SET)) {
            fprintf(stderr, "Steam UI size/read failed\n"); fclose(file); return 2;
        }
        size_t length = (size_t)end;
        char *source = malloc(length + 1);
        if (!source || fread(source, 1, length, file) != length || ferror(file)) {
            fprintf(stderr, "Steam UI content read failed\n"); free(source); fclose(file); return 2;
        }
        fclose(file);
        source[length] = 0;
        np_webpatch_set_fallback_tool("wineforge-proton");
        size_t transformed_length = 0;
        const char *shape = NULL;
        char *transformed = np_webpatch_transform((const uint8_t *)source, length, &transformed_length, &shape);
        assert(transformed && shape && !strcmp(shape, NP_SHAPE_SELECTTOOL));
        assert(strstr(transformed, "f&&D.data.default_tool_identifier===\"wineforge-proton\"?"));
        assert(strstr(transformed, "y.push({data:T.name,label:T.display_name})"));
        assert(strstr(transformed, "selectedOption:D.data.selected_tool_identifier,onChange:T=>I(T.data)"));
        free(transformed);
        char *context = strstr(source, "function g(A){const{label:b,nAppId:f}=A,");
        assert(context);
        context[9] = 'X';
        transformed = np_webpatch_transform((const uint8_t *)source, length, &transformed_length, &shape);
        assert(!transformed && shape && !strcmp(shape, NP_SHAPE_SELECTTOOL));
        free(source);
        puts("selecttool inherited-label patch: PASS; context drift refused");
        return 0;
    }
    assert(try_serve("/private/tmp/Integration/enabled", O_RDONLY) == -1);
    assert(try_serve("/private/tmp/Integration/settings-bridge.json", O_RDONLY) == -1);
    assert(try_serve("/mock/steamui/chunk.js", O_WRONLY) == -1);
    assert(try_serve("/mock/steamui/chunk.js", O_RDONLY | O_CREAT) == -1);
    assert(enabled_reads == 0);
    assert(try_serve("/mock/steamui/chunk.js", O_RDONLY) == -1);
    assert(enabled_reads == 1);
    assert(!np_webpatch_should_patch("/mock/steamui/localization/foo.js"));
    assert(!np_webpatch_should_patch("/mock/other/chunk.js"));
    puts("webpatch guard: PASS; own metadata does not reenter enabled guard; writes untouched");
    return 0;
}
