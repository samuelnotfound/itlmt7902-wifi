# Comparative authentication audit — 2026-11-05

This review compares connection behavior across related drivers, rather than
assuming the fixed OFDM rate explains the Ramos failures. It changes no runtime
code and does not declare a radio fix. The current installed driver is 0.2.13;
the user reports HeliPort 2.1.3 is stable.

## Evidence and limits

The supplied `Ramos-driver.txt` contains a successful Redmi A3 authentication,
association, WPA2 exchange, DHCP and traffic in the same boot as four completed
Ramos failures. Both Ramos bands queue three authentication requests per attempt
without raw TXS/TXFREE reports or a matching authentication response. The 2.4 GHz
basic-rate fallback is present, so that correction alone did not solve the issue.
The user separately confirms that connecting to Ramos first after boot also
fails. Do not ask for that test again or make a previous hotspot connection a
prerequisite of the diagnosis.

Missing host completion reports do not independently prove that the radio never
transmitted. Beacon reception does not prove that addressed authentication
responses would pass every firmware/hardware receive filter. The exact defect
remains unconfirmed.

## Sources inspected

| Source | Revision | Value and limitation |
| --- | --- | --- |
| Linux mt76 plus mac80211 | torvalds/linux `05441d697f7f9a927264f4058cbbc6a03dfcac9d` | Includes the station lifecycle that a standalone kext must implement itself. |
| Original Linux reference | morrownr/mt76 `c539593ef5649ac855e0995f2f3492c42f4f32eb` | Current port's pinned register/firmware reference; not an independent hardware result. |
| Alternative MT7902 backport | abdullaabdullazade/mt7902_driver `f1b728bf56703feb91503e66870590476fef2f99`, `mt7902/src/` | Another mt76-derived implementation; shares much code with upstream. |
| Vendor-style MT7902 driver | same repository/revision, `gen4-mt7902/` | Different management state machine and TX submission machinery; command/state numbers cannot be transplanted without checking their ABI. |
| macOS MT7922 port | hbyonly1/itlmt7922-wifi `20aaac5c8e1d97d737a73f1a684ded2ec1d0f940` | Direct ancestor of this port, tested by its maintainer on MT7922; different chip, firmware and antenna configuration. |
| MtkWlan macOS source | local WTL checkout `ac11695f1190821f8a292884344feca22c71c38b` | Useful architecture comparison; MT7925/Connac3 path is not a drop-in Connac2 implementation. Native work remains deferred. |
| Community connection-state patch | ArseniySV/mt7902-fix `4eb6598587cde8585e91c4e494ab810f254bd1bf` | A competing hypothesis, not an upstream-established fix. |

Files were read as source material. No install scripts or code from these
repositories were executed.

## 1. Pre-authentication QoS is a separate variable from the transmit rate

**Confirmed source difference.** In Linux `net/mac80211/mlme.c`,
`ieee80211_prep_connection()` allocates/inserts the peer for authentication.
`ieee80211_assoc_success()` assigns `sta->sta.wme` from the association response
before moving the peer to the associated state. mt76 copies this value into both
STA BASIC and WTBL GENERIC QoS fields.

Our `m3FastPlan()` enables its HT20 plan for both parsed Ramos profiles and
disables it for the CCK-capable hotspot. Before authentication, `m3Sta(0,true,0)`
already writes `fastActive` into STA BASIC `b[9]` and WTBL GENERIC `g[13]`.
`m3Bss(false)` also sets QBSS from `fastActive`. Thus the two observed paths
differ in pre-authentication QoS/BSS configuration as well as in their fixed rate.
Saying that OFDM itself is the cause was too strong.

There is a later negotiation gap too: `m3Assoc()` accepts status/AID but does not
derive negotiated peer QoS from the association response. That cannot explain
the current missing initial auth response, but it can affect interoperability
after authentication starts working.

**First candidate correction:** keep pre-authentication peer QoS off, then enable
it only after successful association and supported WMM negotiation. Keep the
same OFDM6 rate, channel, antenna mask, firmware and power limit for the first
comparison. This is a source-backed lifecycle correction; its effect on Ramos
is a hardware hypothesis, not a demonstrated fix.

## 2. Connection state: a real alternative, but conflicting references

