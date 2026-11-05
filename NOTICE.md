# Sources and licenses

This standalone MT7902 bootstrap port is derived from hbyonly1's
[itlmt7922-wifi](https://github.com/hbyonly1/itlmt7922-wifi) at commit
`20aaac5c8e1d97d737a73f1a684ded2ec1d0f940`. Its source and original contributor
notices are retained under the Clear BSD license in `LICENSE`. The foundation
itself derives from thanhnndev and contributors' macOS MediaTek driver. The exact
upstream notices and README are preserved in `docs/UPSTREAM-NOTICE.md` and
`docs/UPSTREAM-README.md`; their MT7922 hardware results do not apply to MT7902.

The MT7902 hardware reference and firmware source are
[morrownr/mt76](https://github.com/morrownr/mt76) at commit
`c539593ef5649ac855e0995f2f3492c42f4f32eb`. In particular, `mt7921/pci.c` and
`mt792x_dma.c` specify command ring 15, a shared 512-entry RX event ring without
the MCU-WA ring, and the MT7902 prefetch map. The original Linux source copyright
and license notices remain applicable; see `licenses/mt76-LICENSE.txt`.

The two unmodified MT7902 images in `fw/` are separately licensed by MediaTek in
`fw/LICENCE.mediatek`. Their SHA-256 pins are recorded in `fw/SHA256SUMS`. The
inherited BSD driver build embeds these images with assembly `.incbin`; they are
not re-licensed as GPL firmware. Release packages carry their license and hashes.

MacKernelSDK is pinned to `3f750085caa17ec3a7880f11c11bf4f48cd6a164` and is
obtained separately. This standalone MediaTek tree is forked from hbyonly1's
project and adapted from our MT7902 0.2.11 snapshot, commit
`fc396ba` (the complete hash is recorded in the build manifest).

The `userspace/HeliPort` submodule is an app-only fork of OpenIntelWireless/HeliPort.
Its UI and supporting code retain their BSD-3-Clause LICENSE and notices. That is
separate from the driver license; using the HeliPort UI does not include the Intel
itlwm kernel driver. Binary bundles include both projects' notices.

Keep the source copyright notices, this file, LICENSE, and the firmware and
third-party license files with redistributed source and binaries.

## OpenIntelWireless net80211 integration branch

The external/itlwm submodule pins official OpenIntelWireless/itlwm commit
53c51c2cdd6e4b69beb91f310d74c53422b0f8bd. Its macOS port is GPL-2.0-or-later
with retained OpenBSD/NetBSD/other file notices. The foundation build compiles
that source into the 0.3.0 net80211 runtime kext. The older standalone build
remains separate and does not compile the new stack.
See external/itlwm/LICENSE and the original notices in each source file.
The linked runtime derivative retains those source/license obligations and
includes the exact driver/upstream source archives and staged modifications in
its package. Intel hardware backends are excluded from the binary.
