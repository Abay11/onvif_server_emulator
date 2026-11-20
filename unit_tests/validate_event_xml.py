#!/usr/bin/env python3
"""Validate CTest-generated Stage 2 XML using the Stage 1 pinned schema cache."""
import argparse
from pathlib import Path
import sys

from lxml import etree

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/rtsp_metadata"))
from contract import load_schema

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--contract", type=Path, required=True)
parser.add_argument("--artifacts", type=Path, required=True)
args = parser.parse_args()
schema = load_schema(args.contract, resource_faults=True)
for filename in ("stage2_metadata.xml", "stage2_metadata_empty.xml",
                 "ResourceUnknownFault.xml", "UnableToGetMessagesFault.xml"):
    root = etree.parse(str(args.artifacts / filename))
    schema.assertValid(root)
    detail = root.find(".//{http://www.w3.org/2003/05/soap-envelope}Detail")
    if detail is not None:
        for fault in detail:
            schema.assertValid(fault)
    print("PASS pinned XSD:", filename)
