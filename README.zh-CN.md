# MT7902 connection test 0.2.15

See README.md for current installation steps, hardware checkpoints and limits.
Firmware initialization and passive scanning have user-reported hardware passes.
A 0.2.13 capture confirms hotspot WPA2 and Ethernet traffic; router authentication
still fails. Candidate 0.2.15 corrects pre-association QoS negotiation and adds
channel-grant diagnostics; its hardware behavior remains unverified.
Use mt7902_stage=3 for the connection test; stage 2 remains the default.
