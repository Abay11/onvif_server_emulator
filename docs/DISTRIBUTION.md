# Licensing and corresponding-source distribution

The project's original code, documentation, configuration and project-created
assets are licensed under **GPL-3.0-only**, to the extent copyright applies.
Copyright (C) 2020-2026 Alim Abaev. See [LICENSE](../LICENSE), [NOTICE](../NOTICE)
and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). The owner confirmed this
is personal development, unrelated to their employer, on 2026-11-21.
Third-party components retain their own licenses; GPLv3 does not replace them.

Commercial use and sale are permitted, including by downstream recipients.
Recipients must retain their GPL rights. No notice of development must be sent
to the FSF, library authors or ONVIF merely because GPL is used.

## Changes versus distribution obligations

When distributing a modified GPL-covered work, give prominent notices identifying
the modifications and their date. GPLv3 does not prescribe a file named CHANGES.
A clear modification notice in the affected files, with a dated change record,
is a practical approach. Git history alone should not be the only record if it
is omitted from distributed source archives.

New independently written application code is not a modification to an upstream
library and does not need an invented upstream change record. If the code adapts
or copies GPL material, or extends an existing GPL-covered work, GPL's
modification and notice conditions still apply.
Even when every dependency is unmodified, distributing a binary or Docker image
still requires the applicable licenses/notices and corresponding source under
the relevant GPL/LGPL/MPL provisions. Private development and execution generally
do not create an obligation to publish private changes. These distinctions follow
[GPLv3 sections 2, 4, 5 and 6](https://opensource.org/license/gpl-3.0).

No third-party source is modified by the licensing/packaging changes of
2026-11-21. The new packaging scripts, application notices, WebUI legal links,
license metadata and source-distribution workflow are original project changes.
The neutral JPEG replacement has separate [provenance](../server_configs/rs/README.md).

## Materials inside a new runtime image

`/usr/local/share/onvif-server-emulator/` contains:

- `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md` and `LICENSES/`;
- `application-source.tar.gz`, captured from the actual Docker build context,
  including the checked-out HTTP submodule and build instructions;
- `debian-source-packages.tsv`, exact source package versions for all installed
  Debian runtime packages;
- `SOURCE_README.md`, these instructions.

`/usr/local/share/osrv-metadata/` contains the plugin's unmodified upstream source
archive, MPL license, original collected notices, `cargo-artifacts.json`,
`cargo-dependency-sources.tar.gz` and `rust-toolchain-source.tsv`.
The artifact manifest is generated from Cargo's actual compiler output, including
fresh cached artifacts and build dependencies. It records license declarations
and source locations. The source archive preserves complete selected registry
crate directories and Git checkout roots, including inherited manifests and
notices. It can include extra files from those checkout roots; it does not claim
every preserved file was compiled. The original Cargo.lock remains in the plugin
source archive. Rust's standard-library source is supplied through the exact
Debian rustc source package in the companion bundle.

Debian's original `/usr/share/doc/*/copyright` and `/usr/share/common-licenses/`
remain in the runtime. Do not remove these notices to reduce image size.
The WebUI displays copyright, GPL, source and no-warranty information without
loading third-party scripts or fonts.

## Building a source companion

Build the runtime and the companion from the **same checkout, build context,
arguments and cached runtime layers**. The source stage captures the package
manifest before adding its download tools, enables signed Debian source indexes,
and downloads exact source versions with `apt-get source --download-only`.
The runtime stage upgrades inherited base packages before recording its manifest,
so stale base-image versions are not paired with newer archive sources.
Missing source versions or Cargo license declarations fail the build.
Downloading a newer source version is not a valid substitute.

From the repository root:

```bash
git submodule update --init --recursive
docker build -f docker/Dockerfile --target runtime \
  --build-arg CMAKE_BUILD_TYPE=Release --build-arg BUILD_JOBS=2 \
  -t osrv-image:release .
docker build -f docker/Dockerfile --target source-bundle \
  --build-arg CMAKE_BUILD_TYPE=Release --build-arg BUILD_JOBS=2 \
  --output type=local,dest=output/release-sources .
```

Source export can download a substantial amount of data: broad GStreamer packages
pull in many libraries. The runtime does not contain all Debian source archives;
they are delivered separately. `output/` is ignored by Git and Docker.

The export includes `SOURCE_README.md`, `SHA256SUMS` and ordered
`source-bundle.tar.gz.part-*` files, each smaller than GitHub's 2 GiB asset limit.
It also includes runtime binary hashes and the exact Debian source manifest for
comparison with the image being released. Keep every part and verify before use:

```bash
cd output/release-sources
sha256sum --check SHA256SUMS
cat source-bundle.tar.gz.part-* > source-bundle.tar.gz
mkdir extracted
tar -xzf source-bundle.tar.gz -C extracted
```

The extracted bundle contains application source/notices, plugin and external
Rust source, Debian source archives (`.dsc`, original tarballs and Debian patches),
and original Debian notices/common licenses. Extract a Debian source package with
`dpkg-source -x package_version.dsc` from the directory containing its companion
archives. Debian's `debian/rules`, patches and source metadata describe its build;
the application and plugin recipes are in `docker/Dockerfile` and `BUILD.md`.
To inspect the plugin, extract its upstream archive and the Cargo dependency
archive side by side; manifest paths in `cargo-artifacts.json` identify the
corresponding sources. Original version/revision pins are retained. The archives
are not a promise that rebuilding requires no toolchain or network access.

## Publishing and native binaries

The tag workflow builds and tests the runtime, exports the source companion,
compares binary hashes and Debian source versions to the tested image, publishes
the companion as GitHub release assets, then pushes the binary image. A failed
source build, mismatch or upload prevents that push. Existing releases are not
retroactively repaired by this change. The old snapshot in existing releases or
Git history still has unresolved provenance.
Source assets are not overwritten on reruns; use a new version tag for a changed
build rather than invalidating the sources associated with a previous binary.

For manual distribution, provide equivalent access to **all matching source
parts** at no additional charge and clear directions alongside the binary/image
download. Preserve that access for the required duration; an upstream package
mirror alone is not a source-availability commitment by this project. Do not
advertise a source bundle for a differently built image. The root Compose file
does not build or export sources.

Native binary distributors must package the same application notices and the
actual dependencies/source materials used in their build, along with
`build/config_validation/`. Docker's companion is not automatically correct for a
different native build. Review LGPL library replacement/relinking requirements if
changing to static linking, and installation-information obligations if shipping
in a GPLv3 User Product. The supplied container uses replaceable shared libraries.
Development images are separate distributions; do not publish one using only the
runtime manifest, because compilers and test tools add their own obligations.

This machinery preserves sources and notices; per-file license exceptions,
future dependency changes, patents and trademarks still need their own review.
It makes no ONVIF certification or endorsement claim.
