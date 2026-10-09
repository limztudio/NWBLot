"""Regenerate the pinned Unicode 17 official grapheme conformance fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent
SEP = "/" * 128


def write_cpp(path, text):
    path.write_bytes(text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


def generate_cases(data):
    cases = []
    for line in data.decode("utf-8").splitlines():
        tokens = line.split("#", 1)[0].split()
        if not tokens:
            continue
        text, boundaries = bytearray(), []
        for token in tokens:
            if token == "\u00F7":
                boundaries.append(len(text))
            elif token != "\u00D7":
                text.extend(chr(int(token, 16)).encode("utf-8"))
        encoded = "".join(f"\\x{value:02X}" for value in text)
        positions = ",".join(str(value) for value in boundaries)
        cases.append(f'    {{ "{encoded}", "{positions}", {len(text)}u }},')
    for index, start in enumerate(range(0, len(cases), 500)):
        content = "\n".join(cases[start:start + 500])
        write_cpp(HERE / f"edit_grapheme_cases_{index}.inc",
                  f"// limztudio@gmail.com\n{SEP}\n\n\n"
                  f"// Generated Unicode 17.0.0 GraphemeBreakTest cases; see impl/ecs_ui/toolkit/edit/unicode/LICENSE.txt.\n{content}\n\n\n{SEP}\n\n")
    return len(cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, help="Use a cached conformance.txt file.")
    arguments = parser.parse_args()
    manifest = json.loads((HERE / "unicode_sources.json").read_text(encoding="utf-8"))
    source = manifest["conformance"]
    payload = (arguments.source_dir / "conformance.txt").read_bytes() if arguments.source_dir else urlopen(source["url"]).read()
    if hashlib.sha256(payload).hexdigest() != source["sha256"]:
        raise ValueError("Pinned Unicode source changed: conformance")
    print(f"Conformance: {generate_cases(payload)} cases")


if __name__ == "__main__":
    main()
