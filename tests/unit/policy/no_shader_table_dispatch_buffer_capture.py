#!/usr/bin/env python3
"""Keep retired ShaderTable test facades out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_IDENTIFIERS = re.compile(
    r"\b(?:"
    r"captureDispatchBuffersForTesting"
    r"|rejectNextBufferAllocationForTesting"
    r"|rejectNextNewBufferMapForTesting"
    r"|m_rejectNextBufferAllocationForTesting"
    r"|m_rejectNextNewBufferMapForTesting"
    r")\b"
)


def find_shader_table_test_facade_references(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIERS)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_shader_table_test_facade_references,
            files_for=production_source_files,
            error_header="Production ShaderTable code must not expose retired test facades.",
            violation_format="{path}:{line}: retired ShaderTable test facade '{identifier}'",
            self_test_cases=(
                (
                    "capture call",
                    "table.captureDispatchBuffersForTesting(buffers);",
                    ((1, "captureDispatchBuffersForTesting"),),
                ),
                (
                    "capture declaration",
                    "void captureDispatchBuffersForTesting(Array<BufferHandle, 4u>& outBuffers)const;",
                    ((1, "captureDispatchBuffersForTesting"),),
                ),
                (
                    "multiple captures",
                    "table.captureDispatchBuffersForTesting(oldBuffers);\n"
                    "table.captureDispatchBuffersForTesting(newBuffers);",
                    (
                        (1, "captureDispatchBuffersForTesting"),
                        (2, "captureDispatchBuffersForTesting"),
                    ),
                ),
                (
                    "comments",
                    "// table.captureDispatchBuffersForTesting(buffers);\n"
                    "// table.rejectNextBufferAllocationForTesting();\n"
                    "// table.rejectNextNewBufferMapForTesting();\n"
                    "/* bool m_rejectNextBufferAllocationForTesting;\n"
                    "bool m_rejectNextNewBufferMapForTesting; */",
                    (),
                ),
                (
                    "literals",
                    'const char* capture = "captureDispatchBuffersForTesting";\n'
                    'const char* allocation = "rejectNextBufferAllocationForTesting";\n'
                    'const char* map = "rejectNextNewBufferMapForTesting";\n'
                    'const char* raw = R"tag(m_rejectNextBufferAllocationForTesting '
                    'm_rejectNextNewBufferMapForTesting)tag";',
                    (),
                ),
                (
                    "rejection calls",
                    "table.rejectNextBufferAllocationForTesting();\n"
                    "table.rejectNextNewBufferMapForTesting();",
                    (
                        (1, "rejectNextBufferAllocationForTesting"),
                        (2, "rejectNextNewBufferMapForTesting"),
                    ),
                ),
                (
                    "rejection fields",
                    "bool m_rejectNextBufferAllocationForTesting = false;\n"
                    "bool m_rejectNextNewBufferMapForTesting = false;",
                    (
                        (1, "m_rejectNextBufferAllocationForTesting"),
                        (2, "m_rejectNextNewBufferMapForTesting"),
                    ),
                ),
                (
                    "near names",
                    "void captureDispatchBuffersForTestingAgain();\n"
                    "void captureDispatchBufferForTesting();\n"
                    "void rejectNextBufferAllocationForTestingAgain();\n"
                    "void rejectNextNewBufferMapForTestingAgain();\n"
                    "bool m_rejectNextBufferAllocationForTestingAgain;\n"
                    "bool m_rejectNextNewBufferMapForTestingAgain;",
                    (),
                ),
            ),
        )
    )
