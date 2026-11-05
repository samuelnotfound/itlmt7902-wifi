# OpenIntelWireless net80211 foundation

Source: [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm)
Pin: `53c51c2cdd6e4b69beb91f310d74c53422b0f8bd` (official master inspected for this port).
Unmodified source and its GPL/BSD notices are in `external/itlwm`.

This branch now contains a concrete gated net80211 bridge, MT7902 firmware
executor, strict RX/CCMP normalizer, hardware key encoder and session-completion
guards. The build compiles them with the actual upstream stack, compatibility,
timers, crypto and MT7902 backend; a relocatable link rejects unresolved stack
and backend methods. Residual kernel imports are recorded, not assumed verified.

The runtime coordinator still needs to bind the Ethernet instance, hardware
worker, bounded mailboxes and completions, and replace the old m3 supervisor.
These new components are not activated by the current 0.2.16 driver. This target
produces build evidence rather than an installable kext.

Implemented contracts:

- Stack calls, timers and Ethernet link notifications use one checked gate;
  firmware commands execute only on the captured hardware worker.
- Hardware state/key snapshots contain copied values, never net80211 pointers.
  Epochs and ticket checks reject old completions. Quiescence is explicit.
- RX reconstructs the CCMP IV from real chip PN/key-ID metadata; net80211 retains
  replay verification. Split A-MSDU, missing metadata and unsupported ciphers fail
  closed. No HT/QoS/aggregation capabilities are advertised in this first bridge.
- Only MIC/RSN-validated SECURE intent plus completed hardware association/keys
  can authorize a protected port. A staged, pinned PAE notification preserves
  asynchronous-key intent without editing the upstream submodule.
- A mandatory Ethernet controller wrapper prevents the upstream stack from
  publishing a transient link-up before the guarded controlled port is ready.
- Key removal is WCID-wide. The executor compares key identity so deleting an
  old GTK cannot clear a newer GTK installed on the shared broadcast WCID.

No older Apple networking family is injected. The Ethernet path uses the target
macOS IONetworkingFamily; OpenBSD net80211 is compiled source within the driver.

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
