#!/usr/bin/env python3
"""Keep execution domains independent of the graphics runtime they serve."""
from __future__ import annotations

import posixpath
import re
import sys
from pathlib import Path

from policy_scan import REPOSITORY_ROOT, SOURCE_SUFFIXES, blank_non_code, line_number

INCLUDE = re.compile(r'^\s*#\s*include\b', re.MULTILINE)
OPERAND = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def forbidden(path: str, target: str) -> bool:
    if path.startswith('core/task/cpu/'):
        return target.startswith(('core/graphics/', 'core/task/gpu/', 'core/frame/'))
    if path.startswith('core/task/gpu/'):
        return target.startswith(('core/graphics/runtime/', 'core/frame/'))
    if path.startswith('core/graphics/') and not path.startswith('core/graphics/runtime/'):
        return target.startswith(('core/task/gpu/', 'core/graphics/runtime/', 'core/frame/'))
    return False


def violations(path: str, source: str) -> list[tuple[int, str]]:
    result = []
    code = blank_non_code(source)
    for match in INCLUDE.finditer(code):
        start = source.rfind('\n', 0, match.start()) + 1
        stop = source.find('\n', match.end())
        directive = OPERAND.match(source[start:None if stop < 0 else stop])
        if not directive:
            continue
        target = directive.group(1)
        if not target.startswith(('core/', 'global/', 'impl/', 'tests/')):
            target = posixpath.normpath(posixpath.join(posixpath.dirname(path), target))
        if forbidden(path, target):
            result.append((line_number(source, match.end()), target))
    return result


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == '--self-test':
        cases = (
            ('core/task/cpu/scheduler.h', '#include <core/alloc/general.h>', False),
            ('core/task/cpu/scheduler.h', '#include <core/graphics/api.h>', True),
            ('core/task/gpu/scheduler.h', '#include <core/graphics/rhi/device.h>', False),
            ('core/task/gpu/scheduler.h', '#include <core/graphics/runtime/runtime.h>', True),
            ('core/task/gpu/compiler.cpp', '#include "../../graphics/runtime/runtime.h"', True),
            ('core/graphics/vulkan/queue.cpp', '#include <core/task/gpu/scheduler.h>', True),
            ('core/graphics/runtime/runtime.h', '#include <core/task/gpu/scheduler.h>', False),
            ('core/task/cpu/scheduler.h', '/*\n#include <core/graphics/api.h>\n*/', False),
            ('core/task/cpu/scheduler.h', '// #include <core/graphics/api.h>', False),
        )
        for path, source, expected in cases:
            actual = bool(violations(path, source))
            if actual != expected:
                print(f'{path}: expected {expected}, got {actual}', file=sys.stderr)
                return 1
        return 0
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else REPOSITORY_ROOT
    errors = []
    for base in ('core/task', 'core/graphics'):
        for path in sorted((root / base).rglob('*')):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(root).as_posix()
            for line, target in violations(relative, path.read_text(encoding='utf-8')):
                errors.append(f'{relative}:{line}: forbidden execution-domain dependency on {target}')
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
