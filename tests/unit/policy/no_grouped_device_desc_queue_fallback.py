#!/usr/bin/env python3
"""Keep the retired grouped Vulkan DeviceDesc queue fallback out of production sources."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from policy_scan import REPOSITORY_ROOT, blank_non_code, line_number, production_source_files
RETIRED_FIELDS_PATTERN = (
    r"graphicsQueue|computeQueue|transferQueue|graphicsQueueIndex|computeQueueIndex|transferQueueIndex|"
    r"asyncComputeLaneEnabled|transferQueueEnabled"
)
DEVICE_DESC_OPEN = re.compile(r"\bstruct\s+DeviceDesc\s*\{")
RETIRED_DEVICE_DESC_FIELD = re.compile(rf"\b(?:{RETIRED_FIELDS_PATTERN})\b")
RETIRED_MEMBER_ACCESS = re.compile(rf"\b(?:desc|deviceDesc)\s*\.\s*(?P<identifier>{RETIRED_FIELDS_PATTERN})\b")
RETIRED_FALLBACK_IDENTIFIER = re.compile(
    r"\b(?:legacyQueueFamilyCount|legacyQueueArena|legacyQueueFamilies|capabilitiesForLegacyQueue|"
    r"timestampValidBitsForLegacyQueue|legacyQueues)\b"
)


def device_desc_body_ranges(code: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for match in DEVICE_DESC_OPEN.finditer(code):
        open_offset = code.find("{", match.start(), match.end())
        depth = 0
        for offset in range(open_offset, len(code)):
            if code[offset] == "{":
                depth += 1
            elif code[offset] == "}":
                depth -= 1
                if depth == 0:
                    ranges.append((open_offset + 1, offset))
                    break
    return ranges


def find_grouped_device_desc_queue_references(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references: list[tuple[int, str]] = []
    for start, end in device_desc_body_ranges(code):
        for match in RETIRED_DEVICE_DESC_FIELD.finditer(code, start, end):
            references.append((line_number(code, match.start()), match.group()))
    for match in RETIRED_MEMBER_ACCESS.finditer(code):
        references.append((line_number(code, match.start("identifier")), match.group("identifier")))
    for match in RETIRED_FALLBACK_IDENTIFIER.finditer(code):
        references.append((line_number(code, match.start()), match.group()))
    return sorted(references)


def run_self_test() -> int:
    cases = (
        (
            "DeviceDesc fields",
            "struct DeviceDesc{ VkQueue graphicsQueue; i32 transferQueueIndex; bool asyncComputeLaneEnabled; };",
            ((1, "asyncComputeLaneEnabled"), (1, "graphicsQueue"), (1, "transferQueueIndex")),
        ),
        ("desc access", "queue = desc.computeQueue;", ((1, "computeQueue"),)),
        ("device desc access", "deviceDesc.transferQueueEnabled = true;", ((1, "transferQueueEnabled"),)),
        ("fallback local", "const VulkanPhysicalQueueDesc legacyQueues[] = {};", ((1, "legacyQueues"),)),
        ("fallback helper", "capabilitiesForLegacyQueue(family, queueClass);", ((1, "capabilitiesForLegacyQueue"),)),
        ("comment", "// deviceDesc.graphicsQueue = queue; legacyQueues", ()),
        ("literal", 'const char* text = "desc.computeQueue legacyQueueArena";', ()),
        (
            "other struct",
            "struct QueueState{ VkQueue graphicsQueue; bool transferQueueEnabled; };",
            (),
        ),
        (
            "near names",
            "struct DeviceDescState{ int graphicsQueueCount; }; deviceDesc.computeQueuePolicy = policy;",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_grouped_device_desc_queue_references(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    for path in production_source_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, identifier in find_grouped_device_desc_queue_references(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: retired grouped DeviceDesc queue seam '{identifier}'")

    if violations:
        print("Vulkan DeviceDesc must receive the required enumerated physical queue registry.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
