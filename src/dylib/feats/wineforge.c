/* WineForge-Internal: native-steam/client-readiness-contract-v1. */
#include "wineforge.h"
#include "../util/file.h"
#include "../util/log.h"
#include <CommonCrypto/CommonDigest.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static int hooks_installed, hooks_expected, tool_registered;
static char client_hash[65];

static int read_line(const char *name, char *out, size_t capacity) {
    char path[1024];
    const char *home = np_home_dir();
    if (!home) return 0;
    np_support_path(path, sizeof(path), home, name);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(out, (int)capacity, f) != NULL;
    if (ok) {
        size_t n = strlen(out);
        if (n && out[n - 1] == '\n') out[--n] = 0;
        else if (!feof(f)) ok = 0;
        if (n && out[n - 1] == '\r') out[--n] = 0;
        if (fgetc(f) != EOF) ok = 0;
    }
    fclose(f);
    return ok;
}

int wf_integration_enabled(void) {
    char enabled[8];
    return read_line("enabled", enabled, sizeof(enabled)) && !strcmp(enabled, "1");
}

static int hex64(const char *s) {
    if (strlen(s) != 64) return 0;
    for (; *s; s++) if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) return 0;
    return 1;
}

int wf_client_allowed(const char *path) {
    char expected[80], actual[65];
    if (!path || !wf_integration_enabled() || !read_line("client.sha256", expected, sizeof(expected)) || !hex64(expected)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    CC_SHA256_CTX ctx;
    unsigned char buffer[65536], digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Init(&ctx);
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f))) CC_SHA256_Update(&ctx, buffer, (CC_LONG)n);
    int ok = !ferror(f);
    fclose(f);
    CC_SHA256_Final(digest, &ctx);
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) snprintf(actual + i * 2, 3, "%02x", digest[i]);
    if (!ok || strcmp(expected, actual)) {
        NP_ERR("WineForge: unsupported or changed Steam client; installing no client hooks");
        return 0;
    }
    pthread_mutex_lock(&state_lock);
    memcpy(client_hash, actual, sizeof(client_hash));
    pthread_mutex_unlock(&state_lock);
    return 1;
}

static void write_session_locked(void) {
    char id[80], path[1024], temporary[1100];
    const char *home = np_home_dir();
    if (!home || !read_line("installation-id", id, sizeof(id)) || !hex64(id)) return;
    np_support_path(path, sizeof(path), home, "session.json");
    snprintf(temporary, sizeof(temporary), "%s.%d.tmp", path, getpid());
    FILE *f = fopen(temporary, "w");
    if (!f) return;
    fprintf(f, "{\"protocolVersion\":1,\"installationID\":\"%s\",\"pid\":%d,\"observedAt\":%lld,\"clientSha256\":\"%s\",\"hooksInstalled\":%d,\"hooksExpected\":%d,\"toolRegistered\":%s,\"ready\":%s}\n",
            id, getpid(), (long long)time(NULL), client_hash, hooks_installed, hooks_expected,
            tool_registered ? "true" : "false",
            wf_integration_enabled() && hooks_expected > 0 && hooks_installed == hooks_expected && tool_registered ? "true" : "false");
    if (fclose(f) || rename(temporary, path)) unlink(temporary);
}

void wf_session_start(void) {
    pthread_mutex_lock(&state_lock);
    write_session_locked();
    pthread_mutex_unlock(&state_lock);
}
void wf_note_hooks(int installed, int expected) {
    pthread_mutex_lock(&state_lock);
    hooks_installed = installed;
    hooks_expected = expected;
    write_session_locked();
    pthread_mutex_unlock(&state_lock);
}
void wf_note_tool_registered(void) {
    pthread_mutex_lock(&state_lock);
    tool_registered = 1;
    write_session_locked();
    pthread_mutex_unlock(&state_lock);
}
