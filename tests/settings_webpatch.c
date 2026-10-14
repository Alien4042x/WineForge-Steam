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
