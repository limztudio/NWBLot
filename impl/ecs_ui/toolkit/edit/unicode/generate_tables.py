"""Regenerate pinned Unicode 17 grapheme properties."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent
SEP = "/" * 128
ENCODING = "utf-8"
LF = "\n"
CRLF = "\r\n"
HEX_RADIX = 16
MIN_RANGE_FIELDS = 2
MIN_INCB_FIELDS = 3
KIND_GRAPHEME = "gcb"
KIND_INDIC_CONJUNCT = "incb"
KIND_EMOJI = "emoji"


def write_cpp(path, text):
    path.write_bytes(text.replace(CRLF, LF).replace(LF, CRLF).encode(ENCODING))


def parse_ranges(data, kind):
    ranges = []
    for line in data.decode(ENCODING).splitlines():
        fields = [item.strip() for item in line.split("#", 1)[0].split(";")]
        if len(fields) < MIN_RANGE_FIELDS or not fields[0]:
            continue
        if kind == KIND_GRAPHEME:
            value = fields[1]
            if value in ("LV", "LVT"):
                continue
        elif kind == KIND_INDIC_CONJUNCT:
            if len(fields) < MIN_INCB_FIELDS or fields[1] != "InCB":
                continue
            value = fields[2]
        else:
            if fields[1] != "Extended_Pictographic":
                continue
            value = "Yes"
        span = fields[0].split("..")
        ranges.append((int(span[0], HEX_RADIX), int(span[-1], HEX_RADIX), value))
    merged = []
    for first, last, value in sorted(ranges):
        if merged and merged[-1][1] + 1 == first and merged[-1][2] == value:
            merged[-1] = (merged[-1][0], last, value)
        else:
            merged.append((first, last, value))
    return merged


def generate_table(kind, ranges):
    names = {KIND_GRAPHEME: ("GraphemeBreak", "LookupGraphemeBreak"), KIND_INDIC_CONJUNCT: ("IndicConjunct", "LookupIndicConjunct")}
    domain, function = names.get(kind, (None, "IsExtendedPictographic"))
    prefix = f"{domain}::" if domain else ""
    rows = LF.join(f"    {{ 0x{first:X}u, 0x{last:X}u, {prefix}{value if domain else '1u'} }}," for first, last, value in ranges)
    return_type = f"{domain}::Enum" if domain else "bool"
    result = f"static_cast<{return_type}>(property)" if domain else "property != 0u"
    default = f"{domain}::" + ("Other" if kind == KIND_GRAPHEME else "None") if domain else "0u"
    hangul = """    if(codePoint >= 0xAC00u && codePoint <= 0xD7A3u)
        return (codePoint - 0xAC00u) % 28u == 0u ? GraphemeBreak::LV : GraphemeBreak::LVT;
""" if kind == KIND_GRAPHEME else ""
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


{return_type} {function}(const u32 codePoint)noexcept{{
{hangul}    const u8 property = LookupUnicodePropertyRanges(__hidden_ui_unicode_{kind}::s_Ranges, codePoint, {default});
    return {result};
}}


{SEP}


NWB_IMPL_UI_END


{SEP}

"""
    write_cpp(HERE / f"{kind}_properties.cpp", output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, help="Use cached gcb/incb/emoji/license .txt files.")
    arguments = parser.parse_args()
    manifest = json.loads((HERE / "sources.json").read_text(encoding=ENCODING))
    payloads = {}
    for key, source in manifest.items():
        payload = (arguments.source_dir / f"{key}.txt").read_bytes() if arguments.source_dir else urlopen(source["url"]).read()
        if hashlib.sha256(payload).hexdigest() != source["sha256"]:
            raise ValueError(f"Pinned Unicode source changed: {key}")
        payloads[key] = payload
    for kind in (KIND_GRAPHEME, KIND_INDIC_CONJUNCT, KIND_EMOJI):
        ranges = parse_ranges(payloads[kind], kind)
        generate_table(kind, ranges)
        print(f"{kind}: {len(ranges)} ranges")
    (HERE / "LICENSE.txt").write_bytes(payloads["license"])


if __name__ == "__main__":
    main()
