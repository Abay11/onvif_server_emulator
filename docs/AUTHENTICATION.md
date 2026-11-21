# Authentication

`common.config.authentication` selects SOAP authentication for all ordinary
services and the separate Event/PullPoint dispatcher. The bundled default is
`digest/ws-security`. Use WebUI **Users & authentication** to select a scheme,
create/delete accounts and enable/disable accounts. Changes require a restart:
SOAP and browser sessions retain their startup accounts until then. At least one
enabled administrator must remain when saving accounts through WebUI.

Supported modes:

| Mode | Accepted credentials |
| --- | --- |
| `none` | Authentication disabled |
| `digest` | HTTP Digest |
| `ws-security` | SOAP WS-Security UsernameToken |
| `digest/ws-security` | Either mechanism; both must verify when both are supplied |

Every protected operation checks the verified account against its ONVIF access
class before invoking the handler, including on Events/PullPoint. Access classes
are distinct identifiers; permissions follow Core 26.06 section 5.9.4 (Table 7),
not a comparison of enum values. Unknown roles and access classes are denied.

| Access class | Allowed roles |
| --- | --- |
| PRE_AUTH | Anonymous, User, Operator, Administrator |
| READ_SYSTEM, READ_MEDIA | User, Operator, Administrator |
| READ_SYSTEM_SENSITIVE, ACTUATE | Operator, Administrator |
| READ_SYSTEM_SECRET, WRITE_SYSTEM, UNRECOVERABLE | Administrator |

A verified account lacking access gets HTTP 400 with a SOAP 1.2 Sender /
ter:NotAuthorized fault, without WWW-Authenticate headers. Missing/invalid Digest
credentials instead get HTTP 401 with a challenge. Denied requests never reach
operation validation, configuration persistence, or reboot callbacks. PRE_AUTH
operations remain available anonymously. Mode `none` deliberately bypasses both
authentication and authorization for open emulator scenarios.

## Accounts and migration

Accounts are read from `users.config` beside `common.config`:

```json
{
  "users": [
    {"login": "a", "password": "a1", "type": "administrator", "enabled": true},
    {"login": "o", "password": "o1", "type": "operator", "enabled": true},
    {"login": "u", "password": "u1", "type": "user", "enabled": false}
  ]
}
```

Passwords and logins are intentionally stored in plaintext for this emulator.
There is no encryption or password hashing at rest. Logins must be nonempty,
unique, contain no control characters, and contain no colon. Disabled accounts
cannot authenticate through either mechanism. Startup requires the accounts file;
there is no fallback to accounts in `common.config`.

To migrate an existing configuration, move its `users` array into a new
`users.config` object, add boolean `enabled` to each account, and remove `users`
from `common.config`. Restart after changing either authentication mode or accounts.
The WebUI configuration editor includes the new file and preserves its existing
write-only password placeholders when saving.

## HTTP Digest

Challenges contain three separate WWW-Authenticate headers in this fixed order:
MD5, SHA-256, SHA-512-256. MD5 is deliberately preferred for client compatibility.
The static realm is `Realm`. SHA-512-256 is the RFC 7616 algorithm, distinct from
ordinary SHA-512. The verifier also accepts the corresponding `-sess` forms.
The supported protection quality is `qop=auth`; `auth-int`, userhash, and
`username*` are not implemented. An omitted algorithm means MD5. Legacy responses
without qop are rejected because the server advertises qop.

Verification uses the actual HTTP method and request target, including its query.
Nonces contain 32 random bytes, expire after five minutes, and are shared across
SOAP dispatchers. Successful requests require increasing nonzero eight-digit
hexadecimal nonce counts per user/client nonce. Replays are rejected. The nonce
cache holds at most 4096 server nonces and 4096 client counters per server nonce;
issuing a challenge at capacity can invalidate an older outstanding challenge.
An otherwise valid response using an expired/evicted nonce receives a fresh
challenge with `stale=true`.

Missing/invalid HTTP credentials get HTTP 401 and fresh Digest challenges.

## WS-Security

