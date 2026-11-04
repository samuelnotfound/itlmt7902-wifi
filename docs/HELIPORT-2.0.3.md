# HeliPort 2.0.3: connection UI lifecycle

The supplied 0.2.13 capture retains a connected hotspot, a successful WPA2
handshake, DHCP and bidirectional DNS after the app was reported to disappear.
It does not contain a HeliPort stack trace: the previous collector filtered only
messages containing the driver name. This capture does not establish a router
join result or the app's exact exit cause.

The app source had a shared animation timer created on a worker run loop and
force-unwrapped after other callbacks could clear it. The new animation runs on
the main run loop, serializes start/stop, rejects stale ticks and bounds frame
indices. Its regression test exercises rapid background start/stop requests,
cancellation, repeated starts and empty/single-frame providers.

Interface details now use SystemConfiguration. The old MAC lookup force-unwrapped
a byte-pattern search, and the route walker traversed unchecked sockaddr lengths.
Missing interfaces and incomplete connection snapshots now yield unavailable
details. The unknown PHY sentinel is explicitly declared in the app's C enum.

These correct identified crash risks; without a crash stack they are not proof
of the exact cause of the reported exit. The next hardware step is app-only:
quit any remaining HeliPort process, replace the app in `/Applications` and reopen
it with the current driver still loaded. No kext update or reboot is required.

The updated `scripts/collect-logs.sh` includes the app's unified-log subsystem,
its process status and up to three already-existing `.ips`/`.crash` reports.
Reports stay local. Release checks reject captured app process/crash reports.
