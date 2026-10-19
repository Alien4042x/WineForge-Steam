#!/usr/bin/env python3
"""WineForge-Internal: native-steam/standalone-candidate-payload-v1.
Export a candidate without installing Steam or asserting a connected build PASS.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys

root = Path(__file__).resolve().parent.parent
output = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else root / 'payload'
if len(sys.argv) > 2:
    raise SystemExit('Usage: package-payload.py [new-output-directory]')
if output.exists():
    raise SystemExit('Output already exists. Choose a new directory to preserve it.')
sha = lambda file: hashlib.sha256(file.read_bytes()).hexdigest()
provenance = json.loads((root / 'provenance.json').read_text())
for relative, expected in provenance['sourceTreeSHA256'].items():
    if sha(root / relative) != expected:
        raise SystemExit('Source differs from the reviewed reference: ' + relative)
client = '625715ada315323351ada75a077142c15d4b742be2db9e63ec354828082ab194'
profile = root / 'profiles' / client / 'client-profile.json'
evidence = json.loads((root / 'docs/validation/client-625715ad.json').read_text())
if sha(profile) != evidence['packagedProfileSha256']:
    raise SystemExit('Client profile differs from the passive-validated reference.')
files = {
    'client-profile.json': profile,
    'run': root / 'compat_run.sh',
    'wineforge-native-steam.dylib': root / 'build/wineforge-native-steam.dylib',
    'wineforge-overlay-shim.dylib': root / 'build/wineforge-overlay-shim.dylib',
    'wineforge-client-check': root / 'build/wineforge-client-check',
    'SETTINGS_CONTRACT_V1.md': root / 'SETTINGS_CONTRACT_V1.md',
    'settings-migration-v1.json': root / 'tests/settings-migration-v1.json',
    'Licenses/NotProton/LICENSE': root / 'LICENSE',
    'Licenses/NotProton/NOTICE': root / 'licenses/NotProton-NOTICE',
    'Licenses/Dobby/LICENSE': root / 'licenses/Dobby-LICENSE',
    'Licenses/cJSON/LICENSE': root / 'licenses/cJSON-LICENSE',
}
metadata = {}
for relative, file in files.items():
    if not file.is_file() or file.is_symlink():
        raise SystemExit('Missing ordinary build input: ' + str(file.relative_to(root)))
    metadata[relative] = {'sha256': sha(file), 'size': file.stat().st_size}
manifest = {
    'protocolVersion': 1, 'toolID': 'wineforge-proton',
    'entry': 'run', 'hook': 'wineforge-native-steam.dylib',
    'minimumMacOS': '15.0', 'x86_64Slice': 'inert',
    'files': metadata,
    'clientCompatibility': {
        'mode': 'anchor-layout-v1', 'checker': 'wineforge-client-check', 'protocolVersion': 1,
    },
    'supportedClient': {
        'sha256': client, 'profile': 'client-profile.json', 'architecture': 'arm64',
        'signaturesExpected': 16, 'signaturesResolved': 16,
        'layoutValidated': True, 'hooksExpected': 9,
    },
    'features': {
        'overlayProtocolVersion': 1, 'settingsProtocolVersion': 1,
        'requiresAppOwnedSettingsService': True,
        'legacyMigration': 'explicit conservative per-AppID import',
    },
    'validation': {
        'passiveSupported': True,
        'evidencePath': 'docs/validation/client-625715ad.json',
        'connectedClientTested': False, 'realSteamModified': False,
        'currentClientHookConnectedTested': False, 'overlayGameTested': False,
        'gameLaunchReady': False,
        'scope': 'Retained passive profile evidence. Newly built binaries require fresh connected validation.',
    },
    'source': {
        'notprotonCommit': provenance['notproton']['commit'],
        'dobbyCommit': provenance['dobby']['commit'],
        'provenanceSHA256': sha(root / 'provenance.json'),
        'overlay': {
            'notprotonCommit': provenance['notproton']['commit'],
            'source': 'src/overlay/overlay_shim.m',
            'sourceSha256': sha(root / 'src/overlay/overlay_shim.m'),
            'pipelineOwnershipFix': provenance['overlayPipelineOwnershipFix'],
            'adaptations': ['Steam Play process/backend gate', 'idempotent late installation'],
            'build': 'make overlay, universal arm64/x86_64, macOS 15.0',
        },
    },
}
output.mkdir(parents=True)
for relative, file in files.items():
    target = output / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(file, target)
for relative in ('run', 'wineforge-native-steam.dylib', 'wineforge-overlay-shim.dylib', 'wineforge-client-check'):
    (output / relative).chmod(0o755)
(output / 'native-client-payload.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Candidate payload exported. Steam and installed application unchanged.')
