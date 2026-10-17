# WineForge native Steam game settings protocol 1

Developer reference for the interface between the native Steam settings panel
and the WineForge host. Players configure games through the panel. The host
implements the settings service and stores their profiles.

This document specifies protocol behavior. Live Steam connection, game launches
and graphics require separate validation.

## Ownership and transport

The WineForge game database is the only stored game-profile authority. Steam
launch options remain user command text, not a second settings database. Both
launch origins build their plan from the same AppID/profile resolver. Updating
one setting must rebuild backend libraries, GPU identity, DLL overrides and
other dependent plan fields. Do not forward a single legacy environment value.

The app owns an authenticated loopback settings service, independent of the
Tauri window lifetime. Before opening Steam it publishes `settings-bridge.json`
inside the existing Integration root, as an atomic regular file owned by the
user with mode 0600:

```json
{"protocolVersion":1,"installationID":"<64 lowercase hex>","baseURL":"http://127.0.0.1:<port>/v1","token":"<64 lowercase hex>"}
```

The native hook embeds a validated descriptor into its in-memory UI rewrite.
It never starts an unversioned app executable or guesses a server address. The
app must start/restart its headless service for both app-opened and independently
started Steam, retain the advertised address for the integration session, and
withdraw the descriptor on disable. Every request must also verify integration
enabled/ownership and the installation nonce. Bind IPv4 loopback only. Reject
foreign Origin, non-JSON input, oversized bodies, invalid AppIDs and unknown
fields. CORS permits only the observed, supported Steam UI origin and required
methods/headers. CSP/CORS in actual Steam are an untested connected gate.

Requests use `Authorization: Bearer <token>`, JSON and no cookies. No network
access occurs merely by loading the settings script. The panel refuses writes
while a GET/PATCH is outstanding and has a bounded request timeout.

`GET /v1/games/<AppID>/settings` returns:

```json
{
  "protocolVersion":1,
  "installationID":"<same nonce>",
  "appID":"730",
  "revision":"<64 lowercase hex>",
  "overrides":{"backend":"inherit","upscaling":"inherit","wfusync":null,"metalHud":null,"dxmtSpatial":"inherit"},
  "effective":{"backend":"d3dmetal","upscaling":"default","wfusync":true,"metalHud":false,"dxmtSpatial":"off"}
}
```

`PATCH` to the same path adds `If-Match: <revision>` and body
`{protocolVersion:1, appID:"730", changes:{<changed fields only>}}`.
Validate and mutate under the existing database lock. The revision must cover
the profile and relevant inherited defaults, so a concurrent WineForge edit
or changed bottle default produces HTTP 409 without a write. Success returns
the updated GET shape. The panel refreshes on conflict and never retries a
stale write. Tauri game-setting edits use this same mutation/validation path.
GET does not create a bottle, install Steam, or change a runtime.

## Fields and precedence

| Field | Override values | Meaning |
| --- | --- | --- |
| backend | inherit, d3dmetal, dxmt | Automatic uses the existing per-game/global Steam Play/bottle resolver. New default D3DMetal |
| upscaling | inherit, default, amd, nvidia | Panel offers WineForge inheritance, AMD FSR and NVIDIA MetalFX. Existing default overrides remain explicit neutral data |
| wfusync | null, true, false | null uses the existing resolver, including preserved explicit advanced `WINEWFUSYNC=0`. New environments default on |
| metalHud | null, true, false | null uses the existing resolver. Explicit off emits `MTL_HUD_ENABLED=0` |
| dxmtSpatial | inherit, off, 1.5, 1.72, 2.0, 3.0 (strings) | Independent of NVIDIA profile. Saved while D3DMetal is selected, emitted only for DXMT |

Game override precedes inherited defaults. Keep current advanced-env inheritance
for untouched fields. An explicit panel on/off change normalizes conflicting
per-game environment entries for that same managed field before setting the
dedicated value, so an old game `WINEWFUSYNC=0` does not silently negate a new
On selection. `null` removes only the dedicated override, retaining advanced
inheritance. Do not silently migrate unrelated game variables.

