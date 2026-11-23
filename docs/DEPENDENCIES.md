# Dependencies and third-party components

Inventory reviewed on 2026-11-21 against `CMakeLists.txt`,
`unit_tests/CMakeLists.txt`, `docker/Dockerfile`, source includes, Python imports,
embedded WebUI assets and the release workflow. See [BUILD.md](../BUILD.md) for
installation. The application uses GPL-3.0-only; dependency licenses remain
separate. See [distribution instructions](DISTRIBUTION.md).

## Application and runtime

| Component | Actual use and version evidence | License and distribution implications |
| --- | --- | --- |
| Simple-Web-Server | Header-only HTTP implementation; Git submodule v3.1.1, commit `898b6abd1be568ff9de4390d44288962e3fac337` | MIT; retain Ole Christian Eidheim's copyright and full permission/disclaimer notice from [the bundled license](../Simple-Web-Server/LICENSE), including when its headers are compiled into a binary. |
| Boost | Asio, PropertyTree (JSON/XML, including its bundled RapidXML implementation), DateTime, Signals2 and UUID utilities; CMake also discovers System, Regex and Thread. Container packages use 1.83. | [BSL-1.0](https://www.boost.org/LICENSE_1_0.txt); preserve source notices. The license includes an exception for solely compiled object code. Boost.Test is optional. |
| OpenSSL libcrypto | Direct `OpenSSL::Crypto` link for Digest and WS-Security; inspected runtime 3.5.7 | [Apache-2.0 for OpenSSL 3+](https://openssl-library.org/source/license/index.html). CMake does not enforce version 3; older OpenSSL has different terms and needs separate review. SSL is discovered by the submodule, but the emulator serves HTTP and explicitly links Crypto. |
| GStreamer core, RTSP server, RTSP, app, video and their dependent base/RTP/SDP/audio libraries | `pkg-config` modules `gstreamer-rtsp-server-1.0`, `gstreamer-rtsp-1.0`, `gstreamer-app-1.0`, `gstreamer-video-1.0`; inspected runtime 1.26.2 | Primarily LGPL-2.1-or-later, with per-file exceptions/notices. Dynamic linking permits multiple application licenses subject to LGPL conditions; see [GStreamer licensing guidance](https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html). |
| GLib, GObject, GIO, GModule | GStreamer event loops, objects and transitive runtime support; inspected GLib 2.84.4 | Primarily LGPL-2.1-or-later; retain package-specific notices and provide corresponding library source as required. |
| gst-plugin-onvif, gst-plugins-rs | `rtponvifmetadatapay`; upstream 0.13.7, commit `cdec77cad2d9b7348d800507bc6eeec5039717eb`; needs GStreamer 1.20+ and Rust 1.71+ to build | MPL-2.0. Loaded as a separate `.so`; its license does not automatically make the emulator MPL. Docker includes its upstream source archive, license and collected dependency notices in `/usr/local/share/osrv-metadata/`. See [provenance](../experiments/rtsp_metadata/README.md#plugin-provenance-and-notices). |
| Python 3 | Runtime subprocess for configuration validation, not just a developer tool; inspected interpreter 3.13 | [PSF-2.0 and incorporated-component terms](https://docs.python.org/3/license.html); retain Python's license history and notices when distributing the interpreter. |
| jsonschema, referencing | Required by CMake and `scripts/validate_configs.py`; `schemas/requirements.txt` specifies `jsonschema>=4.18,<5`; inspected versions 4.19.2 and 0.36.2 | MIT; preserve their copyright, permission and disclaimer notices. The requirement range is not a lock file. |
| attrs, jsonschema-specifications, rpds-py | Transitive Python validation dependencies; inspected versions 25.3.0, 2023.12.1 and 0.21.0 | MIT in the inspected Debian packages; rpds-py's native code and bundled Rust materials also have package notices. These dependencies must accompany an isolated Python runtime. |
| glibc, libstdc++, libgcc and libatomic | C/C++ runtime and threading; inspected glibc 2.41, GCC runtime 14.2 | glibc primarily LGPL-2.1-or-later; GCC runtime GPL-3.0-or-later with the GCC Runtime Library Exception where applicable. Using GCC does not by itself require GPL licensing of application code. Preserve the component-specific exceptions. |

`onvif_server` is an internal static library containing project sources; this does
not mean that GStreamer, OpenSSL or all other external libraries are statically
linked. `ldd /app/main` in the inspected runtime confirms shared GStreamer,
GLib, libcrypto and GNU runtime libraries. Header-only dependencies are not shown
by `ldd`.

## Media plugins, codecs, fonts and graphics

The Dockerfile installs complete plugin packages, which pull in substantially
more libraries than the default pipelines use. A package's `base`, `good`, `bad`
or `ugly` name is not its license. In particular, an LGPL plugin wrapper can load
a GPL codec library.

| Component | Where used | License findings |
| --- | --- | --- |
| GStreamer base/good/x plugins | Test video/audio sources, converters, resampling, queues, RTP payloaders, `alawenc`, `mulawenc`, `timeoverlay`; `pngdec`, `videoscale`, `pngenc` for startup scene previews | Primarily LGPL-2.1-or-later plus per-file permissive terms; inspect the exact package notices. `gstreamer1.0-x` supplies the Pango overlay plugin. |
| x264 / `x264enc` (ugly) | Hardcoded high/low live video and replay in `RtspServer.cpp`, including file-backed streams | Debian `libx264-164` uses GPL-2.0-or-later for its main code. Runtime version `0.164.3108+git31e19f9`. A separate upstream commercial license, if obtained, does not automatically cover Debian's binary or other GPL dependencies. |
| gst-libav and FFmpeg | `avenc_aac`, `avenc_g726` in `utility/AudioSourceReader.cpp`; file playback through `decodebin` can load additional decoders | The inspected FFmpeg 7.1.5 library reports **GPL version 2 or later**, and `avcodec_configuration()` includes `--enable-gpl`, `--enable-libx264` and `--enable-libx265`. Do not label this distribution LGPL-only based on FFmpeg's default upstream license. See [FFmpeg's explanation](https://ffmpeg.org/legal.html). |
| GStreamer bad plugins | Replay `rtponviftimestamp` and extra installed plugins | Mixed dependency obligations. Removing `ugly` alone does not eliminate GPL components in the rest of the image. |
| Other installed codecs/media support | Examples: x265, Xvid, libmpeg2, liba52, libdca, FAAD, libcdio, Rubber Band, FFTW; also Opus, Vorbis, Ogg, FLAC, libvpx, dav1d, OpenH264, JPEG, PNG and WebP | Includes GPL, LGPL, BSD, MIT and other terms, depending on library and file. These are transitive packages, not all direct emulator links. Build-specific package versions and original license notices are preserved as described below. Codec patents are separate from copyright licenses. |
| Pango/PangoCairo, Cairo, HarfBuzz, FreeType, Fontconfig, FriBidi, Pixman | Overlay text, metadata plugin and their dependencies | Pango/FriBidi primarily LGPL; Cairo LGPL-2.1-or-later OR MPL-1.1; FreeType FTL OR GPL-2.0-or-later; HarfBuzz/Fontconfig/Pixman have permissive terms and exceptions. Preserve exact package notices and choose a compatible offered alternative. |
| DejaVu fonts | Explicit `fonts-dejavu-core` package for text overlays | Bitstream Vera terms plus public-domain DejaVu changes and package-specific notices; font notices and naming restrictions remain separate from the application license. |
| TimGM soundfont, icons and other image data packages | Pulled transitively by the broad plugin/graphics packages | TimGM has GPL terms; icons include Creative Commons and other terms. Their presence in an image does not automatically relicense emulator code, but their own distribution conditions still apply. |

## Build, validation, browser tests and CI

| Component | Scope | License / notes |
| --- | --- | --- |
| C++20 compiler, GNU Make, CMake 3.16+, pkg-config, Git, ccache | Build tools; Docker `toolchain`/`dev` stages | GCC/Make/Git/ccache principally GPL; CMake BSD-3-Clause; Debian's `pkg-config` command is supplied by pkgconf (ISC, plus file-specific terms). Tool use alone does not transfer their license to generated application code. Distributed development images retain their own obligations. |
| rustc, Cargo, Rust standard library and Cargo crates | Build the pinned metadata `.so`; not needed to execute it | Rust principally MIT OR Apache-2.0 with component exceptions. Inspect target/features and selected crate manifests, rather than assuming one license covers every crate. |
| Boost.Test | Optional C++ unit tests with `ENABLE_UNIT_TESTS=ON` | BSL-1.0. |
| lxml / libxml2 / libxslt | XML/schema validation in smoke tests and metadata experiments; inspected lxml 5.4.0 | lxml core BSD-3-Clause, with separate test/schema notices; libxml2/libxslt principally MIT with file-specific notices. Not a direct C++ runtime dependency. |
| xmlschema / elementpath | XML Schema 1.1 tests; inspected 3.4.5 / 4.8.0 | MIT for both in the inspected development packages. |
| PyGObject / GObject introspection | Python metadata experiment server | PyGObject LGPL-2.1-or-later; introspection and wrapped libraries retain their own terms. |
| Selenium / Chromium / ChromeDriver | Optional `unit_tests/*browser.py`; absent from the declared Docker `dev` package list, installed in the inspected browser-test image | Selenium Apache-2.0; Chromium/ChromeDriver BSD-style licenses plus numerous third-party notices. These are test tools, not embedded WebUI dependencies. |
| Selenium's Python dependency chain | Inspected browser environment | certifi MPL-2.0; websocket-client and sortedcontainers Apache-2.0; trio, outcome and sniffio MIT OR Apache-2.0; attrs, urllib3, trio-websocket, wsproto, h11 and idna MIT/BSD-style terms; typing_extensions PSF-2.0; packaging Apache-2.0 OR BSD-2-Clause. Read the exact Debian source-package notices in the development environment. |
| Docker Engine/BuildKit/Buildx/Compose, curl, CA certificates | Documented development/deployment tools | Docker projects principally Apache-2.0, curl under the curl license; other components retain their terms. These host tools are not all packaged in the runtime image. Debian's ca-certificates build package is distinct from its included certificate data. |
| GitHub Actions checkout v7 and Docker setup-buildx v4, build-push v7, login v4 | `.github/workflows/docker-publish.yml` | Upstream action repositories use MIT (checkout) and Apache-2.0 (Docker actions), with their own dependencies/notices. Version tags are mutable references, not source-content locks. They are not application libraries. |

The WebUI uses project-local HTML/CSS/JavaScript embedded by CMake. No npm
manifest, third-party frontend framework, downloaded web font or CDN dependency
was found. System fonts selected by CSS are supplied by the client environment.

## Rust plugin dependency evidence

The plugin manifest directly names gstreamer, gstreamer-base, gstreamer-rtp,
gstreamer-video, cairo-rs, pango, pangocairo, chrono, once_cell, xmlparser and
xmltree, plus gst-plugin-version-helper as a build dependency. Its pinned
Cargo.lock records registry versions and Git revisions. The bindings and most
utility crates use permissive licenses; the plugin itself remains MPL-2.0.

New builds retain `cargo-artifacts.json` from actual Cargo compiler output and
`cargo-dependency-sources.tar.gz` with the corresponding external source roots
under `/usr/local/share/osrv-metadata/`. This includes build dependencies and
inherited source/license files. The full plugin source archive includes its
Cargo.lock. Complete selected Git checkout roots can include uncompiled crates;
the artifact manifest distinguishes actual build inputs from extra preserved files.
Do not infer a crate's offered license solely from the presence of both MIT and
Apache license files. Read its manifest and any per-file terms.

Workspace lock files also include optional and non-Linux dependencies; they are
not bills of compiled crates. Release inventories and source archives are
generated during each build.

## Protocol documents and bundled image assets

External ONVIF WSDL/XSD and their OASIS/W3C imports are validation inputs, not
libraries relicensed by the application license. The metadata harness pins
ONVIF specs commit `d7f3684501c4a6d58f879e72e1e3322c120f95e6` and hashes external
imports in [imports.lock.json](../experiments/rtsp_metadata/imports.lock.json).
The harness stores downloaded originals outside the repository/image. Check
per-file terms and [ONVIF's terms](https://www.onvif.org/terms-of-use/) before
redistributing those caches. No protocol material was copied by this review.

[Resource provenance](../server_configs/rs/README.md) records generated synthetic
plate/vehicle/face fixtures and the neutral `server_configs/rs/snapshot.jpeg`
generated from scratch on 2026-11-21 without reference images. The current
snapshot is a 1280x960 JPEG showing a fictional empty courtyard and a SYNTHETIC
TEST IMAGE label. This replaces the snapshot with unknown provenance; it does
not clear redistribution rights for the old bytes in Git history or existing
images/releases. User-provided streaming files also need their own distribution
rights. Original project assets are covered by the scope stated in [NOTICE](../NOTICE),
to the extent copyright applies.

## Build-specific inventories and review limits

The initial inspection used existing local Trixie runtime/development images,
not a proven rebuild of this checkout. Version examples above describe that
inspection, not permanent dependency locks. Local image IDs and large raw package
snapshots are intentionally not committed as release evidence.

New runtime builds generate `debian-source-packages.tsv` and binary hashes in
`/usr/local/share/onvif-server-emulator/`. The `source-bundle` target retrieves
those exact source packages (including Debian patches), plugin and Rust sources,
and original notices. The release workflow compares the bundle's binary hashes
and source-version manifest against the tested runtime before publication.
See [DISTRIBUTION.md](DISTRIBUTION.md) for extraction and availability requirements.

Debian copyright files may cover source-only tests, documentation, alternative
licenses or exceptions. Raw `License:` fields are not normalized SPDX conclusions
about every shipped binary; source manifests and license metadata do not replace
reading file-specific terms. The release bundle preserves original notices even
when they have no machine-readable license field.

To inspect a release, run `dpkg-query -W` inside its immutable image, read
`/usr/share/doc/<package>/copyright` (including symlinks), check `ldd /app/main`
and inspect its packaged Cargo artifact manifest. FFmpeg exposes its actual
build flags/license through `avcodec_configuration()` and `avcodec_license()`.
Dynamic plugin selection and `decodebin` require more than `ldd main`.
Repeat the review whenever package versions, codec settings or build flags change;
the unpinned Debian base tag and apt repositories can change the resulting image.
