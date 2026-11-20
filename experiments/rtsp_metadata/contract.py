#!/usr/bin/env python3
"""Prepare pinned schemas outside the checkout; validate without network access."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from urllib.request import Request, urlopen

COMMIT = "d7f3684501c4a6d58f879e72e1e3322c120f95e6"
LOCK = Path(__file__).with_name("imports.lock.json")


def verify_repository(root):
    actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if actual != COMMIT:
        raise ValueError(f"Expected ONVIF 24.12 commit {COMMIT}, found {actual}")
    subprocess.run(["git", "-C", str(root), "diff", "--exit-code", "HEAD", "--", "wsdl"], check=True)


def checked_imports(destination):
    result = {}
    for canonical, item in json.loads(LOCK.read_text()).items():
        path = destination / "external" / item["file"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError(f"Schema hash mismatch: {canonical}")
        result[canonical] = path
        result[item["url"]] = path
    return result


def prepare(destination):
    destination.mkdir(parents=True, exist_ok=True)
    root = destination / "specs"
    if not root.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--branch", "24.12",
                        "https://github.com/onvif/specs.git", str(root)], check=True)
    verify_repository(root)
    external = destination / "external"
    external.mkdir(exist_ok=True)
    for item in json.loads(LOCK.read_text()).values():
        path = external / item["file"]
        if not path.exists():
            request = Request(item["url"], headers={"User-Agent": "osrv-metadata-stage1/1.0"})
            with urlopen(request, timeout=30) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != item["sha256"]:
                raise ValueError(f"Downloaded schema hash mismatch: {item['url']}")
            path.write_bytes(data)
    checked_imports(destination)
    print(f"Prepared ONVIF 24.12 schemas at {destination}")


def load_schema(destination, resource_faults=False):
    from lxml import etree
    root = destination / "specs"
    verify_repository(root)
    imports = checked_imports(destination)

    class Resolver(etree.Resolver):
        def resolve(self, url, public_id, context):
            if url in imports:
                return self.resolve_string(imports[url].read_bytes(), context, base_url=url)
            if url.startswith(("https:", "http:")):
                raise ValueError(f"Unpinned external schema: {url}")
            return None

    parser = etree.XMLParser(no_network=True, resolve_entities=False)
    parser.resolvers.add(Resolver())
    metadata_path = (root / "wsdl/ver10/schema/metadatastream.xsd").resolve()
    if not resource_faults:
        return etree.XMLSchema(etree.parse(str(metadata_path), parser))
    # Independently authored validation entry point; upstream files remain intact.
    wrapper = etree.Element("{http://www.w3.org/2001/XMLSchema}schema")
    for namespace, location in [
        ("http://www.onvif.org/ver10/schema", metadata_path.as_uri()),
        ("http://docs.oasis-open.org/wsrf/r-2", "http://docs.oasis-open.org/wsrf/r-2.xsd"),
        ("http://www.w3.org/2003/05/soap-envelope", "https://www.w3.org/2003/05/soap-envelope"),
    ]:
        etree.SubElement(wrapper, "{http://www.w3.org/2001/XMLSchema}import",
                         namespace=namespace, schemaLocation=location)
    return etree.XMLSchema(etree.fromstring(etree.tostring(wrapper), parser))


def load_media_schema(destination, media2=False):
    """Validate the pinned inline schema using XSD 1.1 element/wildcard priority.

    libxml2 rejects the upstream VideoSourceConfigurationOptionsExtension2 model
    under XSD 1.0 UPA rules. No upstream document is modified to work around it.
    Requires the optional, validation-only xmlschema package (tested with 3.4.5).
    """
    from lxml import etree
    from xmlschema import XMLSchema11
    root = destination / "specs"
    verify_repository(root)
    imports = checked_imports(destination)
    path = (root / ("wsdl/ver20/media/wsdl/media.wsdl" if media2 else
                    "wsdl/ver10/media/wsdl/media.wsdl")).resolve()
    wsdl = etree.parse(str(path), etree.XMLParser(no_network=True, resolve_entities=False))
    schema = wsdl.find(".//{http://www.w3.org/2001/XMLSchema}schema")
    return XMLSchema11(etree.tostring(schema), base_url=path.parent.as_uri() + "/",
                       uri_mapper={url: str(file.resolve()) for url, file in imports.items()},
                       allow="local", use_fallback=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--prepare", action="store_true")
    args = parser.parse_args()
    if args.prepare:
        prepare(args.destination)
    else:
        load_schema(args.destination)
        print("PASS pinned metadata schema and imports compile offline")