The panel has one visible default, `Use WineForge default` (`inherit`). The
previous explicit neutral `default` is accepted for saved profiles and displayed
through a disabled hidden compatibility option. Loading the panel does not
convert or write it. Selecting WineForge inheritance explicitly clears that
override through the existing versioned PATCH.

Profile generation owns its GPU identity/NV extension/DLL routing fields.
Unrelated advanced tuning such as D3DM_MAX_FPS and other DXMT_CONFIG entries
remains allowed. Strip legacy/inherited panel-owned backend/profile values
before resolution, then derive all these fields from the canonical selection:

- D3DMetal AMD: `D3DMETAL_UPSCALER_PROFILE=amd`.
- D3DMetal NVIDIA: profile=nvidia, vendor=4318, device=10370, description
  NVIDIA GeForce RTX 4080, `D3DM_ENABLE_METALFX=1`, managed builtin
  nvapi/nvapi64/nvngx overrides in addition to the backend overrides.
- DXMT NVIDIA: `DXMT_ENABLE_NVEXT=1` and managed builtin nvapi64/nvngx.
- Default: no stale AMD/NVIDIA identity or NV extension fields/overrides.
- AMD on effective DXMT has no implementation in the current app. Reject a new
  incompatible selection with HTTP 422. Do not advertise a working AMD DXMT
  profile or silently choose another backend.
- DXMT spatial On sets `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` and exactly one
  `d3d11.metalSpatialUpscaleFactor=<factor>` entry. Off explicitly emits 0 and
  removes only that entry. Preserve every other DXMT_CONFIG entry. D3DMetal
  must not inherit an active DXMT spatial setting. Changing backend/profile
  does not erase the separately stored spatial preference.

AVX2, Retina and raw-controller switches are not retained in the new panel:
the current shared runner has no canonical settings implementation for them.
Their existing launch-option text is preserved. Do not reinterpret these keys
as functioning WineForge settings.

## Optional import of old launch options

This applies only when a player chooses to import existing Steam Launch Options
into a WineForge game profile. New setups use the panel directly. No automatic
sweep of Steam launch options takes place.

The panel previews literal assignments before one standalone `%command%`.
For example, `CX_GRAPHICS_BACKEND=dxmt %command%` can import the DXMT backend.
`WINEMSYNC` is an old MSync setting. It is preserved and never converted to
WFUSync. Supported `WINEWFUSYNC=0/1` values import the WFUSync setting.
The exact supported inputs and conservative rewrites are recorded in the shared
[migration fixtures](tests/settings-migration-v1.json). Complex commands,
unknown values, ambiguous combinations and custom spatial factors are preserved.

`POST /v1/games/<AppID>/settings/migrate` uses the same auth/If-Match and body:
`{protocolVersion:1,appID,launchOptions:<exact original string>}`. The app uses
the same conservative parser, merges only fields the user has not explicitly
configured, refuses conflicting imports, and stores a SHA-256 migration receipt
atomically with the canonical profile. It returns the GET shape plus
`cleanup:{before:<exact input>,after:<conservative rewrite>}`. It is idempotent
for that AppID/input, including after a failed Steam cleanup. Unsupported or
ambiguous options are never removed.

Only after that successful database commit may the panel call
SteamClient.Apps.SetAppLaunchOptions, and only if the current details still equal
cleanup.before and cleanup.after exactly matches the panel's own parser. A
failed or concurrent cleanup keeps the saved profile and reports pending
cleanup. It never reapplies old values over newer WineForge settings. Both
launch origins ignore migrated legacy settings as authority and must use the
saved canonical profile. Read-only preview never changes Steam options.

## Host integration and validation

The companion WineForge host contains the settings service, GET/PATCH/migrate
handlers, revision checks, migration receipts and shared launch-plan resolver.
They are maintained in the launcher project, outside this native source tree.

Offline validation covers both launch origins, profile DLL sets, spatial
isolation, explicit zero, conflicts and cleanup retries. Preserve explicit
Default separately from inheritance. Packaging must retain exact payload hashes
and installer ownership/readiness checks. Offline results do not establish live
Steam UI behavior, successful game launches or anti-cheat compatibility.
