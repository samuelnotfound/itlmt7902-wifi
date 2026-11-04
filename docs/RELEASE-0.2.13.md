# 0.2.13: basic-rate fallback and constrained router negotiation

The supplied 0.2.11 capture has diagnostic stage 3, joins enabled and a successful
CCK-capable hotspot connection in the same boot: auth/assoc replies, valid WPA2
MIC, PTK/GTK installation, DHCP and traffic. The OFDM-only router on channel 1
then timed out on all three authentication exchanges. Each queued three frames
but observed no raw TXS/TXFREE completion or matching authentication response.
This is before the password handshake. Descriptor consumption alone does not
establish radio transmission. A router rejection was not observed.

The target's parsed rates were OFDM supported `ff`, basic `00`, CCK `0/0`.
The driver selected OFDM6 for TXWI but sent an empty basic-rate bitmap in the
firmware STA PHY record. Linux mac80211's `ieee80211_set_sta_rate_info()` uses the
lowest supported rate when the AP sets no legacy basic-rate flags. This version
applies that fallback before station setup, preserving already valid basic sets.
It never invents a rate absent from the AP. The OFDM-only fixture now produces
basic OFDM bit 0, which maps to band-relative STA PHY bitmap `0010` on 2.4 GHz
and `0001` on 5 GHz. The fixed TX rate stays OFDM6.

Association Supported/Extended Supported Rates now use the AP/driver intersection
instead of a fixed 2.4 GHz list that advertised unsupported CCK to OFDM-only APs.
The captured CCK-capable hotspot's association rate bytes remain unchanged.
Malformed IEs or no usable rates produce a distinct rates error, not an auth
timeout. `wifi-rate-profile` records the effective set and whether fallback was
used; it is retained in the bounded per-attempt history.

The earlier 0.2.12 constrained HT20 profile is also ported into this standalone
tree for OFDM-only HT-capable peers: one stream, 20 MHz, long GI, peer-supported
MCS0..7, consistent WMM/BSS/STA flags. Wide/VHT/HE, LDPC, STBC and SGI are not
advertised. CCK-capable 2.4 GHz peers retain the legacy path. The 10 dBm test
ceiling and non-DFS/WPA2-PSK/CCMP restrictions remain. Failed HT association or
handshake can fall back to legacy; authentication failure is not mislabeled as
an HT association failure.

This corrects source-level compatibility gaps. It does not prove that an empty
basic-rate bitmap caused the missing completions, nor that HT20 fixes an earlier
authentication timeout. Router association, WPA2, DHCP and traffic with this
changed driver require hardware validation. Retain 0.2.11 as the hotspot baseline.

The host suite tests all 4095 nonempty legacy rate masks, the captured rate
profiles, lowest-bitrate selection, 5 GHz CCK rejection, malformed input, output
bounds, round trips and unchanged hotspot bytes. HT20 encoder fixtures exercise
all MCS masks and truncated output. GitHub Actions runs the suite with sanitizers,
builds the x86_64 kext and pinned HeliPort UI, and verifies firmware and packaging.

Install only the new `itlmt7902.kext` if HeliPort 2.0.2 is already installed.
Keep `mt7902_stage=3` and reboot once to load the changed kernel driver. Try a
hotspot and each router band in that same boot, then collect logs without another
reboot. No new boot arguments or router configuration changes are required.

Source comparison:

- [Linux mac80211 basic-rate fallback and supported-rate intersection](https://linux.googlesource.com/linux/kernel/git/torvalds/linux/+/441c63ff42c4e666304cdd32d23b5fc6bc1ea3cc/net/mac80211/mlme.c)
- [Pinned mt76 station PHY/RA record layout](https://github.com/morrownr/mt76/blob/c539593ef5649ac855e0995f2f3492c42f4f32eb/mt76_connac_mcu.c)
