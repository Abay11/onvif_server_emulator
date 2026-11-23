# Administration WebUI

Open `https://<emulator-host>:8443/admin/` or `http://<emulator-host>:<http-port>/admin/` (by default,
`http://127.0.0.1:8080/admin/`). `/admin` redirects to `/admin/`. The administration
page shares the ONVIF HTTP and HTTPS listeners and bind/port configuration.
Both are enabled by default; see [HTTPS and certificates](HTTPS.md). No extra
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

The Users & authentication section manages accounts and the saved ONVIF scheme,
with the active scheme shown separately. Account and scheme changes require restart.
The overview shows active settings; [configuration administration](CONFIGURATION_UI.md)
supports media profile/configuration editing and validated JSON file saves.
The separate **Network configuration** sidebar section edits HTTP/HTTPS, addresses,
advertised web protocol, ports, Discovery and PullPoint connections. Network changes
require restart; active values remain separate from saved settings.
Restart requirements and write failures are reported explicitly. The [detector panel](DETECTORS_UI.md)
provides persisted rule editing, live detector settings and manual simulation steps.
The status API selects explicit fields and excludes account
lists, passwords, arbitrary config contents and the media input file/URI.
Configuration settings are captured during initialization; unapplied file edits
are not presented as active settings. Profile associations come from the shared
`MediaProfilesManager` tree and reflect persisted SOAP changes without a restart.
Environment overrides for advertised/discovery IPv4 are included. Displayed media
associations do not guarantee that the corresponding hardcoded RTSP pipeline
parameters changed. Reachability indicates that the administration HTTP API is
responding, rather than a health probe of every subsystem.

## Access and restart

`/admin/` redirects unauthenticated browsers to `/admin/login`. Use the login
and password of any enabled account with `type: "administrator"` in `users.config`.
Operator, User, disabled accounts and incorrect credentials are rejected with the
same generic sign-in error. This is mandatory even when SOAP authentication is
`none`; the account snapshot is shared with ONVIF and is reloaded on restart.

POST `/admin/api/login` accepts JSON `{ "login": "...", "password": "..." }`
with `X-ONVIF-Admin: login`. Successful sign-in creates a random 256-bit session
and sets `onvif_admin_session` with Path `/admin`, HttpOnly, SameSite=Strict and
Max-Age=28800. Sessions expire after eight hours, are kept only in server memory,
and disappear on application restart. HTTPS responses add Secure to the session
cookie. Transport security is determined by the listener, without trusting proxy
forwarding headers.
The server retains at most 1024 unexpired sessions. At capacity, sign-in returns
503 until sessions expire or users sign out.

All registered page/API handlers check the session before executing. Public
resources are the sign-in page/API and non-secret CSS/JavaScript assets. APIs
return HTTP 401 with `AuthenticationRequired` when a session is missing or expired;
client code returns to sign-in. No credentials or session tokens are stored in
localStorage or sessionStorage. POST `/admin/api/logout` with
`X-ONVIF-Admin: logout` revokes the session and clears its cookie. Login/logout and
all existing writes retain same-origin checks. An administrator can edit
`users.config` through the existing editor; saved account changes take effect for
both SOAP and WebUI on restart. Ensure an enabled administrator remains available;
if none remain, restore one in `users.config` outside the WebUI and restart.

Restart uses the same callback and pending guard as ONVIF
[`SystemReboot`](SYSTEM_REBOOT.md). The response is written before the callback
executes. This restarts the application inside its container, preserving its PID,
arguments and environment. Streams and subscriptions disconnect. Invalid edited
configuration may prevent startup. A changed HTTP address or port requires
opening the new address manually. Restart invalidates all browser sessions;
when the listener returns, the page requests administrator sign-in again. Exec errors remain in the
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
| `GET /admin/` | HTML for an administrator session; otherwise redirect to `/admin/login` |
| `GET /admin/login` | Public sign-in form |
| `POST /admin/api/login` | Verify administrator credentials and create a browser session |
| `POST /admin/api/logout` | Revoke the session and clear its cookie |
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
# Replace the bundled example credentials with an enabled ONVIF administrator.
curl --fail-with-body -c /tmp/onvif-admin.cookies \
  -H 'Content-Type: application/json' -H 'X-ONVIF-Admin: login' \
  --data '{"login":"a","password":"a1"}' http://127.0.0.1:8080/admin/api/login
curl --fail-with-body -b /tmp/onvif-admin.cookies http://127.0.0.1:8080/admin/api/status
curl --fail-with-body -b /tmp/onvif-admin.cookies http://127.0.0.1:8080/admin/api/reboot \
  -H 'Content-Type: application/json' \
  -H 'X-ONVIF-Admin: reboot' \
  --data '{"confirm":true}'
