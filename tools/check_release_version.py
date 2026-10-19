"""Require a firmware release tag to match the version compiled into the image."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--platformio", type=Path, default=Path("platformio.ini"))
    args = parser.parse_args()

    tag_version = args.tag.removeprefix("v")
    if not tag_version or tag_version == args.tag:
        raise SystemExit("Release tag must begin with 'v'.")

    source = args.platformio.read_text(encoding="utf-8")
    match = re.search(
        r'RFLINK_FIRMWARE_VERSION=\'"([0-9]+(?:\.[0-9]+)+)"\'',
        source,
    )
    if match is None:
        raise SystemExit("RFLINK_FIRMWARE_VERSION is missing from platformio.ini.")
    if match.group(1) != tag_version:
        raise SystemExit(
            f"Release tag {args.tag} does not match firmware version {match.group(1)}."
        )

    print(f"release_version={tag_version}")


if __name__ == "__main__":
    main()
