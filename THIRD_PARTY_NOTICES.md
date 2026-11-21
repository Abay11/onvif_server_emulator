# Third-party notices

The application is licensed under GPL-3.0-only; that grant does not replace the
terms of its dependencies. Original third-party copyright notices, licenses,
exceptions and source files must remain with their respective components.

| Component | License / required materials |
| --- | --- |
| Simple-Web-Server v3.1.1, commit `898b6abd1be568ff9de4390d44288962e3fac337` | Copyright (c) 2014-2020 Ole Christian Eidheim. Full MIT notice in `LICENSES/Simple-Web-Server-MIT.txt`; this notice also accompanies binaries compiled from its headers. |
| Boost | Boost Software License 1.0, reproduced in `LICENSES/BSL-1.0.txt`. Original source/package notices identify individual contributors, including code embedded in header-only components. |
| OpenSSL 3 | Apache License 2.0, reproduced in `LICENSES/Apache-2.0.txt`. Original copyright and attribution notices are in the exact package's `/usr/share/doc/libssl3t64/copyright` and corresponding source. Older OpenSSL versions require a fresh license review. |
| GStreamer, RTSP Server and GLib | Primarily LGPL-2.1-or-later; see `LICENSES/LGPL-2.1.txt`, original package copyright files, corresponding sources and any per-file exceptions. |
| x264, GPL-enabled FFmpeg and other GPL libraries | Original GPL notices and corresponding source must accompany distribution as required. `LICENSES/GPL-2.0.txt` and the application's `LICENSE` reproduce GPLv2 and GPLv3; the exact dependency's offered versions still control. |
| gst-plugins-rs ONVIF plugin 0.13.7, commit `cdec77cad2d9b7348d800507bc6eeec5039717eb` | MPL-2.0, reproduced in `LICENSES/MPL-2.0.txt`. The unmodified plugin sources, actual Cargo artifact manifest, external Rust source archive and upstream notices are packaged under `/usr/local/share/osrv-metadata/`. |
| Python and validation packages | Python's PSF license history and included component terms remain in its Debian copyright/source files. jsonschema, referencing, attrs, jsonschema-specifications and rpds-py principally use MIT; retain their original notices, including bundled Rust materials. |
| Fonts, graphics, codecs and remaining Debian dependencies | The exact `/usr/share/doc/*/copyright` files and `/usr/share/common-licenses/` in the image remain authoritative. The release source bundle includes source packages for all installed Debian packages, rather than assuming one license covers an entire plugin package. |

The copies in `LICENSES/` came from the checked-out Simple-Web-Server license and
Debian's verbatim common-license texts on 2026-11-21. They are unmodified license
texts, not new grants by the emulator author. They supplement rather than replace
file-specific notices. The application's GPLv3 text is in `LICENSE`.

See [DEPENDENCIES.md](docs/DEPENDENCIES.md) for component use and
[DISTRIBUTION.md](docs/DISTRIBUTION.md) for exact-source packaging and release
instructions. Neither a dependency list nor installing packages alone establishes
that every distribution obligation has been met.
