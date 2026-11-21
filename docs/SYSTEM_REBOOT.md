# Application Reboot

Device Management implements `SystemReboot` at `/onvif/device_service`.
The [administration WebUI](WEB_UI.md) exposes the same application restart through
a separate JSON endpoint and shares the pending restart guard with SOAP.
After editing configuration files, invoke this operation from an ONVIF client to
restart the emulator and reread the complete configuration directory, including
logging, authentication, endpoints, discovery, events, media profiles and RTSP.
The container and application PID stay the same. No container restart policy,
Docker socket, privileged mode or additional process manager is required.

Example for the default unauthenticated configuration:

```bash
curl --fail-with-body http://127.0.0.1:8080/onvif/device_service \
  -H 'Content-Type: application/soap+xml; charset=utf-8; action="http://www.onvif.org/ver10/device/wsdl/SystemReboot"' \
  --data '<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:tds="http://www.onvif.org/ver10/device/wsdl"><s:Body><tds:SystemReboot/></s:Body></s:Envelope>'
```

With Digest authentication, use an administrator account (for example add
`--digest --user 'a:a1'` for the shipped example credentials). The operation uses
the `UNRECOVERABLE` access class. As with other Device operations, the current
shared dispatcher enforces authentication only in `digest` mode; the other
configured authentication mode names do not establish enforcement.

The response is SOAP 1.2 with `tds:SystemRebootResponse` containing one required
`tds:Message` string. Restart is initiated only after the HTTP response has been
successfully written. A failed response write cancels that restart attempt.
Concurrent accepted reboot requests share the pending restart. Wrong operation
namespaces, nonempty parameters and unexpected operation attributes produce a
Sender / `ter:InvalidArgVal` SOAP fault. An embedding that does not supply a reboot
callback receives Receiver / `ter:ActionNotSupported` instead of success.

The executable supplies the restart callback to `Server::init()`. It isolates the
calling thread's file descriptor table and marks all descriptors above stderr
close-on-exec using Linux `close_range` with `UNSHARE | CLOEXEC`. It then executes
`/proc/self/exe` with the original arguments. This requires Linux 5.11+ and mounted
`/proc`, both present in the documented WSL2/container environment. All old
sockets and worker threads disappear on successful exec. If either system call
fails, the error is logged and the existing application remains available for a
retry. The success response has already been sent at that point and cannot be
replaced by a fault.

This simulates a device reboot by restarting the emulator application; it does
not reboot the host OS. Active HTTP/RTSP connections and event subscriptions are
lost, and clients must reconnect and recreate subscriptions. Persisted profile
changes survive; transient state is reset. Environment overrides and the original
configuration directory still apply. New listener settings take effect, so use
the updated address or port when reconnecting. Invalid edited configuration may
prevent the new application from starting; there is no validation or rollback
transaction. An exec does not change the container environment or bind mounts.

## Contract references

- [ONVIF Core Specification 21.06, section 8.3.13](https://www.onvif.org/specs/2106/ONVIF-Core-Spec-v2106.pdf):
  empty request, response before reboot, required Message, no operation-specific
  faults, `UNRECOVERABLE` access class.
- [Device Management WSDL, revision d7f3684501c4a6d58f879e72e1e3322c120f95e6](https://github.com/onvif/specs/blob/d7f3684501c4a6d58f879e72e1e3322c120f95e6/wsdl/ver10/device/wsdl/devicemgmt.wsdl):
  embedded XSD version 24.12, target namespace
  `http://www.onvif.org/ver10/device/wsdl`, empty `SystemReboot` complex type,
  `SystemRebootResponse/Message` of `xs:string`, and SOAP action
  `http://www.onvif.org/ver10/device/wsdl/SystemReboot`.

The Core version supplies operation semantics; the later WSDL supplies the wire
contract, which retains the same empty request and required string response for
this operation. The operation types use only XSD built-ins; imported ONVIF types
are not needed for their payloads. These references do not imply support for
other operations or changes introduced in that WSDL revision. Third-party
specification/schema files are not redistributed by this change.

## Validation

Use the documented Docker `dev` image and a copied configuration directory.
Boost tests cover the handler response, namespace/parameter rejection, unavailable
reboot callback, access class and recovery after a failed restart callback.
The integration test starts and terminates its own emulator with temporary config
copies; run it in an isolated container network namespace with the default ports
and TCP 18080 available:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 unit_tests/system_reboot_smoke.py --network-smoke
```

The smoke test checks complete responses, repeated reloads with an unchanged PID,
a changed HTTP listener, Digest administrator reboot and rejection for anonymous,
user and operator accounts. With `--network-smoke`, it also runs
`network_smoke.py` after each of the first two reboots, covering SOAP, discovery,
snapshots, PullPoint creation and TCP/UDP RTSP media. These Linux checks do not
replace the Windows/VMS acceptance steps in [WSL2.md](WSL2.md).

For optional validation against the complete pinned Device WSDL and its imports,
prepare the schema cache using [the contract helper](../experiments/rtsp_metadata/README.md)
and add `--contract-dir /path/to/cache` to the smoke command. This validates reboot
request and response payloads with XSD 1.1 without modifying upstream schemas.

The pinned Device WSDL contains bare text inside the annotation of its second
simple type, which strict schema compilation rejects. The optional validator
permits only that exact upstream annotation error during schema compilation and
still validates message instances strictly; upstream files remain unchanged.
