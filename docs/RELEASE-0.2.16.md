# MT7902 0.2.16 candidate

Add explicitly open, password-free Wi-Fi support to the Ethernet/HeliPort path.
Previously the user client and app rejected empty keys; association always
advertised Privacy plus RSN, then waited for WPA2 EAPOL, transmitted protected
Ethernet frames and dropped plaintext DHCP/ARP. Removing a hotspot password could
not exercise a working open connection in that implementation.

ABI v1 now accepts either an empty passphrase or 8-63 bytes. Full beacon security
and supplied credentials must agree: an empty key is accepted only when Privacy
is clear and no RSN/WPA IE exists; protected SSIDs require supported WPA2/CCMP and
a key/PMK. Malformed/duplicate/truncated IEs, WEP, WPA1 and unsupported RSN/OWE
are rejected. Discovery validates the complete IE stream before any stored-IE
truncation; a captured join target too large to retain is refused.

Open association clears Privacy, omits RSN, skips PMK/4-way/key installation and
admits plaintext only after successful association and BSS/STA setup. Open TX has
no Protected flag; WPA2 TX remains protected. Open RX refuses protected frames;
WPA2 RX still refuses plaintext IP/ARP/DHCP and A-MSDU. Existing decoder validation
and interface/link gates remain in force. Teardown/recovery reset open state.
No radio timing, rate, antenna, firmware, power limit or authentication fallback
policy changes from 0.2.15 are made.

HeliPort 2.1.5 removes secret-value reads from menu metadata and saved-network
listing, disables authorization dialogs for background retries, reuses authorized
credentials during the app session and bounds failed automatic attempts. Open
scan entries never retrieve an old password for the same SSID.

Portable fixtures cover open/WPA2 credential matching, downgrade resistance,
malformed/truncated/duplicate security IEs, association Privacy bits and all
open/WPA2 plaintext/linked/aggregate/EAPOL policy combinations. Host ASan/UBSan
and the Intel macOS build must pass before packaging. These checks cannot prove
RF connectivity or macOS Keychain behavior; both need hardware validation.

Install the app in /Applications. The app-only Keychain fix needs no reboot.
For open hotspots, also replace the existing itlmt7902.kext and reboot once.
Retain the current connection-mode configuration; no new boot argument is added.
Try a password-free Android hotspot, a WPA2 hotspot, and each Ramos band in that
same boot. Capture the actual error and wifi-attempt-history if a join fails.
Ramos authentication remains an open hardware issue; this release does not claim
that adding open support fixes WPA2 router authentication.