Our STA BASIC connection state is PORT_SECURE (2) from the initial record.
STA_REC_STATE separately carries NONE (0), then ASSOC (2). These fields represent
different concepts and must not be conflated with the vendor driver's state
numbers 1/2/3.

The community patch changes newly added peers to CONNECT (1), then PORT_SECURE
on later updates. However, both the inspected current Linux source and the
alternative MT7902 backport still use PORT_SECURE whenever the station is
enabled. MtkWlan also deliberately follows that convention. The patch therefore
does not prove that our initial state is wrong, and should not be applied
globally as an established fix.

**Candidate experiment:** compare CONNECT versus PORT_SECURE for the initial
record, isolated from QoS/rate changes, if lifecycle cleanup alone does not help.
Keep host data blocked until key installation in either case. Record the emitted
connection state, STA_REC_STATE, QoS and WTBL fields with each attempt.

## 3. Management submission and completion need their own diagnosis

Linux mt76 builds a CT TXWI/TXP, holds the payload token until TXFREE and uses
management/off-channel queue classification. The vendor driver explicitly marks
authentication MMPDUs FORCE_TX and uses TX-done callbacks to advance from send to
response-wait states; its descriptor builder can select ALTX for that control
flag. Our CT authentication descriptor already selects ALTX0 (16), so merely
copying that queue number would change nothing.

Our descriptors select fixed OFDM6 `0x004b` for Ramos. Linux's mode/index encoding
and the vendor Connac2 descriptor-mode/SPE-selection macros agree with the fields
we use. No incorrect fixed-rate number was found. In contrast, MtkWlan's MT7925
path uses a different rate index and different TXWI bit positions. Replacing
`0x004b` with its OFDM6 index 18 would mix generations.

The MT7922 ancestor uses duplicate antenna SPE index 24. MT7902 reports one
stream/chain; our index 0 matches the single-chain reference mapping. Do not
copy a dual-chain setting merely because that other chip connects.

**Needed improvement:** per-management-token submission, DMA-fetch, TXS and
TXFREE timestamps, including late reports after the exchange ends. Current
counters show no completions inside the exchange but cannot trace each frame
through later cleanup. Never release/reuse a payload just because a timer or
DMA-consumer index advanced. The inspected WTL path has timeout-based token
reclamation; it is not a pattern to copy into this port.

## 4. Channel grant validation is incomplete

The port verifies ROC event namespace, BSS, token and status, but does not retain
the returned primary channel, band, width or maximum interval. Linux consumes
the returned interval to manage its ROC timer. A successful command transport or
status is less evidence than the current log wording implies.

The port requests one 5000 ms JOIN ROC. Its permitted handshake waits can reach
six seconds for M1 plus another six for M3, after auth/association time. Linux's
managed-TX lifecycle reacquires/holds the join ROC through authorization.
This lifetime mismatch is a later connection risk, not proof of the present
authentication failure at about 1.5 seconds.

**Correction/instrumentation:** parse and record the actual grant fields, check
them against the target, track expiration and make channel ownership cover the
state machine's actual budget. Do not assume the requested interval was granted.

There is also a confirmed diagnostic defect: `wifi-channel-setup` is captured
once per boot. A channel-1 snapshot remains attached to subsequent channel-44
attempts. Split the immutable register snapshot from current-attempt channel
setup instead of interpreting the old channel string as a failed switch.

## 5. MAC timing differs across the references

Linux `mt7921_set_channel()` calls `mt792x_mac_set_timeing()` after channel switch;
ERP slot changes also call it. The macOS MT7922 ancestor leaves that setup to
firmware, as our current port does. These are different policies; success on the
MT7922 is not evidence that firmware covers every MT7902 condition.

Earlier 0.2.8 timing writes/readback checks blocked setup and were removed.
The timing registers later read as zero through both the fixed PCI window and
validated MCU register replies, with a nonzero CF-End value. That observation
does not settle register semantics or prove a missing timing write causes Ramos
failure. Do not reintroduce a guessed blocking readback test or replace all
register access with an unvalidated remap path.

**Candidate investigation:** validate the applicable MT7902 timing path and its
read/write behavior separately. This remains open alongside station/queue setup.

