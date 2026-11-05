# MT7902 0.2.15 candidate

## Connection changes

Complete the pre-association QoS correction: CE SET_EDCA_PARMS previously still
sent qos=1 and applied beacon WMM parameters, although BSS/STA QoS had been
turned off in 0.2.14. It now sends qos=0 and non-QoS DCF defaults before
association. After successful WMM negotiation, use the actual association
response's WMM Parameter element rather than a potentially different beacon.

Keep the current upstream-style PORT_SECURE initial peer as the first attempt.
Only an authentication response timeout enables one CONNECT-on-add retry, based
on the separately identified MT7902 community variant. Abort the old channel
request, require successful station removal, create a fresh peer, acquire a new
ROC, then retry the same authentication frames. Fixed rate, antenna, firmware,
channel and power limits are unchanged. AP rejection, cancellation, firmware
fault and successful initial authentication do not trigger this retry.

Both trials stay on the existing hardware worker. No app ABI or boot argument
is added. Data remains blocked until association/key installation; outstanding
payloads remain owned until TXFREE or a quiesced reset. Stop/replacement-join/
disconnect commands interrupt management wait loops and prevent the extra trial.

## Diagnostics and limits

The original timed-out trial is saved separately in wifi-attempt-history.
The follow-up is labeled wifi-auth-profile=retry-connect-on-add; the first is
baseline-port-secure. wifi-edca-setup records QoS, parameter source and command
transport result. wifi-peer-setup includes the actual connection state.

This is a source-backed EDCA correction plus a bounded compatibility hypothesis,
not a hardware-confirmed Ramos fix. The latest readable capture remains 0.2.13;
the user reports 0.2.14 still fails. OPENCORE reads failed during this work, so
no new capture could be independently inspected. ROC lifetime, MAC timing and
management completion visibility remain open investigations if both trials fail.

## Companion app

HeliPort 2.1.4 preserves the original UI and fixes saved-network security NONE
metadata. Existing credentials stay in Keychain. Auto-join selects the current
joinable scan entry. This app bug is independent of on-air authentication.

## Verification and test

Host fixtures check independent STA BASIC/EDCA wire layouts, pre-auth non-QoS
parameters, negotiated WMM AC ordering, malformed/duplicate AC records, truncated
inputs and no partial output on failure. Existing sanitizer and release checks
and the Intel macOS build must pass before distributing the candidate.

Replace the existing kext with this bundle and reboot once. Replace only the
app in /Applications to install its correction; an app-only test needs no reboot.
Try each Ramos band and a hotspot in that same boot, then collect driver logs.
Keep both auth-profile entries in the capture if the router still fails.

## Primary sources

- [Linux non-QoS default contention and QoS lifecycle](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/net/mac80211/util.c)
- [Linux CE EDCA wire layout](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt7921/mcu.c)
- [Community CONNECT-on-add variant](https://github.com/ArseniySV/mt7902-fix/blob/4eb6598587cde8585e91c4e494ab810f254bd1bf/0003-fix-conn-state.patch)
