# itlmt7902-wifi — standalone Ethernet driver + HeliPort UI

This repository is a fork of [hbyonly1/itlmt7922-wifi](https://github.com/hbyonly1/itlmt7922-wifi),
adapted for MediaTek **MT7902 PCIe 14c3:7902** on Intel x86_64 macOS Tahoe.
It contains the standalone `itlmt7902.kext` and links a modified **HeliPort** UI
through the [userspace/HeliPort submodule](userspace/HeliPort).

The driver exposes an Ethernet interface managed through HeliPort's menu-bar UI.

## Baseline and validation

Driver **0.2.15** builds on the MT7902 0.2.11 snapshot, commit `fc396ba`, which
provided Internet through Redmi A3 and iPhone 12 mini hotspots. Router joins on
that baseline timed out. This version corrects empty basic-rate configuration,
matches association rates to the AP, and adds constrained HT20 negotiation for
OFDM-only peers. A supplied 0.2.13 capture confirms hotspot authentication, WPA2,
DHCP and traffic; both Ramos router bands still time out during authentication.
Version 0.2.15 completes pre-association EDCA/QoS setup and adds a bounded
CONNECT-on-add retry after an authentication timeout. Both trial profiles are
recorded in one boot. Its router behavior needs hardware validation.
See [the release notes](docs/RELEASE-0.2.15.md) and
[the comparative driver audit](docs/COMPARATIVE-AUTH-AUDIT.md).

Driver ancestry and firmware: hbyonly1's MT7922 port, thanhnndev and contributors'
MediaTek driver, and the pinned BSD-3-Clause-Clear mt76 references recorded in
[NOTICE.md](NOTICE.md). HeliPort's UI is from OpenIntelWireless under its own
retained BSD-3-Clause license. No Intel itlwm kernel driver sources are used.

## Install

If your existing standalone `itlmt7902.kext` already connects to a hotspot, **keep
that installed driver** and try only the new app first. ABI v1 is unchanged, so
replacing the app does not require a reboot.

1. Download **HeliPort-MT7902.zip** from a successful Actions build. Extract it and
   copy **HeliPort-MT7902.app** to `/Applications`.
2. Quit other Wi-Fi control apps and disable their login items.
   Run only HeliPort-MT7902, choose the hotspot/network and enter its password.
   Password saving uses HeliPort's keychain management. Its modern/legacy UI
   choices remain in Preferences. The adapter has a separate bundle ID, so it
   does not replace stock HeliPort's settings or keychain namespace.

For a fresh driver installation, back up your EFI and add `itlmt7902.kext` under
OpenCore `Kernel → Add` with BundlePath `itlmt7902.kext`, ExecutablePath
`Contents/MacOS/itlmt7902`, PlistPath `Contents/Info.plist`, Enabled true. Put the
bundle in `EFI/OC/Kexts`. This baseline requires **`mt7902_stage=3`** for connection
mode; without that boot argument, it defaults to passive scan-only stage 2.
Append the argument to your existing `boot-args` under OpenCore
`NVRAM → Add → 7C436110-AB2A-4BBB-A880-FE41995C9F82` and reboot once.
Do not place
passwords in Info.plist. Firmware is embedded; no separate firmware directory is
required on macOS. Keep the old EFI as a recovery option.

The combined **itlmt7902-HeliPort-0.2.15.zip** has the driver, app, notices and log
collector. The app-only ZIP is preferable when you already have the working driver.
For a driver update with HeliPort already installed, use the separate
**itlmt7902.kext-0.2.15.zip**; it includes the kext, release notes and licenses.

HeliPort **2.1.4** restores the original modern/legacy HeliPort menu, including on
Tahoe. It bounds menu polling and joins to prevent overlapping work from repeated
clicks, keeps password windows alive until closed, and hardens menu/string access.
Actions uses the macOS 15 Intel runner with Xcode 16.4.
It also fixes saved-network security metadata and auto-join selecting stale
"None" entries; credentials remain in Keychain. See the app's 2.1.4 release notes.
Keep an installed 0.2.13 driver when trying this app update; no reboot is needed.
The log collector includes app logs and up to three existing HeliPort crash reports.

## Source layout and GitHub linked folders

- `src`, `codec`, `fw`: standalone MediaTek driver and embedded firmware.
- `scripts`, `tests`, `docs`: build checks, protocol tests and driver history.
- `userspace/HeliPort`: Git submodule pointing to the modified UI repository at
  an exact commit. GitHub displays it as a linked folder with an arrow.
- `app`: the older standalone menu app retained as a source/reference fallback;
  the main Ethernet bundle uses HeliPort-MT7902.

```sh
git clone --recurse-submodules https://github.com/samuelnotfound/itlmt7902-wifi.git
# For an existing clone:
git submodule update --init --recursive
```

Updating the UI is explicit: update the app fork, then commit its new submodule
pointer here. It does not silently follow a moving app branch.

## Limits and diagnostics

This checkpoint supports visible WPA2-PSK/CCMP networks with optional PMF and
non-DFS channels 1–11, 36/40/44/48, 149/153/157/161/165. One-stream HT20 is tried
for OFDM-only HT-capable peers; CCK-capable 2.4 GHz peers retain the legacy path.
The 10 dBm test ceiling is retained. Group rekey, sleep/wake and router
interoperability remain unfinished. The app's power switch disconnects and stops
its joins; it does not physically turn off the radio. Driver data not supplied
through its ABI (BSSID/noise/MCS/PHY) is shown as unknown.

Run `bash collect-logs.sh` from the bundle after a failed attempt. Driver notes are
in `docs/LEGACY-STANDALONE-README.md` and the numbered release notes.

## Build

GitHub Actions runs driver host tests, verifies the pinned firmware, builds the
standalone x86_64 kext and the pinned HeliPort UI, and packages only those outputs.
Local macOS builds use `bash scripts/build.sh` and
`bash userspace/HeliPort/scripts/build-macos.sh`. Original driver licenses and
HeliPort's independent UI license/notices accompany the binaries.
