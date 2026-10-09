#!/usr/bin/env python3
"""Combine disjoint authored fixture roots into one private project cook root."""

import argparse
import hashlib
from pathlib import Path


def prepare_asset_root(output_root, source_roots):
    output = output_root.resolve()
    sources = [source.resolve() for source in source_roots]
    if output == Path(output.anchor):
        raise ValueError("output root must not be a filesystem root")
    for source in sources:
        if not source.is_dir():
            raise ValueError(f"source root is not an existing directory: {source}")
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise ValueError("output root must be separate from every authored source root")
    for index, source in enumerate(sources):
        for previous in sources[:index]:
            if source.is_relative_to(previous) or previous.is_relative_to(source):
                raise ValueError("source roots must be separate and must not overlap")

    contents = {}
    identities = {}
    for source in sources:
        for path in sorted(source.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"fixture source must not contain symbolic links: {path}")
            if not path.is_file():
                continue
            relative = path.relative_to(source).as_posix()
            identity = relative.casefold()
            if identity in identities:
                raise ValueError(f"fixture asset identity collision: {relative} and {identities[identity]}")
            identities[identity] = relative
            contents[relative] = path.read_bytes()

    existing = set()
    for path in output.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"private output must not contain symbolic links: {path}")
        if path.is_file():
            existing.add(path.relative_to(output).as_posix())
    unexpected = sorted(existing - contents.keys())
    if unexpected:
        raise ValueError("unexpected existing private output files: " + ", ".join(unexpected))

    output.mkdir(parents=True, exist_ok=True)
    for relative, payload in contents.items():
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)

    actual = {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()}
    if actual != contents.keys():
        raise ValueError("private output file identities changed while writing")
    mismatched = [
        relative for relative, payload in contents.items()
        if hashlib.sha256((output / relative).read_bytes()).digest() != hashlib.sha256(payload).digest()
    ]
    if mismatched:
        raise ValueError("private output payload verification failed: " + ", ".join(mismatched))
    return len(contents)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, action="append", required=True)
    args = parser.parse_args(argv)
    try:
        count = prepare_asset_root(args.output_root, args.source_root)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Prepared {count} fixture asset files in {args.output_root.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
