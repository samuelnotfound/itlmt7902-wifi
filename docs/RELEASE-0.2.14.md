# 0.2.14: defer peer QoS until association

The supplied 0.2.13 capture confirms working hotspot WPA2/DHCP/traffic, while
both Ramos bands time out during initial authentication without observed raw
TXS/TXFREE reports. The user confirms that Ramos also fails as the first
connection after boot. The exact radio failure is not established.

This candidate corrects a separate lifecycle mismatch identified in the
[comparative driver audit](COMPARATIVE-AUTH-AUDIT.md): 0.2.13 sets STA BASIC,
WTBL GENERIC and BSS QBSS QoS from the requested HT plan before authentication.
Linux mac80211 derives the peer WMM flag from the association response before
moving the peer to the associated state.

The candidate starts every attempt with peer QoS disabled. BSS QBSS and both
peer QoS flags remain zero throughout authentication/association requests. A
bounded association-response decoder reads status, AID and the AP's WMM
Parameter IE. Successful HT association enables peer QoS only when WMM was
accepted; otherwise the existing teardown-and-retry ladder tries the legacy
profile. Legacy hotspot operation keeps QoS disabled. Attempt start, teardown
and firmware recovery clear the negotiated flag.

Fixed OFDM/CCK rates, initial connection state, HT20 capabilities, antenna
selection, firmware, power ceiling and command ABI are unchanged. This makes
the pre-authentication QoS correction independently reviewable. It is a
candidate router compatibility fix, not proof of working Ramos connectivity.

Diagnostics now include:

- `wifi-peer-setup`: emitted STA state, connection state, QoS, HT and rate masks.
- `wifi-bss-setup`: emitted BSS connected/QoS flags and requested HT plan.
- `wifi-roc-grant`: actual returned channel, band, width, center channels,
  request type and interval, alongside the requested channel. Incomplete grant
  details are explicitly marked unavailable; diagnostics add no new grant gate.
- `wifi-channel-setup`: current-attempt channel command/result. The one-time
  register readout is separately named `wifi-channel-register-snapshot`.

The new BSS/STA/grant diagnostics are retained in the bounded attempt history,
without keys, password fields or frame bodies. ROC lifetime policy is not
changed by this candidate; observed grants will inform that separate issue.

Host regression fixtures cover valid WMM negotiation, legacy/no-WMM responses,
AP rejection, truncated/malformed/unknown vendor elements, AID masking and
adversarial IE bytes. Independent Connac2 ROC fixtures cover both bands,
different returned lifetimes, rejected grants, wrong namespaces/tokens/BSS and
all truncated lengths. CI runs these with ASan/UBSan, plus the existing suite,
Intel kext compilation, embedded firmware hashes and release packaging checks.

Install the candidate `itlmt7902.kext` in the existing OpenCore entry and reboot
once to load it. Keep the stable HeliPort 2.1.3 application and existing startup
settings. Try both Ramos networks and a hotspot in that boot; capture the
driver registry after attempts without another reboot. A host/build pass does
not establish hardware connectivity.

Source comparison:

- [mac80211 association success sets peer WMM](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/net/mac80211/mlme.c#L7169)
- [mt76 copies peer QoS to station records](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt76_connac_mcu.c#L478)
- [Connac2 ROC grant structure](https://github.com/morrownr/mt76/blob/c539593ef5649ac855e0995f2f3492c42f4f32eb/mt7921/mt7921.h#L75)
