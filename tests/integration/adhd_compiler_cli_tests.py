#!/usr/bin/env python3

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time


def ninja_escape(path: pathlib.Path) -> str:
    return path.as_posix().replace("$", "$$").replace(" ", "$ ").replace(":", "$:")


def run_cpp_import_cli_tests(compiler: pathlib.Path, root: pathlib.Path, ninja: str | None) -> None:
    cpp_root = root / "cpp_import_cli"
    cpp_inc = cpp_root / "inc"
    sdk_dir = cpp_inc / "sdk"
    cpp_src = cpp_root / "src"
    cpp_out = cpp_root / "out"
    sdk_dir.mkdir(parents=True)
    cpp_src.mkdir(parents=True)

    detail_header = sdk_dir / "widget_detail.h"
    detail_header.write_text(
        "#pragma once\n"
        "namespace CliDemo{\n"
        "enum class Mode : unsigned int{\n"
        "    Off = 0u,\n"
        "    On = 1u\n"
        "};\n"
        "}\n",
        encoding="utf-8",
    )
    widget_header = sdk_dir / "widget.h"
    widget_header.write_text(
        "#pragma once\n"
        '#include "widget_detail.h"\n'
        "#if defined(NWB_CLI_ENABLE_WIDGET) && defined(NWB_CLI_EXTRA_FLAG) && (NWB_CLI_SCALE == 3)\n"
        "namespace CliDemo{\n"
        "class Widget{\n"
        "public:\n"
        "    unsigned int count() const;\n"
        "    Mode mode() const;\n"
        "};\n"
        "inline unsigned int ComputeValue(const unsigned int x){ return x * NWB_CLI_SCALE; }\n"
        "}\n"
        "#endif\n",
        encoding="utf-8",
    )

    consumer = cpp_src / "consumer.adhd"
    consumer.write_text(
        "u32 ExerciseCliImport(in Widget widget){\n"
        "    const Mode m = widget.mode();\n"
        "    const u32 c = widget.count();\n"
        "    if(m == Mode::On)\n"
        "        return ComputeValue(c);\n"
        "    return 0u;\n"
        "}\n",
        encoding="utf-8",
    )
    cpp_depfile = cpp_root / "consumer.d"
    emitted_externs = cpp_root / "emitted.extern.adhd"

    command = [
        str(compiler),
        "--input",
        str(consumer),
        "--output",
        str(cpp_out),
        "--depfile",
        str(cpp_depfile),
        "--cpp-header",
        "sdk/widget.h",
        "--cpp-include-dir",
        str(cpp_inc),
        "--cpp-define",
        "NWB_CLI_ENABLE_WIDGET=1",
        "--cpp-arg",
        "-DNWB_CLI_EXTRA_FLAG",
        "--cpp-arg",
        "-DNWB_CLI_SCALE=3",
        "--emit-cpp-externs",
        str(emitted_externs),
    ]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
    if result.returncode != 0:
        raise AssertionError(f"--cpp-header compilation failed: {result.stdout}\n{result.stderr}")

    gen_header = (cpp_out / "consumer.adhd.h").read_text(encoding="utf-8")
    gen_source = (cpp_out / "consumer.adhd.cpp").read_text(encoding="utf-8")
    if "#include <sdk/widget.h>" not in gen_header or "::CliDemo::Widget" not in gen_header:
        raise AssertionError(f"generated header missing imported C++ includes/types:\n{gen_header}")
    if "::CliDemo::Mode::On" not in gen_source or "::CliDemo::ComputeValue" not in gen_source:
        raise AssertionError(f"generated source missing imported C++ symbols:\n{gen_source}")

    externs_text = emitted_externs.read_text(encoding="utf-8")
    if 'cpp_header("sdk/widget.h")' not in externs_text or 'extern "' in externs_text:
        raise AssertionError(f"unexpected emitted externs content:\n{externs_text}")
    if "ComputeValue" not in externs_text or "Widget" not in externs_text:
        raise AssertionError(f"emitted externs missing imported declarations:\n{externs_text}")

    dep_text = cpp_depfile.read_text(encoding="utf-8").replace("\\", "/").replace("/:/", ":/")
    if widget_header.as_posix().lower() not in dep_text.lower() or detail_header.as_posix().lower() not in dep_text.lower():
        raise AssertionError(f"depfile missing imported C++ headers:\n{dep_text}")

    if ninja:
        generated = (cpp_out / "consumer.adhd.h", cpp_out / "consumer.adhd.cpp")
        for path in generated:
            path.unlink()
        touch = "; ".join(f"open(r'{path.as_posix()}', 'a').close()" for path in generated)
        command_text = f'"{sys.executable}" -c "{touch}"'.replace("$", "$$")
        manifest = cpp_root / "build_cpp_dep.ninja"
        manifest.write_text(
            f"rule generate\n  command = {command_text}\n  depfile = {ninja_escape(cpp_depfile)}\n  deps = gcc\n"
            f"build {' '.join(ninja_escape(path) for path in generated)}: generate\n",
            encoding="utf-8",
        )
        result = subprocess.run([ninja, "-f", str(manifest)], cwd=cpp_root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode != 0:
            raise AssertionError(f"Ninja failed on C++ depfile manifest: {result.stdout}\n{result.stderr}")
        result = subprocess.run([ninja, "-f", str(manifest), "-t", "deps", str(generated[0])], cwd=cpp_root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        deps_out = result.stdout.replace("\\", "/").lower() if os.name == "nt" else result.stdout
        expected = (consumer, widget_header, detail_header)
        if result.returncode != 0 or any((p.as_posix().lower() if os.name == "nt" else p.as_posix()) not in deps_out for p in expected):
            raise AssertionError(f"Ninja -t deps missing imported C++ header deps:\n{result.stdout}\n{result.stderr}")
        timestamp = generated[0].stat().st_mtime_ns + 2_000_000_000
        os.utime(detail_header, ns=(timestamp, timestamp))
        result = subprocess.run([ninja, "-f", str(manifest)], cwd=cpp_root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode != 0 or "[1/1]" not in result.stdout:
            raise AssertionError(f"Ninja did not rebuild after transitive C++ header changed: {result.stdout}\n{result.stderr}")

    standalone = cpp_src / "standalone.adhd"
    standalone.write_text(
        '#include "emitted.extern.adhd"\n'
        "u32 UseEmitted(in Widget w){\n"
        "    if(w.mode() == Mode::Off)\n"
        "        return 0u;\n"
        "    return ComputeValue(w.count());\n"
        "}\n",
        encoding="utf-8",
    )
    standalone_out = cpp_root / "standalone_out"
    result = subprocess.run(
        [str(compiler), "--input", str(standalone), "--output", str(standalone_out), "--include-dir", str(cpp_root)],
        cwd=root,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=30,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(f"standalone compile with emitted externs failed: {result.stdout}\n{result.stderr}")
    standalone_hdr = (standalone_out / "standalone.adhd.h").read_text(encoding="utf-8")
    standalone_cpp = (standalone_out / "standalone.adhd.cpp").read_text(encoding="utf-8")
    if "#include <sdk/widget.h>" not in standalone_hdr or "::CliDemo::ComputeValue" not in (standalone_hdr + standalone_cpp):
        raise AssertionError("standalone compile did not emit C++ include/call from emitted externs")

    err_other = cpp_src / "other.adhd"
    err_other.write_text("", encoding="utf-8")
    err_out = cpp_root / "err_out"
    err_out.mkdir()
    sentinel_hdr = err_out / "consumer.adhd.h"
    sentinel_cpp = err_out / "consumer.adhd.cpp"
    sentinel_ext = cpp_root / "sentinel.extern.adhd"
    sentinel_hdr.write_text("hdr sentinel", encoding="utf-8")
    sentinel_cpp.write_text("cpp sentinel", encoding="utf-8")
    sentinel_ext.write_text("ext sentinel", encoding="utf-8")

    multi_emit = subprocess.run(
        [str(compiler), "--input", str(consumer), "--input", str(err_other), "--output", str(err_out), "--emit-cpp-externs", str(sentinel_ext)],
        cwd=root,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=30,
        check=False,
    )
    if multi_emit.returncode == 0 or "single input file" not in multi_emit.stdout + multi_emit.stderr:
        raise AssertionError(f"--emit-cpp-externs with multiple inputs did not fail cleanly: {multi_emit.stdout}\n{multi_emit.stderr}")
    if sentinel_hdr.read_text(encoding="utf-8") != "hdr sentinel" or sentinel_cpp.read_text(encoding="utf-8") != "cpp sentinel" or sentinel_ext.read_text(encoding="utf-8") != "ext sentinel":
        raise AssertionError("--emit-cpp-externs error mutated sentinel files")

    missing_hdr = subprocess.run(
        [str(compiler), "--input", str(err_other), "--output", str(err_out), "--cpp-header", "nonexistent/missing.h"],
        cwd=root,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=30,
        check=False,
    )
    if missing_hdr.returncode == 0 or sentinel_hdr.read_text(encoding="utf-8") != "hdr sentinel" or sentinel_cpp.read_text(encoding="utf-8") != "cpp sentinel":
        raise AssertionError(f"missing --cpp-header did not fail without touching sentinels: {missing_hdr.stdout}\n{missing_hdr.stderr}")


def main() -> None:
    compiler = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="nwb_adhd_include_") as name:
        root = pathlib.Path(name)
        source_dir = root / "source"
        include_dir = root / "include"
        output_dir = root / "output"
        source_dir.mkdir()
        include_dir.mkdir()
        source = source_dir / "input.adhd"
        source.write_text('#include "shared.adhd"\n', encoding="utf-8")
        (include_dir / "shared.adhd").write_text("", encoding="utf-8")
        command = [str(compiler), "--input", str(source), "--output", str(output_dir), "--include-dir", str(include_dir)]

        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode != 0:
            raise AssertionError(f"missing include did not fall back: {result.stdout}\n{result.stderr}")

        header = output_dir / "input.adhd.h"
        generated = output_dir / "input.adhd.cpp"
        header.write_text("header sentinel", encoding="utf-8")
        generated.write_text("source sentinel", encoding="utf-8")
        (source_dir / "shared.adhd").mkdir()
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode == 0 or "failed to read include" not in result.stdout + result.stderr:
            raise AssertionError(f"unreadable include did not fail: {result.stdout}\n{result.stderr}")
        if header.read_text(encoding="utf-8") != "header sentinel" or generated.read_text(encoding="utf-8") != "source sentinel":
            raise AssertionError("failed include changed existing generated output")

        first_dir = root / "first"
        second_dir = root / "second"
        first_dir.mkdir()
        second_dir.mkdir()
        first = first_dir / "duplicate.adhd"
        first.write_text("", encoding="utf-8")
        output_header = output_dir / "duplicate.adhd.h"
        output_source = output_dir / "duplicate.adhd.cpp"
        output_header.write_text("header sentinel", encoding="utf-8")
        output_source.write_text("source sentinel", encoding="utf-8")
        variants = ("duplicate.adhd", "DUPLICATE.adhd") if sys.platform == "win32" else ("duplicate.adhd",)
        for variant in variants:
            second = second_dir / variant
            second.write_text("", encoding="utf-8")
            command = [str(compiler), "--input", str(first), "--input", str(second), "--output", str(output_dir)]
            result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
            if result.returncode == 0 or "same output name" not in result.stdout + result.stderr:
                raise AssertionError(f"colliding outputs did not fail: {result.stdout}\n{result.stderr}")
            if output_header.read_text(encoding="utf-8") != "header sentinel" or output_source.read_text(encoding="utf-8") != "source sentinel":
                raise AssertionError("colliding inputs changed existing generated output")

        absent_output = root / "not_created"
        command = [str(compiler), "--input", str(first), "--input", str(second), "--output", str(absent_output)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode == 0 or absent_output.exists():
            raise AssertionError("colliding inputs created an output directory")

        for depfile in (first, output_header):
            command = [str(compiler), "--input", str(first), "--output", str(output_dir), "--depfile", str(depfile)]
            result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
            if result.returncode == 0 or first.read_text(encoding="utf-8") != "" or output_header.read_text(encoding="utf-8") != "header sentinel":
                raise AssertionError(f"depfile collision damaged input or output: {result.stdout}\n{result.stderr}")

        if sys.platform == "win32":
            drive_relative = first.drive + os.path.relpath(first, root)
            drive_rooted = str(first)[len(first.drive):]
            for depfile in (drive_relative, drive_rooted):
                command = [str(compiler), "--input", str(first), "--output", str(output_dir), "--depfile", depfile]
                result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                if result.returncode == 0 or first.read_text(encoding="utf-8") != "" or output_header.read_text(encoding="utf-8") != "header sentinel":
                    raise AssertionError(f"Windows path collision damaged input or output: {result.stdout}\n{result.stderr}")

        command = [str(compiler), "--input", str(first), "--input", str(output_header), "--output", str(output_dir)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode == 0 or output_header.read_text(encoding="utf-8") != "header sentinel":
            raise AssertionError(f"output collision damaged a later input: {result.stdout}\n{result.stderr}")

        include_source = root / "include_source"
        include_source.mkdir()
        main_source = include_source / "with_include.adhd"
        included = include_source / "shared.adhd"
        main_source.write_text('#include "shared.adhd"\n', encoding="utf-8")
        included.write_text("", encoding="utf-8")
        command = [str(compiler), "--input", str(main_source), "--output", str(output_dir), "--depfile", str(included)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode == 0 or included.read_text(encoding="utf-8") != "":
            raise AssertionError(f"depfile collision damaged an included source: {result.stdout}\n{result.stderr}")

        if sys.platform == "win32":
            source_forms = (main_source.drive + os.path.relpath(main_source, root), str(main_source)[len(main_source.drive):])
            for source_form in source_forms:
                command = [str(compiler), "--input", source_form, "--output", str(output_dir)]
                result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                if result.returncode != 0:
                    raise AssertionError(f"Windows source path failed to resolve its include: {result.stdout}\n{result.stderr}")

        invalid = second_dir / "invalid.adhd"
        invalid.write_text("class {", encoding="utf-8")
        command = [str(compiler), "--input", str(first), "--input", str(invalid), "--output", str(output_dir)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode == 0 or output_header.read_text(encoding="utf-8") != "header sentinel" or output_source.read_text(encoding="utf-8") != "source sentinel":
            raise AssertionError(f"later compile failure changed earlier output: {result.stdout}\n{result.stderr}")

        distinct = second_dir / "distinct.adhd"
        distinct.write_text("", encoding="utf-8")
        output_header.write_text("header sentinel", encoding="utf-8")
        output_source.write_text("source sentinel", encoding="utf-8")
        blocked_source_output = output_dir / "distinct.adhd.cpp"
        blocked_source_output.mkdir()
        command = [str(compiler), "--input", str(first), "--input", str(distinct), "--output", str(output_dir)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode == 0 or output_header.read_text(encoding="utf-8") != "header sentinel" or output_source.read_text(encoding="utf-8") != "source sentinel" or (output_dir / "distinct.adhd.h").exists():
            raise AssertionError(f"batch output write failure mutated earlier outputs: {result.stdout}\n{result.stderr}")
        blocked_source_output.rmdir()

        command = [str(compiler), "--input", str(first), "--input", str(distinct), "--output", str(output_dir)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode != 0 or not (output_dir / "distinct.adhd.h").is_file():
            raise AssertionError(f"distinct outputs did not succeed: {result.stdout}\n{result.stderr}")

        hardlink_dep = root / "hardlink_dep.d"
        try:
            os.link(first, hardlink_dep)
        except OSError:
            hardlink_dep = None
        if hardlink_dep is not None:
            command = [str(compiler), "--input", str(first), "--output", str(output_dir), "--depfile", str(hardlink_dep)]
            result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
            if result.returncode == 0:
                raise AssertionError("hardlinked depfile collision with input did not fail")
            hardlink_dep.unlink()

        ns_root = root / "ns_root"
        ns_sub = ns_root / "sub_pkg"
        ns_out = root / "ns_out"
        ns_sub.mkdir(parents=True)
        ns_input = ns_sub / "widget.adhd"
        ns_input.write_text("const u32 s_Value = 1u;\n", encoding="utf-8")
        rel_include_dir = os.path.relpath(ns_root, root)
        if sys.platform == "win32":
            rel_include_dir = rel_include_dir.upper()
        command = [str(compiler), "--input", str(ns_input), "--output", str(ns_out), "--include-dir", rel_include_dir]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        if result.returncode != 0 or "namespace NWB::SubPkg" not in (ns_out / "widget.adhd.h").read_text(encoding="utf-8"):
            raise AssertionError(f"relative/case-variant include-dir did not derive namespace: {result.stdout}\n{result.stderr}")

        ninja = shutil.which("ninja")
        if ninja:
            special_name = "source #$ space" if os.name == "nt" else "source #$ " + "\\ " + "\\" * 2 + " " + "\\" * 3 + " back\\slash:colon"
            special = root / special_name
            special.mkdir()
            dep_input = special / "input.adhd"
            dep_include = special / "shared #$.adhd"
            dep_input.write_text('#include "shared #$.adhd"\n', encoding="utf-8")
            dep_include.write_text("", encoding="utf-8")

            def make_escape(path: pathlib.Path) -> str:
                text = path.as_posix()
                out = []
                index = 0
                while index < len(text):
                    if text[index] == "\\":
                        start = index
                        while index < len(text) and text[index] == "\\":
                            index += 1
                        count = index - start
                        out.append("\\" * (count * 2 if index < len(text) and text[index] == " " else count))
                        continue
                    char = text[index]
                    if char in " #:":
                        out.append("\\")
                    elif char == "$":
                        out.append("$")
                    out.append(char)
                    index += 1
                return "".join(out)

            dep_second = special / "second.adhd"
            dep_second.write_text("", encoding="utf-8")
            for index, inputs in enumerate(((dep_input,), (dep_input, dep_second))):
                dep_output = root / f"dep_output_{index}"
                depfile = root / f"input_{index}.d"
                command = [str(compiler)]
                for item in inputs:
                    command.extend(["--input", str(item)])
                command.extend(["--output", str(dep_output), "--depfile", str(depfile)])
                result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                if result.returncode != 0:
                    raise AssertionError(f"depfile compilation failed: {result.stdout}\n{result.stderr}")
                dep_content = depfile.read_text(encoding="utf-8")
                generated = tuple(dep_output / f"{item.stem}.adhd.{suffix}" for item in inputs for suffix in ("h", "cpp"))
                for path in generated:
                    path.unlink()
                touch = "; ".join(f"open(r'{path.as_posix()}', 'a').close()" for path in generated)
                command_text = f'"{sys.executable}" -c "{touch}"'.replace("$", "$$")
                manifest = root / f"build_{index}.ninja"
                manifest.write_text(f"rule generate\n  command = {command_text}\n  depfile = {ninja_escape(depfile)}\n  deps = gcc\nbuild {' '.join(ninja_escape(path) for path in generated)}: generate\n", encoding="utf-8")
                result = subprocess.run([ninja, "-f", str(manifest)], cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                if result.returncode != 0:
                    raise AssertionError(f"Ninja could not consume depfile: {result.stdout}\n{result.stderr}")
                result = subprocess.run([ninja, "-f", str(manifest), "-t", "deps", str(generated[0])], cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                expected = (*inputs, dep_include)
                dependency_output = result.stdout.replace("\\", "/") if os.name == "nt" else result.stdout
                if result.returncode != 0 or any(path.as_posix() not in dependency_output for path in expected):
                    raise AssertionError(f"Ninja lost depfile dependencies: {result.stdout}\n{result.stderr}")
                timestamp = generated[0].stat().st_mtime_ns + 2_000_000_000
                os.utime(dep_input, ns=(timestamp, timestamp))
                result = subprocess.run([ninja, "-f", str(manifest)], cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                if result.returncode != 0 or "[1/1]" not in result.stdout:
                    raise AssertionError(f"Ninja did not rebuild after a dependency changed: {result.stdout}\n{result.stderr}")
                make = shutil.which("make") if os.name != "nt" else None
                if make:
                    make_depfile = root / f"make_input_{index}.d"
                    make_depfile.write_text(dep_content, encoding="utf-8")
                    makefile = root / f"build_{index}.mk"
                    makefile.write_text(f"include {make_depfile.name}\n{make_escape(generated[0])}:\n\t@:\n", encoding="utf-8")
                    timestamp = time.time_ns() - 1_000_000_000
                    os.utime(dep_input, ns=(timestamp, timestamp))
                    os.utime(generated[0], ns=(timestamp - 1_000_000_000, timestamp - 1_000_000_000))
                    result = subprocess.run([make, "-q", "-f", str(makefile), str(generated[0])], cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                    if result.returncode != 1:
                        raise AssertionError(f"Make did not detect the changed dependency: {result.stdout}\n{result.stderr}")

            if os.name != "nt":
                bad_cases = (("source\tbad", False), ("source;bad", False), ("source\\#bad", False), ("source\\:bad", False), ("source\\$bad", False), ("source?bad", False), ("source%bad", False), ("source=bad", False), ("source[bad]", False), ("~", True))
                for index, (directory, relative_input) in enumerate(bad_cases):
                    bad_dir = root / directory
                    bad_dir.mkdir()
                    bad_input = bad_dir / "input.adhd"
                    bad_input.write_text("", encoding="utf-8")
                    bad_output = root / f"bad_dep_output_{index}"
                    bad_output.mkdir()
                    bad_header = bad_output / "input.adhd.h"
                    bad_source = bad_output / "input.adhd.cpp"
                    bad_depfile = root / f"bad_input_{index}.d"
                    for path in (bad_header, bad_source, bad_depfile):
                        path.write_text("sentinel", encoding="utf-8")
                    input_arg = bad_input.relative_to(root) if relative_input else bad_input
                    command = [str(compiler), "--input", str(input_arg), "--output", str(bad_output), "--depfile", str(bad_depfile)]
                    result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                    if result.returncode == 0 or "cannot be represented safely" not in result.stdout + result.stderr:
                        raise AssertionError(f"unsafe depfile path {directory!r} was accepted: {result.stdout}\n{result.stderr}")
                    if any(path.read_text(encoding="utf-8") != "sentinel" for path in (bad_header, bad_source, bad_depfile)):
                        raise AssertionError("unsafe depfile path changed existing outputs")

                tail_include = root / "tail;bad.adhd"
                tail_include.write_text("", encoding="utf-8")
                tail_input = root / "tail_input.adhd"
                tail_input.write_text('#include "tail;bad.adhd"\n', encoding="utf-8")
                tail_output = root / "tail_output"
                tail_output.mkdir()
                tail_header = tail_output / "tail_input.adhd.h"
                tail_source = tail_output / "tail_input.adhd.cpp"
                tail_depfile = root / "tail_input.d"
                for path in (tail_header, tail_source, tail_depfile):
                    path.write_text("sentinel", encoding="utf-8")
                command = [str(compiler), "--input", str(tail_input), "--output", str(tail_output), "--depfile", str(tail_depfile)]
                result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                if result.returncode == 0 or "cannot be represented safely" not in result.stdout + result.stderr:
                    raise AssertionError(f"unsafe included depfile path was accepted: {result.stdout}\n{result.stderr}")
                if any(path.read_text(encoding="utf-8") != "sentinel" for path in (tail_header, tail_source, tail_depfile)):
                    raise AssertionError("unsafe included depfile path changed existing outputs")

                for index, name in enumerate(("terminal:", "terminal\\")):
                    terminal_input = root / name
                    terminal_input.write_text("", encoding="utf-8")
                    terminal_output = root / f"terminal_output_{index}"
                    terminal_output.mkdir()
                    terminal_header = terminal_output / f"{name}.adhd.h"
                    terminal_source = terminal_output / f"{name}.adhd.cpp"
                    terminal_depfile = root / f"terminal_{index}.d"
                    for path in (terminal_header, terminal_source, terminal_depfile):
                        path.write_text("sentinel", encoding="utf-8")
                    command = [str(compiler), "--input", str(terminal_input), "--output", str(terminal_output), "--depfile", str(terminal_depfile)]
                    result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
                    if result.returncode == 0 or "cannot be represented safely" not in result.stdout + result.stderr:
                        raise AssertionError(f"unsafe terminal depfile path {name!r} was accepted: {result.stdout}\n{result.stderr}")
                    if any(path.read_text(encoding="utf-8") != "sentinel" for path in (terminal_header, terminal_source, terminal_depfile)):
                        raise AssertionError("unsafe terminal depfile path changed existing outputs")
        else:
            print("SKIP: Ninja depfile consumption check (Ninja not available)")

        run_cpp_import_cli_tests(compiler, root, ninja)


if __name__ == "__main__":
    main()
