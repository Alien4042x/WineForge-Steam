#ifndef WINEFORGE_NATIVE_STEAM_STATE_H
#define WINEFORGE_NATIVE_STEAM_STATE_H
/* WineForge-Internal: native-steam/client-readiness-contract-v1. */
int wf_integration_enabled(void);
int wf_client_allowed(const char *path);
void wf_note_hooks(int installed, int expected);
void wf_note_tool_registered(void);
void wf_session_start(void);
#endif
