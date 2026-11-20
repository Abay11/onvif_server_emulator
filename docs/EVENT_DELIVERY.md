# Shared event delivery (metadata Stage 2)

The emulator now generates each live notification once and distributes immutable
messages to independent consumers. HTTP PullPoint uses this source. The RTSP
server receives the same `IEventSource` dependency. [Stage 3](RTSP_METADATA.md)
adds an opt-in live metadata track using that source. SOAP metadata configuration
and capability advertisement remain Stage 4 work.

## Ownership and execution

`Server` constructs a shared `event::EventSource`. Event service initialization
registers its configured generators on this source and calls `Run()` once.
Subscribing, synchronizing or unsubscribing does not restart generator timers.

`EventSource` owns a single Asio worker. Its `Invoke()` helper serializes generator
registration, snapshots and PullPoint state changes, including HTTP requests that
arrive on another thread. Calls already on the event worker execute directly.
Generator callbacks publish through this executor. Registration is finished before
`Run()`; repeated `Run()` calls have no effect.

`IEventSource::Subscribe()` returns an independently owned `EventSubscription`.
By default it queues the current Initialized property states. A consumer can omit
that initial snapshot. `Synchronize()` appends property snapshots to just the
specified queue, preserving pending transient notifications. Generators without
property state contribute no initialization messages.

Consumers can call `Take()` from another thread. Each batch contains shared
pointers to const notifications, a dropped-message count and a synchronization
flag. `AsyncWait()` posts one readiness callback to the consumer's Asio context;
it does not execute consumer code on the publishing path. Keep that context alive
until the subscription is closed and any running callback finishes. Capture
consumer dependencies weakly in callbacks. `CancelWait()`/`Close()` invalidate
queued callbacks; they do not interrupt an already executing callback.

The source retains weak subscription references. Destroying a handle closes its
queue. Source destruction disconnects generators, cancels their timers, closes
remaining queues, stops the worker and joins it. Owners must release the source
from outside its worker. Event-service teardown releases its PullPoints before
the server's source dependency is destroyed. Existing global Event-service state,
the detached optional HTTP listener and RTSP shutdown still prevent claiming
complete restart or multi-server support.

## Queue limits and recovery

The default queue permits 256 notifications and 4 MiB of serialized notification
payload per consumer. These are internal `EventQueueLimits`, not a new config
file schema. The byte limit measures XML payload size, not total allocator or
shared-message memory usage.

On overflow the queue drops its oldest notifications until the new one fits.
A single notification larger than the byte limit is dropped. Other consumers
continue independently. Loss produces a log warning, a per-batch dropped count
and a sticky synchronization flag. A consumer should drain its bounded backlog
and ask the source for property state again. The flag clears only when a complete
snapshot can be queued without another loss. Transient events cannot be recovered
by a property snapshot; no transient replay or persistent notification storage
is implied.

PullPoint attempts property resynchronization before returning messages after
overflow. If a snapshot itself exceeds queue limits, loss remains reported and
subsequent pulls retry. Consumers must choose limits large enough for the desired
property snapshot and embedded image notifications.

## PullPoint behavior

- Subscriptions receive unique paths (`s0`, `s1`, ...) and independent queues.
  Lookup compares complete paths, so `s1` cannot match `s10` or an appended path.
- Creation queues Initialized states. Synchronization appends fresh property
  states without clearing transient events already waiting for delivery.
- PullMessages respects MessageLimit and leaves remaining messages queued.
  A second blocking request for one subscription receives an
  `wsnt:UnableToGetMessagesFault` instead of replacing the first request.
- Each subscription has its own renewable 60-second lease. A pending pull keeps
  its subscription alive through the configured wait plus another 60 seconds;
  delivery renews the lease. Unsubscribe or expiration cancels that subscription's
  timers and readiness wait. A pending HTTP pull receives ResourceUnknownFault
  when its subscription closes. Invalid/removed references also receive this
  typed fault.

The existing simulation still uses `PullPoint.Timeout` instead of the client's
Timeout, does not implement subscription topic/content filters or ChangedOnly,
and does not honor requested InitialTerminationTime/Renew TerminationTime. The
fixed lease is an explicit simulation limitation; it is not full ONVIF subscription
lifetime compliance. Completing these request semantics belongs to later contract
work. Metadata configuration/filtering is planned in Stage 4.

## XML contract and references

`onvif/Notification.*` holds the transport-independent model and serializer.
PullMessages adds its SOAP timing/envelope fields around the same notification
node used by `serialize_metadata()` under
`tt:MetadataStream/tt:Event/wsnt:NotificationMessage`. The metadata wrapper has
no SOAP envelope. Its default namespace bindings cover `tt`, `wsnt` and `tns1`;
callers can supply additional topic and structured-payload namespace bindings.
Event initialization also stores its configured bindings on the shared source;
the RTSP adapter captures those bindings before preparing its media instance.
XML escaping and structured ElementItem trees are preserved. An unset legacy
data_name no longer produces an unnamed SimpleItem.

Use the pinned [ONVIF 24.12 release](https://github.com/onvif/specs/tree/d7f3684501c4a6d58f879e72e1e3322c120f95e6)
selected in [Stage 1](../experiments/rtsp_metadata/README.md): Core Event Handling
(PullMessages and SetSynchronizationPoint), Streaming metadata RTP/container,
`wsdl/ver10/events/wsdl/event.wsdl`, `wsdl/ver10/schema/metadatastream.xsd`,
`onvif.xsd`, `common.xsd` and their imports. The typed fault details use imported
WS-Notification/WS-Resource/WS-BaseFault definitions. Additional binding references
are [WS-BaseNotification 1.3](https://docs.oasis-open.org/wsn/wsn-ws_base_notification-1.3-spec-os.htm)
and [WS-BaseFaults 1.2](https://docs.oasis-open.org/wsrf/wsrf-ws_base_faults-1.2-spec-os.pdf).
No third-party specification/schema text is copied into this document.

## Validation

In the documented development container run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The `SharedEvents` Boost suite covers independent PullPoint/metadata consumers,
unchanged generator deadlines, overflow/oversized messages, snapshots, message
limits, cancellation, subscription expiration, executor isolation, shared message
identity and escaped structured XML. CTest also writes representative metadata
and fault XML into its build fixture directory.

After preparing the Stage 1 schema volume, validate those artifacts offline:

```bash
docker run --rm --network none \
  -v "$(pwd)":/workspace:ro \
  -v osrv-dev-build:/workspace/build:ro \
  -v osrv-metadata-contract:/contract:ro \
  osrv-metadata-dev:stage1 \
  python3 unit_tests/validate_event_xml.py --contract /contract \
    --artifacts build/unit_tests/fixtures/build/unit_tests
```

The validator creates its own schema entry point importing the unmodified pinned
metadata, SOAP and resource-fault schemas. It validates typed fault details as
well as their envelopes.

Against a running source-built server with default event settings, disabled
authentication and a copied configuration, run:

```bash
python3 unit_tests/network_smoke.py
python3 unit_tests/event_delivery_smoke.py
```

Both scripts accept host/port options. The event script creates two subscriptions,
checks matching live notifications and independent unsubscribe, and removes its
subscriptions. The network script additionally exercises discovery and TCP/UDP
video/audio. LAN/Windows/VMS acceptance remains separate from container tests.

Stage 2 validation passed in `osrv-dev:latest` on the local Debian 13 Engine:
the CMake build, full CTest suite (including ten new SharedEvents cases), pinned
schema validation of all four generated XML artifacts, and both network scripts.
Network checks used a source-built server and configuration copies in an isolated
container; no host listening ports or source configuration files were changed.
