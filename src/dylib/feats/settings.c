/* WineForge-Internal: native-steam/settings-descriptor-v1. */
#include "settings.h"
#include "../util/file.h"
#include <cJSON.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int hex64(const char *s) {
    if (!s || strlen(s) != 64) return 0;
    for (; *s; s++) if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) return 0;
    return 1;
}

static int loopback_url(const char *s) {
    static const char prefix[] = "http://127.0.0.1:";
    if (!s || strncmp(s, prefix, sizeof(prefix) - 1)) return 0;
    s += sizeof(prefix) - 1;
    if (*s < '1' || *s > '9') return 0;
    unsigned port = 0, digits = 0;
    while (*s >= '0' && *s <= '9') {
        if (++digits > 5) return 0;
        port = port * 10 + (unsigned)(*s++ - '0');
    }
    return port <= 65535 && !strcmp(s, "/v1");
}

static int read_owned(const char *rel, char *out, size_t cap, int secret) {
    char path[1024]; struct stat st;
    const char *home = np_home_dir();
    if (!home) return 0;
    np_support_path(path, sizeof(path), home, rel);
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid()
        && st.st_size > 0 && (size_t)st.st_size < cap
        && (!secret || (st.st_mode & 0777) == 0600);
    size_t got = 0;
    while (ok && got < (size_t)st.st_size) {
        ssize_t n = read(fd, out + got, (size_t)st.st_size - got);
        if (n <= 0) { ok = 0; break; }
        got += (size_t)n;
    }
    close(fd);
    if (!ok || memchr(out, 0, got)) return 0;
    out[got] = 0;
    return 1;
}

char *wf_settings_bridge_json(void) {
    char data[2048], id[80];
    if (!read_owned("settings-bridge.json", data, sizeof(data), 1)
        || !read_owned("installation-id", id, sizeof(id), 0)) return NULL;
    /* cJSON stores strings as C strings; reject escaped embedded NUL instead
     * of allowing an apparently valid prefix to hide an unknown field/value. */
    if (strstr(data, "\\u0000")) return NULL;
    size_t len = strlen(id);
    if (len && id[len - 1] == '\n') id[--len] = 0;
    if (!hex64(id)) return NULL;
    cJSON *json = cJSON_ParseWithOpts(data, NULL, 1);
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); return NULL; }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "protocolVersion");
    const cJSON *nonce = cJSON_GetObjectItemCaseSensitive(json, "installationID");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(json, "baseURL");
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(json, "token");
    int fields = 0, valid = 1;
    for (const cJSON *p = json->child; p; p = p->next) {
        fields++;
        if (!p->string || (strcmp(p->string, "protocolVersion") && strcmp(p->string, "installationID")
            && strcmp(p->string, "baseURL") && strcmp(p->string, "token"))) valid = 0;
        for (const cJSON *q = p->next; q; q = q->next)
            if (q->string && p->string && !strcmp(p->string, q->string)) valid = 0;
    }
    valid = valid && fields == 4 && cJSON_IsNumber(version) && version->valuedouble == 1
        && cJSON_IsString(nonce) && hex64(nonce->valuestring) && !strcmp(id, nonce->valuestring)
        && cJSON_IsString(token) && hex64(token->valuestring)
        && cJSON_IsString(url) && loopback_url(url->valuestring);
    char *result = valid ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    return result;
}
