"""Regenerate the pinned Unicode 17 official grapheme conformance fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

LIT_UTF_8 = "utf-8"

LIT_CRLF = "\r\n"
LIT_LF = "\n"

HERE = Path(__file__).resolve().parent
SEP = "/" * 128
HEX_RADIX = 16
CASES_PER_CHUNK = 500


def write_source(path, text):
    path.write_bytes(text.replace(LIT_CRLF, LIT_LF).replace(LIT_LF, LIT_CRLF).encode(LIT_UTF_8))


def generate_cases(data):
    cases = []
    for line in data.decode(LIT_UTF_8).splitlines():
        tokens = line.split("#", 1)[0].split()
        if not tokens:
            continue
        text, boundaries = bytearray(), []
        for token in tokens:
            if token == "\u00F7":
                boundaries.append(len(text))
            elif token != "\u00D7":
                text.extend(chr(int(token, HEX_RADIX)).encode(LIT_UTF_8))
        encoded = "".join(f"\\x{value:02X}" for value in text)
        positions = ",".join(str(value) for value in boundaries)
        cases.append((encoded, positions, len(text)))
    write_cases(cases)
    return len(cases)


def write_cases(cases):
    lines = ["// limztudio@gmail.com", SEP, "", "", "// Generated Unicode 17.0.0 GraphemeBreakTest cases; see global/unicode/LICENSE.txt."]
    for group, start in enumerate(range(0, len(cases), CASES_PER_CHUNK)):
        rows = cases[start:start + CASES_PER_CHUNK]
        for name, column in [("Text", 0), ("Offsets", 1)]:
            values = [f'    StringView("{row[0]}", {row[2]}u)' if column == 0 else f'    "{row[1]}"' for row in rows]
            lines.append(f"const Array<StringView, {len(rows)}u> s_Grapheme{name}{group} = Array<StringView, {len(rows)}u>(")
            lines.append(",\n".join(values))
            lines.extend([");", ""])
    for name in ("Text", "Offsets"):
        lines.append(f"StringView GetUiGrapheme{name}(in usize index){{")
        for group, start in enumerate(range(0, len(cases), CASES_PER_CHUNK)):
            end = min(start + CASES_PER_CHUNK, len(cases))
            lines.extend([f"    if(index < {end}u)", f"        return s_Grapheme{name}{group}[index - {start}u];"])
        lines.extend(['    return "";', "}", ""])
    lines.extend(["", SEP, ""])
    write_source(HERE / "ui_grapheme_conformance_data.adhd", "\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, help="Use a cached conformance.txt file.")
    arguments = parser.parse_args()
    manifest = json.loads((HERE / "unicode_sources.json").read_text(encoding=LIT_UTF_8))
    source = manifest["conformance"]
    payload = (arguments.source_dir / "conformance.txt").read_bytes() if arguments.source_dir else urlopen(source["url"]).read()
    if hashlib.sha256(payload).hexdigest() != source["sha256"]:
        raise ValueError("Pinned Unicode source changed: conformance")
    print(f"Conformance: {generate_cases(payload)} cases")


if __name__ == "__main__":
    main()
