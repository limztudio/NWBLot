#!/usr/bin/env python3
import argparse
from pathlib import Path
import re


VERTEX_REF_PATTERN = re.compile(r"^(\s*)\[(.*)\](,?)\s*$")


def count_tokens(line, open_token, close_token):
    return line.count(open_token) - line.count(close_token)


def skip_block(lines, index, open_token, close_token):
    depth = 0
    while index < len(lines):
        depth += count_tokens(lines[index], open_token, close_token)
        index += 1
        if depth <= 0:
            return index
    raise RuntimeError("unterminated block while converting skinned mesh metadata")


def convert_static_mesh(input_path, output_path):
    lines = input_path.read_text(encoding="utf-8").splitlines()
    converted = []
    index = 0
    in_vertex_refs = False

    while index < len(lines):
        stripped = lines[index].strip()

        if index == 0 and stripped == "skinned_mesh asset;":
            converted.append("mesh asset;")
            index += 1
            continue

        if stripped.startswith("asset.skin = {"):
            index = skip_block(lines, index, "{", "}")
            continue

        if stripped.startswith("asset.skeleton_joint_count ="):
            index += 1
            continue

        if stripped.startswith("asset.inverse_bind_matrices = ["):
            index = skip_block(lines, index, "[", "]")
            continue

        if stripped == "asset.vertex_refs = [":
            in_vertex_refs = True
            converted.append(lines[index])
            index += 1
            continue

        if in_vertex_refs:
            if stripped == "];":
                in_vertex_refs = False
                converted.append(lines[index])
                index += 1
                continue

            match = VERTEX_REF_PATTERN.match(lines[index])
            if match:
                values = [value.strip() for value in match.group(2).split(",")]
                if len(values) == 6:
                    converted.append(f"{match.group(1)}[{', '.join(values[:5])}]{match.group(3)}")
                    index += 1
                    continue

        converted.append(lines[index])
        index += 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\r\n".join(converted) + "\r\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Generate a static mesh metadata file from a skinned mesh metadata file.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    convert_static_mesh(args.input, args.output)


if __name__ == "__main__":
    main()
