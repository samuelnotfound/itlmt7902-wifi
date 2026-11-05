# OpenIntelWireless net80211 foundation

Source: [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm)
Pin: `53c51c2cdd6e4b69beb91f310d74c53422b0f8bd` (official master inspected for this port).
Unmodified source and its GPL/BSD notices are in `external/itlwm`.

This branch starts a stack migration. Its build compiles upstream net80211 state,
input/output, node selection, security and EAPOL units against the Intel macOS
kernel SDK. It checks the archive's MAC entry points. The archive is not yet wired
into the active radio driver; this target produces no installable new-stack kext.
The current 0.2.16 package still uses the existing connection engine.

| Layer | Owner |
| --- | --- |
| Menus and Keychain | HeliPort-MT7902 |
| Ethernet controller and control interface | itlwm-compatible frontend |
| Scan cache, selection, AUTH/ASSOC/RUN, WPA2/EAPOL, roaming | upstream net80211 |
| PCI, firmware, DMA, MCU, TXWI/RXD and TXFREE | MT7902 backend |
| Channel/ROC, BSS/STA/WTBL/EDCA and key install/remove | MT7902 callbacks |

Intel IWM/IWX/IWN hardware backends are excluded from the foundation archive.

Integration requirements:

1. Connect the Ethernet frontend to `ieee80211com`/`ifnet`. Populate real NIC
   channels/rates rather than Intel defaults.
2. Feed validated raw RX to `ieee80211_input` with actual RSSI/channel/crypto
   metadata. Define hardware CCMP stripping. Never run both net80211 and the old
   m3 handshake/decapsulation on the same frame.
3. Drain management/data queues through MT7902 descriptors. Each accepted
   mbuf/node has one owner until TXFREE or quiesced teardown. Ring consumption
   cannot release payload or prove transmission over the air.
4. Implement `ic_newstate` through asynchronous firmware commands carrying a join
   generation. Ignore stale replies. Never wait for the worker while holding the
   stack gate. AUTH needs a matching channel grant and initialized peer; ASSOC
   programs negotiated state; RUN alone does not authorize a WPA2 controlled port.
5. Implement key install/remove and EDCA from negotiated stack state. Publish link
   only when association and controlled-port state allow traffic. Open links have
   no RSN handshake; keep WPA2 plaintext rejection intact.
6. Stop the old m3 connection/security engine before enabling net80211. Validate
   cancellation, timeout, rekey, disconnect, link loss and DMA recovery with
   event sequences before the combined hardware session.

The stack provides a stronger MAC/security foundation. It cannot by itself fix
the missing Ramos authentication response, which precedes password validation and
may still depend on the MT7902 radio/MCU backend. Retain the hotspot capture as a
regression reference.

Build-only: `MACKERNELSDK=/path/to/MacKernelSDK bash scripts/build-net80211-foundation.sh`.
This target's artifacts are evidence, not files to install in OpenCore.
