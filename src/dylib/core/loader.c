// Dylib entry point into steamclient.dylib
#include "../version.h"
#include "../util/log.h"
#include "../util/file.h"
#include "../resolver/resolver.h"
#include "../resolver/sigdb.h"
#include "../core/macho.h"
#include "../hooks/hooks.h"
#include "../feats/compat.h"
#include "../feats/wineforge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <dirent.h>
#include <mach-o/dyld.h>
#include <libgen.h>

#define STEAMCLIENT_DYLIB "steamclient.dylib"

// The bootstrapper can take a while before the real client dylib appears...
#define WAIT_TIMEOUT_MS 60000

/* WineForge-Internal: native-steam/pinned-client-profile-v1. */
static void sigdb_path_for(char *buf, size_t buf_size) {
    const char *home = np_home_dir();
    if (!home) { buf[0] = 0; return; }
    np_support_path(buf, buf_size, home, "client-profile.json");
}

static pthread_t g_install;
static int       g_install_started;
static int       g_active;

static void *install_thread(void *unused) {
    (void)unused;

    NP_LOG("install_thread: waiting for %s", STEAMCLIENT_DYLIB);

    const struct mach_header_64 *mh = NULL;
    intptr_t slide = 0;

    char client_path[1024];
    if (np_await_image(STEAMCLIENT_DYLIB, WAIT_TIMEOUT_MS, &mh, &slide,
                             client_path, sizeof(client_path)) != 0) {
        // The Steam bootstraper does this
        NP_LOG("install_thread: %s absent after %d ms, hooks will install in the "
               "relaunched client", STEAMCLIENT_DYLIB, WAIT_TIMEOUT_MS);
        return NULL;
    }

    /* WineForge-Internal: native-steam/exact-client-hash-gate-v1. */
    if (!wf_client_allowed(client_path)) return NULL;

    uintptr_t text_base = 0;
    size_t text_size = 0;

    if (np_find_segment(mh, slide, "__TEXT", &text_base, &text_size) != 0) {
        NP_ERR("install_thread: no __TEXT segment in %s", STEAMCLIENT_DYLIB);
        return NULL;
    }

    NP_LOG("install_thread: %s __TEXT @ 0x%lx (%zu bytes) slide=0x%lx",
           STEAMCLIENT_DYLIB, (unsigned long)text_base, text_size,
           (unsigned long)slide);

    np_sigdb_t sigdb = {0};

    char sig_path[512];
    sigdb_path_for(sig_path, sizeof(sig_path));

    if (np_load_profile(sig_path, &sigdb) != 0) {
        NP_ERR("install_thread: cannot load signature database from %s", sig_path);
        return NULL;
    }

    np_resolve_result_t resolved = {0};

    int resolved_count = np_resolve_signatures(mh, slide, &sigdb, STEAMCLIENT_DYLIB,
                                               &resolved);
    int required = np_required_for_module(&sigdb, STEAMCLIENT_DYLIB);
    NP_LOG("install_thread: signatures %d/%d resolved", resolved_count, required);

    // Safety feature :)
    if (!required || resolved_count < required) {
        NP_ERR("install_thread: %d/%d signatures resolved, installing nothing; "
               "the signature database is out of date for this Steam build",
               resolved_count, required);
        np_free_resolution(&resolved);
        np_free_profile(&sigdb);
        return NULL;
    }

    /* WineForge-Internal: native-steam/validate-layout-before-hooks-v1. */
    if (!np_hooks_preflight(&resolved)) {
        np_free_resolution(&resolved);
        np_free_profile(&sigdb);
        return NULL;
    }
    np_hooks_launch_install(mh, slide);

    int total = 0;
    int installed = np_hooks_install_all(mh, slide, &resolved, &total);

    NP_LOG("install_thread: ready, %d/%d hooks installed", installed, total);

    if (installed == 0)
        NP_WARN("install_thread: no hooks installed, check the signature database");
    else if (installed < total)
        NP_WARN("install_thread: only %d of %d hooks installed, compat support is "
                "incomplete for this client build", installed, total);

    np_free_resolution(&resolved);

    wf_note_hooks(installed, total);

    np_free_profile(&sigdb);

    return NULL;
}

static void np_log_platform_overrides(void) {
    static const char *const vars[] = {
        "SteamOverridePlatform",
        "SteamPlatform",
        "STEAM_FORCE_OS",
        "STEAMOS",
        "SteamOS",
        "SteamOverrideOSType",
        "STEAM_COMPAT_FORCE_PLATFORM",
    };

    int found = 0;
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        const char *val = getenv(vars[i]);
        if (val) {
            NP_LOG("np_init: platform override present %s=%s", vars[i], val);
            found = 1;
        }
    }
    if (!found)
        NP_LOG("np_init: no platform override env set, client reports as macOS");
}

__attribute__((constructor))
static void np_init(void) {
    const char *pn = getprogname();
    if (!pn || strcmp(pn, "steam_osx") != 0 || !wf_integration_enabled())
        return;

    np_log_init();
    g_active = 1;

    wf_session_start();

    NP_LOG("np_init: loaded into %s (pid %d) version %s",
           pn, getpid(), NOTPROTON_VERSION);

    np_log_platform_overrides();
    np_compat_export_tools_path();
    // Steam scans compatibilitytools.d at startup, so the tool directories have to exist
    // before that occurs.
    np_compat_ensure_tool_manifest();
    np_hooks_spawn_install();

    if (pthread_create(&g_install, NULL, install_thread, NULL) == 0)
        g_install_started = 1;
    else
        NP_ERR("np_init: cannot create loader thread");
}

__attribute__((destructor))
static void np_fini(void) {
    if (!g_active)
        return;

    if (g_install_started) {
        np_await_stop();
        pthread_join(g_install, NULL);
    }

    NP_LOG("np_fini: unloading, hooks left in place for the OS to reclaim");
}