## 6. Firmware, power, DMA and receive filtering remain separate checks

The observed firmware is MT7902-specific and successfully handles the hotspot.
That establishes substantial working functionality, not universal correctness
of CLC/SAR/SKU limits, both radio bands, receive filtering or PCIe DMA handling.
The alternative drivers contain additional power/ACPI fixes; their applicability
must be checked against this port's own parsing and the supplied SAR values,
rather than disabling limits or raising power to see what happens.

The port uses contiguous, sub-4-GiB physical DMA addresses and barriers; Linux
uses its DMA mapping infrastructure and WTL has explicit DMA buffer helpers.
A CPU-readable TX buffer is not independent proof of device-visible payload.
The hotspot success argues against a universal DMA failure but does not prove
every ownership/visibility path. No evidence here justifies changing OpenCore's
IOMMU settings or the driver's cache policy as a first fix.

Host RX parsing rejects no decoded authentication candidates in the failed
exchanges, but packets discarded by hardware/firmware never reach that parser.
Compare the applied RX-filter/OMAC/BSSID state as well as TX state; do not label
this a transmit-only defect merely because no host authentication response exists.

## Upstream reports constrain the conclusion

[mt76 issue 1115](https://github.com/openwrt/mt76/issues/1115) reports MT7902
authentication timeouts with the same firmware build `20251212032127`, but with
2.4 GHz failing and 5 GHz working. It is not the same failure profile as ours.
The discussion includes independent reproduction and a transmit-power experiment
that did not remove that user's authentication problem. It supplies alternatives
to investigate, not a diagnosis or patch for this machine.

[mt76 issue 1130](https://github.com/openwrt/mt76/issues/1130) reports an MT7921
5 GHz authentication timeout. That is another example of the symptom's limited
specificity, not evidence that our different chip has the same defect.

## Implementation order

1. Isolate pre-authentication QoS/negotiation from rate selection. This is the
   clearest demonstrated lifecycle difference in the two current profiles.
2. Capture actual ROC grants, current-attempt BSS/STA settings and per-token
   completion timing in the same candidate build. These are observations, not
   silent changes of antenna/power/rate policy.
3. If needed, expose controlled initial connection-state and legacy/HT profile
   choices so comparisons can happen within one loaded driver. Run MCU changes
   on the existing worker and keep the stable HeliPort ABI intact.
4. Investigate MAC timing and firmware management submission as independent
   branches if the first comparison still produces no completions.

No additional reboot or repetition of the already-confirmed first-connection
test is needed for this source analysis. This review produced no new binary.

Primary source links:

- [Linux mac80211 station preparation and association](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/net/mac80211/mlme.c)
- [Linux mt76 station/BSS records](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt76_connac_mcu.c)
- [Linux fixed-rate descriptors](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt76_connac_mac.c)
- [Linux MT7921/MT7902 managed-TX/channel lifecycle](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt7921/main.c)
- [Linux MAC timing](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt792x_mac.c)
- [Alternative MT7902 backport station records](https://github.com/abdullaabdullazade/mt7902_driver/blob/f1b728bf56703feb91503e66870590476fef2f99/mt7902/src/mt76_connac_mcu.c)
- [Vendor authentication submission](https://github.com/abdullaabdullazade/mt7902_driver/blob/f1b728bf56703feb91503e66870590476fef2f99/gen4-mt7902/mgmt/auth.c)
- [Vendor authentication TX-done state machine](https://github.com/abdullaabdullazade/mt7902_driver/blob/f1b728bf56703feb91503e66870590476fef2f99/gen4-mt7902/mgmt/saa_fsm.c)
- [Vendor Connac2 TX descriptor handling](https://github.com/abdullaabdullazade/mt7902_driver/blob/f1b728bf56703feb91503e66870590476fef2f99/gen4-mt7902/nic/nic_txd_v2.c)
- [Community CONNECT-state patch](https://github.com/ArseniySV/mt7902-fix/blob/4eb6598587cde8585e91c4e494ab810f254bd1bf/0003-fix-conn-state.patch)
- [MT7922 macOS ancestor](https://github.com/hbyonly1/itlmt7922-wifi/tree/20aaac5c8e1d97d737a73f1a684ded2ec1d0f940)