```

With a valid administrator session, a reboot request without the required custom
header, or with a mismatched Origin
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

## Live system event journal

The Event journal section updates about once per second through
`GET /admin/api/journal`. Category (system/client/detector), exact level
(info/warn/error), and text filters can be combined. Text search covers source
and summary, with case folding for ASCII text. Apply filters reloads retained
history; pause affects the view only. The browser keeps at most 500 recent
matching entries, newest first, and indicates connection failures and missing
history. Multiple browsers read independently. Hidden tabs suspend polling and
resume from their cursor when visible again.

The journal collects structured operational events rather than copying stdout
or reading log files. Sources currently include initialization/listener readiness,
restart requests and failures, SOAP processing failures and access rejections,
WebUI/ONVIF client activity, RTSP connection/disconnection, and detector event
summaries. HTTP activity is not a login or persistent connection session.
Authenticated WebUI page visits are recorded with the administrator role. SOAP
roles are recorded after verification when authentication is enabled; otherwise
SOAP activity is labelled unauthenticated. Repeated HTTP activity for the same peer/protocol/role
is coalesced for one minute, with at most 256 coalescing keys. Polling status and
journal endpoints does not generate activity records itself. A Digest challenge
can appear as an access rejection before the authenticated retry.

`ServerConfigs` owns a `SystemJournal`. `Server` injects it into `EventSource`
before event generators start. On each live publication, `EventSource::Publish`
writes a bounded summary directly into this journal **before** enqueuing messages
for external consumers. This does not create an `EventSubscription`, PullPoint,
RTSP metadata reader, ONVIF request or synchronization request. Collection runs
with zero ONVIF clients and with metadata disabled. Slow or overflowing external
consumer queues cannot consume, reset or suppress journal entries. Synchronization
snapshots generated for individual clients are not counted as new detector events.
The ONVIF wire format and subscription semantics are unchanged; their existing
contract references remain in [EVENT_DELIVERY.md](EVENT_DELIVERY.md).

The internal append is a short mutex-protected operation shared by all publisher
threads. It performs no network/disk I/O or browser callbacks. Reads take a
bounded snapshot; a browser's connection, filters and refresh rate do not control
detector generation. RTSP signal callbacks retain their journal handles through
GObject closure destruction, independently of the server's callback lifetime.

The server retains the latest **4096 entries in memory**, with bounded fields
(source up to 256 UTF-8 bytes, summary up to 1024). Detector summaries select
rule/source identifiers and known scalar state/count/object/likelihood fields;
images, image URLs, arbitrary ElementItems and recognition identity fields are
excluded. The timestamp is journal capture time in UTC. Each entry receives a
monotonic sequence ID under the journal lock. Retention evicts oldest entries;
this is recent operational history, not a durable audit log. Restart/exec clears
history and changes the journal instance ID. Abrupt termination cannot add a
shutdown record. Existing console/file logging remains separate and unchanged.

### Journal HTTP interface

Parameters are optional: `after` (nonnegative sequence ID, default 0), `limit`
(1–200, default 100), `category`, `level`, and `q` (at most 256 bytes; the UI allows
64 characters). Unknown, duplicate or invalid parameters receive 400. Reboot and
status APIs are unaffected. This endpoint requires the same administrator
session as the rest of the WebUI.

```bash
curl 'http://127.0.0.1:8080/admin/api/journal?category=detector&after=0&limit=100'
```

The JSON object contains `instance_id`, `cursor`, `has_more`, `reset`, `dropped`
and an `entries` array. Entries have numeric `id` plus string `time`, `level`,
`category`, `source`, `message`. `cursor` advances over all scanned records,
including filtered records; reuse it for incremental reads with the same filter.
A filter change should start at 0. `has_more` indicates more history to scan.
`dropped` counts global sequence IDs already evicted since the supplied cursor,
including records that might not match the current filter. A cursor ahead of the
current journal returns `reset=true` and restarts from retained history. Readers
must also compare `instance_id` and restart from 0 when it changes. A failed HTTP
request must not advance the reader's cursor. The UI follows these rules and
reports retention gaps explicitly.

### Journal validation

In the Docker dev environment, after building and running CTest:

```bash
python3 unit_tests/system_journal_smoke.py --network-smoke
```

This starts a source-built process with temporary configuration and metadata
explicitly disabled. It first waits for detector entries with no ONVIF consumers,
then checks independent reads, incremental cursors, filters and ONVIF/RTSP client
activity. The optional network checks run `network_smoke.py` and
`event_delivery_smoke.py` against that server. CTest also verifies concurrent
publication ordering, bounded retention and payloads, cursor reset/gap behavior,
filter validation, secret exclusion, and capture despite a slow external
subscriber overflowing its queue. Browser acceptance should additionally check
pause/resume, filter changes, reconnect, missing-history notices and layout.

## Detector panel

Open `/admin/#detectors` for built-in detector settings, the rule-based hierarchy,
uniform-frame geometry previews, vertex editing, enable switches and rule CRUD.
See [Detector administration](DETECTORS_UI.md) for API, coordinates, persistence
and runtime pause semantics. Rules and detector/type settings persist; rule engine
and individual rule enable switches reset after restart. WebUI authorization remains unchanged, including for these mutations.

## Save validation feedback

Saving or applying detector changes reports validation failures in a modal,
with the configuration filename, JSON Pointer to the field and a short reason.
The server checks the complete future configuration with the existing offline
JSON Schema validator before creating any temporary config file. Draft values
remain available for correction. Missing validation dependencies/resources block
the save as well. See [save validation](../schemas/README.md#server-save-gate-and-webui-errors)
for runtime packaging, HTTP 422/503 details and tests.

## Sign-in validation

`python3 unit_tests/web_login_browser.py --binary build/main` checks desktop/mobile
layout in Chromium, both administrator accounts, incorrect/disabled/non-admin
credentials, HttpOnly cookies, replay after logout and sign-in after restart.
Screenshots default to `/tmp/web-login-preview`. `admin_ui_tests.cpp` covers
page/API guards before mutations, same-origin login/logout, session rotation and
expiry with an injected clock. Existing Python WebUI checks use `admin_session.py`
and accept `OSRV_TEST_ADMIN_LOGIN` / `OSRV_TEST_ADMIN_PASSWORD` for custom accounts.
