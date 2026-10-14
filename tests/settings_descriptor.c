/* Offline tests of app-owned metadata. No Steam/Wine dylib or process. */
#include "../src/dylib/feats/settings.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[1024], descriptor[1100];
static int cases;
static void write_text(const char *path, const char *value, mode_t mode) {
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs(value, f) >= 0); assert(!fclose(f)); assert(!chmod(path, mode));
}
static void expect(const char *json, mode_t mode, int valid) {
    write_text(descriptor, json, mode);
    char *result = wf_settings_bridge_json();
    assert((result != NULL) == valid);
    if (result) { assert(strstr(result, "http://127.0.0.1:31415/v1")); free(result); }
    cases++;
}
int main(void) {
    strcpy(directory, "/private/tmp/wf-settings-descriptor-XXXXXX"); assert(mkdtemp(directory));
    assert(!setenv("WINEFORGE_STEAM_INTEGRATION_ROOT", directory, 1));
    snprintf(descriptor, sizeof(descriptor), "%s/settings-bridge.json", directory);
    char id_path[1100]; snprintf(id_path, sizeof(id_path), "%s/installation-id", directory);
    char id[66]; memset(id, 'a', 64); id[64] = '\n'; id[65] = 0; write_text(id_path, id, 0644);
    char token[65]; memset(token, 'b', 64); token[64] = 0;
    const char *fmt = "{\"protocolVersion\":%d,\"installationID\":\"%s\",\"baseURL\":\"%s\",\"token\":\"%s\"%s}";
    id[64] = 0; char json[2200];
    snprintf(json, sizeof(json), fmt, 1, id, "http://127.0.0.1:31415/v1", token, "");
    expect(json, 0600, 1); expect(json, 0644, 0); expect(json, 0660, 0);
    const char *urls[] = { "http://example.com/v1", "http://127.0.0.1:65536/v1", "http://127.0.0.1:0/v1", "http://127.0.0.1:31415/v1/evil", "http://127.0.0.1:031415/v1", "http://127.0.0.1:31415/v1?token=x" };
    for (size_t i = 0; i < sizeof(urls)/sizeof(*urls); i++) {
        snprintf(json, sizeof(json), fmt, 1, id, urls[i], token, ""); expect(json, 0600, 0);
    }
    snprintf(json, sizeof(json), fmt, 2, id, "http://127.0.0.1:31415/v1", token, ""); expect(json, 0600, 0);
    snprintf(json, sizeof(json), fmt, 1, id, "http://127.0.0.1:31415/v1", token, ",\"protocolVersion\":1"); expect(json, 0600, 0);
    snprintf(json, sizeof(json), fmt, 1, id, "http://127.0.0.1:31415/v1", token, ",\"extra\":true"); expect(json, 0600, 0);
    snprintf(json, sizeof(json), fmt, 1, id, "http://127.0.0.1:31415/v1", "short", ""); expect(json, 0600, 0);
    snprintf(json, sizeof(json), fmt, 1, id, "http://127.0.0.1:31415/v1", token, "");
    char wrong[66]; memset(wrong, 'c', 64); wrong[64] = '\n'; wrong[65] = 0; write_text(id_path, wrong, 0644); expect(json, 0600, 0);
    id[64] = '\n'; id[65] = 0; write_text(id_path, id, 0644); id[64] = 0;
    char target[1100]; snprintf(target, sizeof(target), "%s/private-target", directory); write_text(target, json, 0600);
    assert(!unlink(descriptor)); assert(!symlink(target, descriptor)); assert(!wf_settings_bridge_json()); cases++;
    assert(!unlink(descriptor));
    char oversized[2100]; memset(oversized, ' ', sizeof(oversized)-1); oversized[sizeof(oversized)-1]=0; expect(oversized, 0600, 0);
    expect("{\"protocolVersion\\u0000hidden\":1}", 0600, 0);
    snprintf(json, sizeof(json), fmt, 1, id, "http://127.0.0.1:31415/v1", token, ""); strcat(json, " trailing-data"); expect(json, 0600, 0);
    assert(!unlink(descriptor)); assert(!wf_settings_bridge_json()); cases++;
    assert(!unlink(target)); assert(!unlink(id_path)); assert(!rmdir(directory));
    printf("settings descriptor: %d cases PASS; fixture removed; no Steam/Wine execution\n", cases);
    return 0;
}
