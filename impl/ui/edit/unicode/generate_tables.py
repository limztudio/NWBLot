"""Regenerate pinned Unicode 17 grapheme properties and the official conformance fixture."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
SEP = "/" * 128


def write_cpp(path, text):
    path.write_bytes(text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


def parse_ranges(data, kind):
    ranges = []
    for line in data.decode("utf-8").splitlines():
        fields = [item.strip() for item in line.split("#", 1)[0].split(";")]
        if len(fields) < 2 or not fields[0]:
            continue
        if kind == "gcb":
            value = fields[1]
            if value in ("LV", "LVT"):
                continue
        elif kind == "incb":
            if len(fields) < 3 or fields[1] != "InCB":
                continue
            value = fields[2]
        else:
            if fields[1] != "Extended_Pictographic":
                continue
            value = "Yes"
        span = fields[0].split("..")
        ranges.append((int(span[0], 16), int(span[-1], 16), value))
    merged = []
    for first, last, value in sorted(ranges):
        if merged and merged[-1][1] + 1 == first and merged[-1][2] == value:
            merged[-1] = (merged[-1][0], last, value)
        else:
            merged.append((first, last, value))
    return merged


def generate_table(kind, ranges):
    names = {"gcb": ("GraphemeBreak", "LookupGraphemeBreak"), "incb": ("IndicConjunct", "LookupIndicConjunct")}
    domain, function = names.get(kind, (None, "IsExtendedPictographic"))
    prefix = f"{domain}::" if domain else ""
    rows = "\n".join(f"    {{ 0x{first:X}u, 0x{last:X}u, {prefix}{value if domain else '1u'} }},"
                     for first, last, value in ranges)
    return_type = f"{domain}::Enum" if domain else "bool"
    result = f"static_cast<{return_type}>(property)" if domain else "property != 0u"
    default = f"{domain}::" + ("Other" if kind == "gcb" else "None") if domain else "0u"
    hangul = """    if(codePoint >= 0xAC00u && codePoint <= 0xD7A3u)
        return (codePoint - 0xAC00u) % 28u == 0u ? GraphemeBreak::LV : GraphemeBreak::LVT;
""" if kind == "gcb" else ""
    output = f"""// limztudio@gmail.com
{SEP}


#include "properties.h"
#include "property_table.h"


{SEP}


NWB_IMPL_UI_BEGIN


{SEP}


namespace __hidden_ui_unicode_{kind}{{


{SEP}


// Generated from the pinned Unicode 17.0.0 data in sources.json; see LICENSE.txt.
static constexpr UnicodePropertyRange s_Ranges[] = {{
{rows}
}};


{SEP}


}};


{SEP}


{return_type} {function}(const u32 codePoint){{
{hangul}    const u8 property = LookupUnicodePropertyRanges(__hidden_ui_unicode_{kind}::s_Ranges, codePoint, {default});
    return {result};
}}


{SEP}


NWB_IMPL_UI_END


{SEP}

"""
    write_cpp(HERE / f"{kind}_properties.cpp", output)


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
        write_cpp(ROOT / "tests/unit/ui" / f"edit_grapheme_cases_{index}.inc",
                  f"// limztudio@gmail.com\n{SEP}\n\n\n"
                  f"// Generated Unicode 17.0.0 GraphemeBreakTest cases; see impl/ui/edit/unicode/LICENSE.txt.\n{content}\n\n\n{SEP}\n\n")
    return len(cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, help="Use cached gcb/incb/emoji/conformance/license .txt files.")
    arguments = parser.parse_args()
    manifest = json.loads((HERE / "sources.json").read_text(encoding="utf-8"))
    payloads = {}
    for key, source in manifest.items():
        payload = (arguments.source_dir / f"{key}.txt").read_bytes() if arguments.source_dir else urlopen(source["url"]).read()
        if hashlib.sha256(payload).hexdigest() != source["sha256"]:
            raise ValueError(f"Pinned Unicode source changed: {key}")
        payloads[key] = payload
    for kind in ("gcb", "incb", "emoji"):
        ranges = parse_ranges(payloads[kind], kind)
        generate_table(kind, ranges)
        print(f"{kind}: {len(ranges)} ranges")
    print(f"Conformance: {generate_cases(payloads['conformance'])} cases")
    (HERE / "LICENSE.txt").write_bytes(payloads["license"])


if __name__ == "__main__":
    main()
