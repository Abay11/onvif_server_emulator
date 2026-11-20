# Administration WebUI

Open `http://<emulator-host>:<http-port>/admin/` (by default,
`http://127.0.0.1:8080/admin/`). `/admin` redirects to `/admin/`. The administration
page shares the existing ONVIF HTTP listener and bind/port configuration. No extra
port, web process, JavaScript package installation, or container asset mount is
needed. HTML, CSS and JavaScript are embedded into the executable at CMake
configuration time and served locally without external fonts, scripts or CDNs.
Rebuilding after an asset change automatically regenerates the embedded assets.

The first version provides:

- Application reachability, process ID, uptime and automatic status refresh every
  five seconds while the tab is visible.
- Active bind/advertised endpoints and RTP range; emulated device identity;
  discovery interface; PullPoint settings; logging and authentication mode names;
  media transport and simulation settings.
- Current profile configuration associations and configured ONVIF service URLs,
  including whether each service is advertised by `GetServices`.
- Application restart with a confirmation dialog, progress, reconnection and a
  check that the application instance changed.

Settings are read only. The API selects explicit fields and excludes account
lists, passwords, arbitrary config contents and the media input file/URI.
Configuration settings are captured during initialization; unapplied file edits
are not presented as active settings. Profile associations come from the shared
`MediaProfilesManager` tree and reflect persisted SOAP changes without a restart.
Environment overrides for advertised/discovery IPv4 are included. Displayed media
associations do not guarantee that the corresponding hardcoded RTSP pipeline
parameters changed. Reachability indicates that the administration HTTP API is
responding, rather than a health probe of every subsystem.

## Access and restart

As requested for this initial version, **the WebUI and its JSON API have no login
or authorization**, including when SOAP Digest authentication is enabled. Any
client that can reach the HTTP listener can inspect the selected settings and
request an application restart. The page labels access as without sign-in.
Login/password authorization is a future stage; the current SOAP authentication
setting applies to SOAP and does not protect `/admin/`.

Restart uses the same callback and pending guard as ONVIF
[`SystemReboot`](SYSTEM_REBOOT.md). The response is written before the callback
executes. This restarts the application inside its container, preserving its PID,
arguments and environment. Streams and subscriptions disconnect. Invalid edited
configuration may prevent startup. A changed HTTP address or port requires
opening the new address manually. The UI waits up to 45 seconds for a changed
instance ID, then reports that restart could not be confirmed. It does not report
success merely because the old instance responds. Exec errors remain in the
application log because the acknowledgement has already been sent.

Reboot requires a POST with a custom header and explicit JSON confirmation.
Cross-site browser requests are rejected through Origin/Fetch Metadata checks;
CORS is not enabled. These measures prevent ordinary third-party web pages and
HTML forms from requesting a restart. They are not authentication. Responses
are not cached and include a restrictive Content Security Policy, MIME sniffing
protection and framing restrictions. Configuration values are inserted into the
DOM as text, not executable HTML.

## HTTP interface

These are emulator administration endpoints, separate from the ONVIF contract.

| Request | Response |
| --- | --- |
| `GET /admin/` | Embedded HTML administration page |
| `GET /admin/app.css` | Embedded stylesheet |
| `GET /admin/app.js` | Embedded browser script |
| `GET /admin/api/status` | Status and selected active settings |
| `POST /admin/api/reboot` | `202 {"status":"rebooting"}`, followed by restart |

Status has `status`, `instance_id`, `pid`, `uptime_seconds`, `reboot_available`,
`reboot_pending`, `settings`, and `profiles` fields. `instance_id` changes across
application runs; `pid` remains unchanged across exec. Top-level numeric and
boolean status fields have JSON number/boolean types. Settings scalars and
profile fields are strings, following the existing PropertyTree configuration
model. `profiles` is always an array. Service listing flags describe advertisement
rather than runtime enablement of SOAP handlers.

For a non-browser client:

```bash
curl --fail-with-body http://127.0.0.1:8080/admin/api/status
curl --fail-with-body http://127.0.0.1:8080/admin/api/reboot \
  -H 'Content-Type: application/json' \
  -H 'X-ONVIF-Admin: reboot' \
  --data '{"confirm":true}'
```

A reboot request without the required custom header, or with a mismatched Origin
or cross-site Fetch Metadata, receives 403. A wrong Content-Type receives 415;
missing/invalid confirmation receives 400; an unavailable reboot callback receives
503. GET on the reboot endpoint receives 405 and cannot restart the application.

## Implementation and validation

`web/AdminUi.cpp` registers the routes during `Server::init()`, after subsystem
initialization. `web/index.html`, `app.css` and `app.js` are embedded through
`web/Assets.h.in`. `utility/ApplicationReboot.*` shares response completion and
restart suppression between the SOAP handler and administration API. Status and
profile mutations use the same HTTP event loop (the existing single-worker
configuration).

Use the documented [Docker dev environment](../BUILD.md#reusable-development-environment):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_UNIT_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 unit_tests/admin_ui_smoke.py --network-smoke
python3 unit_tests/system_reboot_smoke.py
```

The smoke test starts its own source-built process with copied configuration;
run it in an isolated container network namespace with default service ports
available. It checks embedded assets, JSON types, secret exclusion, environment
overrides, rejected reboot requests, actual WebUI restart/config reload, stable
PID with a new instance, and continued SOAP restart support. With
`--network-smoke`, it also runs `network_smoke.py` against the restarted server.
CTest includes administration HTTP tests, live profile updates, startup settings
versus edited files, missing callback handling and the shared pending flag.

For browser acceptance, check desktop and narrow layouts, keyboard navigation,
dialog cancellation/confirmation, offline status and reconnect after restart.
A process without a reboot callback should display a disabled restart button.
The Linux checks do not replace the [Windows/VMS acceptance checks](WSL2.md).
