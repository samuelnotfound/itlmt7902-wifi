#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Compile/link net80211, its MT7902 bridge and the real hardware backend.
# This target never emits an installable kext or enables a radio runtime path.
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
python3 -B foundation/stage_net80211_pae.py --source "$source_root" --output "$output/staged"
# The archive is reconstructed from this exact source list on each invocation;
# stale objects in the directory are never picked up by a glob or a linker.
sources=("$source_root"/itl80211/openbsd/net80211/*.c
         "$source_root/itl80211/openbsd/net80211/CTimeout.cpp"
         "$source_root/itl80211/compat.cpp"
         "$source_root/itl80211/openbsd/sys/_ifq.cpp"
         "$source_root/itl80211/openbsd/sys/_mbuf.cpp"
         "$source_root/itl80211/openbsd/sys/_task.cpp"
         "$source_root/itl80211/openbsd/sys/arp.c"
         foundation/support/CTimeoutMeta.cpp)
# Shared crypto units selected by the official itlwm Xcode target. Firmware
# decompression and all Intel IWM/IWX/IWN hardware sources stay excluded.
for name in poly1305 sha2 arc4 hmac cast aes ecb_enc md5 michael sha1 idgen \
            rmd160 cmac chachapoly key_wrap rijndael gmac ecb3_enc set_key blf sha1-pbkdf2; do
    sources+=("$source_root/itl80211/openbsd/crypto/$name.c")
done
flags=(-arch x86_64 -mmacosx-version-min=12.0 -O2 -g
       -mkernel -fapple-kext -fno-builtin -fno-stack-protector -nostdlib -nostdlibinc
       -ffreestanding -fno-exceptions -fno-rtti -std=gnu++17 -Wno-register
       -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
       -DIEEE80211_STA_ONLY
       -I"$source_root/itl80211/openbsd" -I"$source_root/itl80211" -I"$source_root/include"
       -iquote "$source_root/itl80211/linux"
       -I"$kernel_sdk/Headers" -include "$source_root/itlwm/PrivateSPI.pch"
       -Ifoundation -Isrc -Icodec
       "-ffile-prefix-map=$PWD=." "-fdebug-prefix-map=$PWD=.")
# Keep the existing driver compiled with its own include order and language
# flags: forcing net80211's BSD compatibility headers into it would hide API or
# layout mismatches behind macros. Only the bridge/executor see both APIs.
driver_flags=(-arch x86_64 -mmacosx-version-min=12.0 -O2 -g -fno-omit-frame-pointer
       "-ffile-prefix-map=$PWD=." "-fdebug-prefix-map=$PWD=."
       "-ffile-prefix-map=$kernel_sdk=MacKernelSDK" "-fdebug-prefix-map=$kernel_sdk=MacKernelSDK"
       -mkernel -fno-builtin -fno-stack-protector -nostdlib -nostdlibinc
       -ffreestanding -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
       "-I$kernel_sdk/Headers" -Isrc -Icodec -Ifoundation)
objects=()
: > "$output/compiled-sources.txt"
for source in "${sources[@]}"; do
    if [[ "$source" == "$source_root/itl80211/openbsd/net80211/ieee80211_pae_input.c" ]]; then
        source="$output/staged/ieee80211_pae_input.c"
    fi
    stem="${source%.*}"
    object="$output/obj/${stem//\//__}.o"
    xcrun clang++ "${flags[@]}" -x c++ -c "$source" -o "$object"
    objects+=("$object")
    printf '%s\n' "$source" >> "$output/compiled-sources.txt"
done
# The framework boundary and firmware executor use actual upstream types and
# actual itlmt7902 member functions. Their references must resolve in the same
# relocatable link, not against compile-only stubs.
bridge_sources=(foundation/net80211_bridge.cpp
                foundation/mt7902_net80211_hardware.cpp)
for source in "${bridge_sources[@]}"; do
    [[ -f "$source" ]]
    stem="${source%.*}"
    object="$output/obj/${stem//\//__}.o"
    xcrun clang++ "${flags[@]}" -x c++ -c "$source" -o "$object"
    objects+=("$object")
    printf '%s\n' "$source" >> "$output/compiled-sources.txt"
done
driver_sources=(itlmt7902 itlmt7902_m1 itlmt7902_m2 itlmt7902_m3 itlmt7902_txrx
                itlmt7902_pwr itlmt7902_ctl itlmt7902_eth itlmt7902_ba itlmt7902_uc
                itl_wpa itl_caps itl_ba)
plain_sources=(foundation/mt7902_net80211_session.cpp
               foundation/mt7902_net80211_rx.cpp
               foundation/mt7902_net80211_key.cpp)
for name in "${driver_sources[@]}"; do plain_sources+=("src/$name.cpp"); done
plain_sources+=(codec/*.cpp)
for source in "${plain_sources[@]}"; do
    stem="${source%.*}"
    object="$output/obj/${stem//\//__}.o"
    xcrun clang++ "${driver_flags[@]}" -std=c++17 -fapple-kext -fno-exceptions -fno-rtti \
        -c "$source" -o "$object"
    objects+=("$object")
    printf '%s\n' "$source" >> "$output/compiled-sources.txt"
done
# The module descriptor has C linkage and firmware uses literal .incbin data;
# compile these exactly as the standalone kext build does, not as C++.
for name in kmod_info fw_blob; do
    source="src/$name.c"
    object="$output/obj/src__$name.o"
    xcrun clang "${driver_flags[@]}" -c "$source" -o "$object"
    objects+=("$object")
    printf '%s\n' "$source" >> "$output/compiled-sources.txt"
done
xcrun libtool -static -o "$output/libitlwm-net80211.a" "${objects[@]}"
xcrun lipo "$output/libitlwm-net80211.a" -verify_arch x86_64
# A static archive alone accepts arbitrary missing dependencies. A relocatable
# link resolves references between every supplied object before symbol auditing.
apple_sdk="$(xcrun --show-sdk-path)"
xcrun ld -r -arch x86_64 -o "$output/net80211-relocatable.o" "${objects[@]}" \
    "-L$kernel_sdk/Library/x86_64" "-L$apple_sdk/usr/lib" -lkmodc++ -lkmod -lcc_kext
xcrun nm -gjU "$output/net80211-relocatable.o" > "$output/defined-symbols.txt"
xcrun nm -ju "$output/net80211-relocatable.o" > "$output/undefined-symbols.txt"
python3 -B foundation/support/test_check_symbols.py
audit_flags=()
if [[ -n "${NET80211_KERNEL_EXPORT_CATALOG:-}" ]]; then
    audit_flags+=(--kernel-export-catalog "$NET80211_KERNEL_EXPORT_CATALOG")
fi
python3 -B foundation/support/check_symbols.py --output "$output" \
    --source-list "$output/compiled-sources.txt" \
    --frontend-hooks foundation/support/frontend-hooks.json "${audit_flags[@]}"
