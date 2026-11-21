This release uses GPL-3.0-only for ONVIF Server Emulator's original code.
Third-party libraries retain their respective licenses and notices.

The matching binary container image is published at
https://ghcr.io/abay11/onvif_server_emulator with this release's version tag.
Its corresponding-source companion is attached here at no additional charge.
Download SOURCE_README.md, SHA256SUMS and every source-bundle.tar.gz.part-* asset.
Verify with `sha256sum --check SHA256SUMS`, concatenate the ordered parts with
`cat source-bundle.tar.gz.part-* > source-bundle.tar.gz`, then extract that archive.

The bundle includes the application source, the metadata plugin and its Rust
dependencies, exact Debian source packages/patches, and original license notices.
See SOURCE_README.md for details. The program comes without warranty; commercial
use and redistribution are permitted subject to its licenses.
