#!/usr/bin/env python3
"""Qualify decoded literal lifetime, object identity and compilation failure atomicity."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


NARROW_LITERAL = "NWB_LITERAL_ESCAPED_VIEW_ACROSS_TRANSLATION_UNITS_20261005"
UTF16_LITERAL = "NWB_LITERAL_UTF16_EMBEDDED\0\u00e9\U0001f642"
UTF32_LITERAL = "NWB_LITERAL_UTF32_EMBEDDED\0\U0001f642\U0010ffff"
SEPARATOR = "/" * 128
PREVIOUS_OBJECT = b"previous successful object must survive rejected compilation"


def unit_hash(values: Sequence[int]) -> int:
    result = 2166136261
    for value in values:
        result = ((result ^ value) * 16777619) & 0xFFFFFFFF
    return result


def source_file(path: Path, body: str) -> None:
    text = "// limztudio@gmail.com\n" + SEPARATOR + "\n\n\n" + body.strip() + "\n\n\n" + SEPARATOR + "\n\n"
    path.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def run(command: Sequence[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=45, check=False)


def require_success(result: subprocess.CompletedProcess, operation: str) -> None:
    if result.returncode:
        raise RuntimeError(f"{operation} returned {result.returncode}:\n{result.stdout}")


class LiteralQualification:
    def __init__(self, options: argparse.Namespace, directory: Path) -> None:
        self.options = options
        self.directory = directory
        self.compiler_wrapper = options.source_dir / "launcher/string_literals/compiler.py"
        self.linker_wrapper = options.source_dir / "launcher/string_literals/linker.py"
        self.windows = os.name == "nt"
        self.clang_cl = Path(options.compiler).stem.lower().endswith("clang-cl")
        self.target_flags = ["--target=" + options.target] if options.target else []
        self.optimization = {"dbg": "-O0", "opt": "-O2", "fin": "-O3"}[options.configuration]
        self.library = "msvcrtd" if options.configuration == "dbg" else "msvcrt"

    def compiler_command(self, source: Path, output: Path, is_c: bool = False) -> list[str]:
        compiler = self.options.c_compiler if is_c else self.options.compiler
        defines = ["PROP_" + self.options.configuration.upper()]
        defines.append("_DEBUG" if self.options.configuration == "dbg" else "NDEBUG")
        if self.clang_cl:
            arguments = [*self.target_flags, "/clang:" + self.optimization, "/MDd" if self.options.configuration == "dbg" else "/MD"]
            arguments.extend("/D" + define for define in defines)
            arguments.extend(["/I" + str(self.options.source_dir), "/TC" if is_c else "/std:c++latest", "/c", str(source), "/Fo" + str(output)])
        else:
            arguments = [*self.target_flags, self.optimization, "-std=c17" if is_c else "-std=c++2c"]
            arguments.extend("-D" + define for define in defines)
            if self.windows:
                arguments.extend(["-D_DLL", "-D_MT", "-Xclang", "--dependent-lib=" + self.library])
            arguments.extend(["-I", str(self.options.source_dir), "-c", str(source), "-o", str(output)])
        return [sys.executable, str(self.compiler_wrapper), "--llvm-library", str(self.options.llvm_library), "--llvm-version", self.options.llvm_version, "--", str(compiler), *arguments]

    def decoded_startup_and_escaped_lifetime(self) -> dict:
        narrow_hash = unit_hash(NARROW_LITERAL.encode("ascii"))
        utf16_bytes = UTF16_LITERAL.encode("utf-16-le")
        utf16_units = [int.from_bytes(utf16_bytes[index:index + 2], "little") for index in range(0, len(utf16_bytes), 2)]
        utf32_units = [ord(value) for value in UTF32_LITERAL]
        common = '''#include <global/type.h>


extern StringView LiteralB();
extern BasicStringView<char16_t> Literal16B();
extern BasicStringView<char32_t> Literal32B();
extern const char* NamedB();
extern char* MutableB();
extern "C" const char* LiteralC(void);
'''
        source_a = self.directory / "startup.cpp"
        source_b = self.directory / "escaped.cpp"
        source_c = self.directory / "interop.c"
        body_a = common + f'''

{SEPARATOR}


constexpr StringView s_Literal = "{NARROW_LITERAL}";
const char g_NamedA[] = "NWB_NAMED_IMMUTABLE_OBJECT_IDENTITY";
char g_MutableA[] = "NWB_MUTABLE_OBJECT_IDENTITY";
StringView g_EscapedView;
bool g_EarlyValid = false;

static_assert(s_Literal.size() == {len(NARROW_LITERAL)} && s_Literal.front() == 'N');


{SEPARATOR}


template<typename T>
u32 HashUnits(const BasicStringView<T> value){{
    u32 hash = 2166136261u;
    for(const T unit : value)
        hash = (hash ^ static_cast<u32>(unit)) * 16777619u;
    return hash;
}}

struct EarlyReader{{
    EarlyReader(){{
        g_EscapedView = LiteralB();
        const BasicStringView<char16_t> wide16 = Literal16B();
        const BasicStringView<char32_t> wide32 = Literal32B();
        g_EarlyValid = g_EscapedView.size() == {len(NARROW_LITERAL)}
            && HashUnits(g_EscapedView) == {narrow_hash}u
            && wide16.size() == {len(utf16_units)} && HashUnits(wide16) == {unit_hash(utf16_units)}u
            && wide32.size() == {len(utf32_units)} && HashUnits(wide32) == {unit_hash(utf32_units)}u;
    }}
}};

EarlyReader g_EarlyReader;


{SEPARATOR}


int main(){{
    if(!g_EarlyValid)
        return 10;
    const StringView other = LiteralB();
    if(s_Literal.data() != other.data() || other.data() != LiteralC())
        return 11;
    if(g_EscapedView.data() != other.data() || HashUnits(g_EscapedView) != {narrow_hash}u)
        return 12;
    if(other.data()[other.size()] != 0 || Literal16B().data()[Literal16B().size()] != 0)
        return 13;
    if(Literal32B().data()[Literal32B().size()] != 0 || Literal16B()[{UTF16_LITERAL.index(chr(0))}] != 0)
        return 14;
    if(Literal32B()[{UTF32_LITERAL.index(chr(0))}] != 0)
        return 15;
    if(g_NamedA == NamedB() || g_MutableA == MutableB())
        return 16;
    g_MutableA[0] = 'X';
    if(MutableB()[0] != 'N')
        return 17;
    return 0;
}}
'''
        body_b = f'''#include <global/type.h>


{SEPARATOR}


constexpr StringView s_Literal = "{NARROW_LITERAL}";
constexpr BasicStringView<char16_t> s_Literal16{{ u"NWB_LITERAL_UTF16_EMBEDDED\\0\\u00e9\\U0001f642", {len(utf16_units)} }};
constexpr BasicStringView<char32_t> s_Literal32{{ U"NWB_LITERAL_UTF32_EMBEDDED\\0\\U0001f642\\U0010ffff", {len(utf32_units)} }};
const char g_NamedB[] = "NWB_NAMED_IMMUTABLE_OBJECT_IDENTITY";
char g_MutableB[] = "NWB_MUTABLE_OBJECT_IDENTITY";


{SEPARATOR}


StringView LiteralB(){{ return s_Literal; }}
BasicStringView<char16_t> Literal16B(){{ return s_Literal16; }}
BasicStringView<char32_t> Literal32B(){{ return s_Literal32; }}
const char* NamedB(){{ return g_NamedB; }}
char* MutableB(){{ return g_MutableB; }}
'''
        source_file(source_a, body_a)
        source_file(source_b, body_b)
        source_file(source_c, f'const char* LiteralC(void){{ return "{NARROW_LITERAL}"; }}')
        suffix = ".obj" if self.windows else ".o"
        objects = [source_a.with_suffix(suffix), source_b.with_suffix(suffix), source_c.with_suffix(suffix)]
        for source, output, is_c in zip((source_a, source_b, source_c), objects, (False, False, True)):
            require_success(run(self.compiler_command(source, output, is_c), self.directory), f"Compile {source.name}")
        executable = self.directory / ("lifetime.exe" if self.windows else "lifetime")
        if self.clang_cl:
            link_arguments = [*self.target_flags, "/MDd" if self.options.configuration == "dbg" else "/MD", *map(str, objects), str(self.options.runtime_object), "/Fe" + str(executable)]
        else:
            link_arguments = [*self.target_flags, *map(str, objects), str(self.options.runtime_object), "-o", str(executable)]
            if self.windows:
                link_arguments.extend(["-Wl,/nodefaultlib:libcmt", "-Wl,/defaultlib:" + self.library])
        # The runtime is deliberately last: Mach-O needs the wrapper to place its constructor first.
        link_command = [sys.executable, str(self.linker_wrapper), "--runtime-object", str(self.options.runtime_object), "--", str(self.options.compiler), *link_arguments]
        require_success(run(link_command, self.directory), "Link startup fixture")
        image = executable.read_bytes()
        forbidden = [NARROW_LITERAL.encode("ascii")]
        forbidden.extend(UTF16_LITERAL.encode(encoding) for encoding in ("utf-16-le", "utf-16-be"))
        forbidden.extend(UTF32_LITERAL.encode(encoding) for encoding in ("utf-32-le", "utf-32-be"))
        if any(marker in image for marker in forbidden):
            raise RuntimeError("An eligible fixture literal remains plaintext in the linked image")
        require_success(run([str(executable)], self.directory), "Startup decode / escaped view / distinct object identity")
        return {"cross_translation_units": 3, "embedded_nul_widths": [16, 32], "image_bytes": len(image)}

    def failed_compilation_preserves_existing_object(self) -> dict:
        invalid = self.directory / "invalid.cpp"
        valid = self.directory / "valid.cpp"
        source_file(invalid, "int Broken( { return 0; }")
        source_file(valid, "int Valid(){ return 42; }")
        output = self.directory / ("previous.obj" if self.windows else "previous.o")
        base = self.compiler_command(valid, output)
        first_response = self.directory / "first.rsp"
        second_response = self.directory / "second.rsp"
        first_response.write_text('"@' + second_response.as_posix() + '"\n', encoding="utf-8")
        second_response.write_text('"@' + first_response.as_posix() + '"\n', encoding="utf-8")
        separator = base.index("--")
        recursive = [*base[:separator + 2], "@" + str(first_response)]
        mismatch = base.copy()
        mismatch[mismatch.index("--llvm-version") + 1] = "0.0.0"
        unsupported_lto = [*base, "/clang:-flto" if self.clang_cl else "-flto"]
        multiple_sources = [*base, str(invalid)]
        cases = (
            ("frontend", self.compiler_command(invalid, output), "error:"),
            ("llvm_version", mismatch, "differs from compiler"),
            ("recursive_response", recursive, "Recursive response file"),
            ("unsupported_lto", unsupported_lto, "Unsupported string-literal compilation mode"),
            ("multiple_sources", multiple_sources, "single C/C++ object compilation"),
        )
        for name, command, diagnostic in cases:
            output.write_bytes(PREVIOUS_OBJECT)
            result = run(command, self.directory)
            if not result.returncode or diagnostic not in result.stdout:
                raise RuntimeError(f"{name} did not reject the input:\n{result.stdout}")
            if output.read_bytes() != PREVIOUS_OBJECT:
                raise RuntimeError(f"{name} overwrote the previous successful object")
        require_success(run(base, self.directory), "Recovery after rejected compilation")
        if output.read_bytes() == PREVIOUS_OBJECT:
            raise RuntimeError("A valid recovery compilation did not publish its object")
        return {"preserved_output_failures": [name for name, _, _ in cases]}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--c-compiler", required=True, type=Path)
    parser.add_argument("--runtime-object", required=True, type=Path)
    parser.add_argument("--llvm-library", required=True, type=Path)
    parser.add_argument("--llvm-version", required=True)
    parser.add_argument("--configuration", required=True, choices=("dbg", "opt", "fin"))
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--target", default="")
    options = parser.parse_args(argv)
    for field in ("compiler", "c_compiler", "runtime_object", "llvm_library", "source_dir"):
        setattr(options, field, getattr(options, field).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="nwb-literal-edge-") as temporary:
        qualification = LiteralQualification(options, Path(temporary))
        report = qualification.decoded_startup_and_escaped_lifetime()
        report.update(qualification.failed_compilation_preserves_existing_object())
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
