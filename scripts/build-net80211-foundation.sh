#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Compile unmodified OpenIntelWireless net80211; no radio loading.
set -euo pipefail
cd "$(dirname "$0")/.."
source_root=external/itlwm
source_pin=53c51c2cdd6e4b69beb91f310d74c53422b0f8bd
[[ "$(git -C "$source_root" rev-parse HEAD)" == "$source_pin" ]]
[[ -z "$(git -C "$source_root" status --porcelain --untracked-files=no)" ]]
kernel_sdk="${MACKERNELSDK:-build/MacKernelSDK}"
[[ -d "$kernel_sdk/Headers" ]]
output=build/net80211-foundation
mkdir -p "$output/obj"
flags=(-arch x86_64 -mmacosx-version-min=12.0 -O2 -g
       -mkernel -fapple-kext -fno-builtin -fno-stack-protector -nostdlib -nostdlibinc
       -ffreestanding -fno-exceptions -fno-rtti -std=gnu++17
       -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
       -DIEEE80211_STA_ONLY
       -I"$source_root/itl80211/openbsd" -I"$source_root/itl80211" -I"$source_root/include"
       -iquote "$source_root/itl80211/linux"
       -I"$kernel_sdk/Headers" -include "$source_root/itlwm/PrivateSPI.pch"
       "-ffile-prefix-map=$PWD=." "-fdebug-prefix-map=$PWD=.")
objects=()
for source in "$source_root"/itl80211/openbsd/net80211/*.c; do
    object="$output/obj/$(basename "${source%.c}").o"
    xcrun clang++ "${flags[@]}" -x c++ -c "$source" -o "$object"
    objects+=("$object")
done
xcrun libtool -static -o "$output/libitlwm-net80211.a" "${objects[@]}"
xcrun lipo "$output/libitlwm-net80211.a" -verify_arch x86_64
xcrun nm -gU "$output/libitlwm-net80211.a" > "$output/defined-symbols.txt"
python3 - <<'PY'
from pathlib import Path
import hashlib,json,subprocess
root=Path('build/net80211-foundation')
symbols=(root/'defined-symbols.txt').read_text()
required=['ieee80211_newstate','ieee80211_input','ieee80211_encap',
          'ieee80211_eapol_key_input','ieee80211_ifattach','ieee80211_ioctl']
for name in required:
    assert name in symbols, name
archive=root/'libitlwm-net80211.a'
manifest={'upstream':'OpenIntelWireless/itlwm',
          'upstream_commit':subprocess.check_output(['git','-C','external/itlwm','rev-parse','HEAD']).decode().strip(),
          'architecture':'x86_64','archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
          'net80211_units':len(list((root/'obj').glob('*.o'))),
          'intel_hardware_backend_included':False,'mt7902_runtime_integrated':False,
          'installable_kext':False,'verified_entry_points':required}
(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
PY