The implementation supports UsernameToken `PasswordDigest` (SHA-1 of decoded
nonce, original Created text, and password, encoded as base64) and `PasswordText`.
It resolves XML namespaces rather than assuming particular prefixes. Nonce and
Created are mandatory for both forms. Nonce uses base64; other encoding types and
unknown password types are rejected. PasswordText is the default when Type is absent.

Created must include a timezone (`Z` or an explicit UTC offset), optionally with fractional seconds.
The accepted age is five minutes, with up to 30 seconds of future clock skew.
Successful tokens consume their decoded nonce globally across SOAP services and
users. Nonces remain cached for six minutes; a full 16384-entry cache rejects new
tokens until entries expire. Missing or invalid WS credentials get HTTP 400 and
a SOAP Sender/ter:NotAuthorized fault. In combined mode, a request with no
credentials gets the HTTP Digest challenge. When both credentials are supplied,
HTTP validation precedes WS validation, and both must identify the same account.

## Scope and references

SOAP authentication is independent of browser administration sessions. The
[WebUI](WEB_UI.md#access-and-restart) requires an enabled administrator account
from the same `users.config`, including when SOAP authentication is disabled.
RTSP, snapshots, and synthetic analytics image GET routes retain their existing
access behavior.
User management through ONVIF GetUsers/CreateUsers/DeleteUsers/SetUser and
SetHashingAlgorithm is not implemented. The challenge algorithm list is fixed.
These are emulator limitations, not claims of full ONVIF conformance. Full
WS-Security signatures, encryption, other security token profiles, and password
key derivation are outside this UsernameToken implementation.

Protocol references used for this implementation:

- [ONVIF Core 26.06](https://www.onvif.org/specs/core/ONVIF-Core-Specification.pdf),
  sections 5.9.1, 5.9.3, 5.9.4, 5.9.5 (retrieved 2026-11-21).
- [Device management WSDL](https://www.onvif.org/ver10/device/wsdl/devicemgmt.wsdl)
  and its imported [onvif.xsd](https://www.onvif.org/ver10/schema/onvif.xsd),
  version 26.06, retrieved 2026-11-21. Device GetServiceCapabilities and GetServices capabilities
  use the WSDL's ordered Network/Security/System structure and Security attributes.
  The bundled Device service version is updated to 26.06.
  The SHA-512-256 extension is from RFC 7616; Core specifically discusses MD5/SHA-256.
- [RFC 7616](https://www.rfc-editor.org/rfc/rfc7616.html), HTTP Digest calculations,
  challenges, session algorithms and replay tracking.
- [OASIS UsernameToken Profile 1.1.1](https://docs.oasis-open.org/wss-m/wss/v1.1.1/os/wss-UsernameTokenProfile-v1.1.1-os.html),
  section 3.1, retaining the 2004 WS-Security namespace identifiers.

No third-party specification text or schemas are bundled by this change.
Cryptography uses dynamically linked OpenSSL 3 libcrypto (Apache-2.0); Debian
packages retain its notices at `/usr/share/doc/libssl3t64/copyright` in the container.

## Validation

Build and run CTest as documented in [BUILD.md](../BUILD.md). Additional checks:

```bash
python3 scripts/validate_configs.py server_configs
python3 unit_tests/config_schema_tests.py
python3 unit_tests/authentication_smoke.py --binary build/main
python3 unit_tests/configuration_ui_smoke.py --binary build/main
```

The authentication smoke test starts isolated server processes using configuration
copies and requires the default HTTP/RTSP/discovery ports to be available. It
exercises all modes, algorithms, role rejection, disabled accounts, replay, Device
capabilities and Events/PullPoint. It also checks role permissions through Digest
and UsernameToken, unchanged configuration after denied mutations, and the
NotAuthorized fault. Boost.Test additionally verifies the full access matrix and
that denied SystemReboot requests never invoke the reboot callback. Run the ordinary network smoke test separately
against a server with authentication disabled. Real Windows/VMS interoperability
still requires the checks in [WSL2.md](WSL2.md).
