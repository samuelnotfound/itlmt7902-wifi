# Continuing MT7902 development on macOS

Updated 2026-11-04. This is a technical project handoff, not a chat export.

## Goal and scope

Develop the standalone `itlmt7902.kext` Ethernet interface and the adapted HeliPort
menu-bar UI for MediaTek MT7902 PCIe `14c3:7902` on Intel x86_64 macOS Tahoe.
Work only in the user's forks. Do not create pull requests. Keep required source,
UI and firmware attribution. Avoid unrelated project history in the README.

## Get the current source

```sh
git clone --recurse-submodules https://github.com/samuelnotfound/itlmt7902-wifi.git
cd itlmt7902-wifi
git switch codex/mt7902-ethernet
git submodule update --init --recursive
```

The app lives in `userspace/HeliPort`, a pinned Git submodule from
https://github.com/samuelnotfound/HeliPort-MT7902. Its development branch is
`codex/mt7902-ethernet-ui`. Commit and push app changes there first, then update
the submodule pointer in the driver repository.

## Current validated source and builds

- Driver version: **0.2.11**. `src/` and `codec/` are unchanged from the earlier
  standalone snapshot `fc396baebec051f853ddee79de13f0b34138dd33`.
- The driver baseline was reported to provide Internet through Android and iPhone
  hotspots. Router interoperability remains unresolved. Do not claim all networks
  work or that a UI change fixes the radio/association path.
- Latest app version: **HeliPort-MT7902 2.0.2**, commit
  `4ca3ef0fc55868c2b6d6647a4183a9d6257a408d`.
- Driver repository commit pinning that app:
  `109181e4558fd38cea4639a2b3a04e3eba6da725`.
- App build and sanitizer-backed regression tests passed:
  https://github.com/samuelnotfound/HeliPort-MT7902/actions/runs/41826584562
- Driver host tests, macOS compilation, packaging, embedded firmware checks and
  combined app build passed:
  https://github.com/samuelnotfound/itlmt7902-wifi/actions/runs/41826699029
- App-only artifact: `HeliPort-MT7902`; combined artifact:
  `MT7902-Ethernet-HeliPort`. The Actions artifacts contain inner ZIPs.
- Hardware validation of the HeliPort adapter is still pending.

## Current setup issue to resolve first

The installed app reported that connections were not enabled. The user confirmed
they had not set a stage boot argument. The current kext defaults to **stage 2,
passive scanning only**. It requires **`mt7902_stage=3`** to enable joins.

Append that argument to the existing OpenCore `boot-args` under
`NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82`, retaining other arguments.
Back up the EFI before editing. Reboot once to apply the driver startup setting.
Do not request repeated reboots for app-only changes. Confirm the active settings
and driver state on macOS:

```sh
nvram boot-args
ioreg -r -c itlmt7902 -l -w0
```

Expected after initialization: `diagnostic-stage = 3`, `join-supported = Yes`.
If stage 3 is selected but joins remain unavailable, inspect the initialization
results and `wifi-error`; do not assume the stage setting is the only cause.

## App behavior and the latest fix

Install `HeliPort-MT7902.app` in `/Applications`; the inherited launch-path check
requires that location. Quit other Wi-Fi control apps before launching it.
Replacing only this app does not require replacing the kext or rebooting.

The 2.0.1 adapter requested scans only in `idle`. Stage 2 stays `scan-ready`, and
failed stage-3 joins can stay `failed`, so newly enabled hotspots could be missing
from an unrefreshed cache. Version 2.0.2 allows refresh requests in all three idle
states. It avoids requests during `connecting`, `scanning` or `connected`, retains
cached results on Busy, and relies on the driver's existing 10-second throttle.
Regression fixtures exercise these policy decisions. Keep the menu open while
the scan finishes. This fixes a refresh defect, not every possible missing AP.

The app uses the existing driver ABI v1, with selectors 0 scan, 1 join and 2
disconnect. The join structure is **108 bytes**: uint32 ABI, SSID[36], PSK[68].
The Ethernet BSD interface may not exist until the first successful join; the
adapter must not treat that as a missing driver. Secrets must never be logged,
put in shell commands or persisted in a kext plist.

App transport and scan policy: `userspace/HeliPort/ClientKit/MT7902.c` and
`MT7902Codec.h`. Swift integration: `HeliPort/NetworkManager.swift`.
Kernel setup/readiness: `src/itlmt7902.cpp`, `itlmt7902_m1.cpp`.
Kernel scan/join command mailbox and status: `src/itlmt7902_uc.cpp`.

## Next hardware test

1. Verify stage 3 and loaded driver version before attempting a connection.
2. Use app 2.0.2 to rescan and try a previously working hotspot. Capture driver
   state if the hotspot is absent or the join fails.
3. Then try the failing router once and collect the same-boot logs using
   `scripts/collect-logs.sh`. Inspect `wifi-attempt-history`, `wifi-error`,
   `wifi-events`, `m3-res` and authentication/association diagnostics to locate the
   failure. Do not call it a bad password unless handshake evidence supports that.
4. Compare router and hotspot attempts before changing radio or security code.
   The exact router failure is not established by the scan-only popup.

Log files may contain private network information. Keep them local unless the
user explicitly asks to share them; never commit credentials or machine identity.

## Builds

The existing GitHub Actions workflow uses `macos-15-intel`. On a Mac with Xcode,
the app can be built and tested with:

```sh
bash userspace/HeliPort/scripts/build-macos.sh
```

For the driver, use `scripts/build.sh` with the pinned MacKernelSDK as documented
in `.github/workflows/ethernet.yml`. Firmware is embedded and licensed separately;
no additional firmware folder is required on the target macOS installation.
