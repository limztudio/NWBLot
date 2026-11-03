#!/usr/bin/env python3

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


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
            special = root / ("source #$ space" if os.name == "nt" else "source #$ space\tback\\slash:colon")
            special.mkdir()
            dep_input = special / "input.adhd"
            dep_include = special / "shared #$.adhd"
            dep_input.write_text('#include "shared #$.adhd"\n', encoding="utf-8")
            dep_include.write_text("", encoding="utf-8")
            def ninja_escape(path: pathlib.Path) -> str:
                return path.as_posix().replace("$", "$$").replace(" ", "$ ").replace(":", "$:")

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
        else:
            print("SKIP: Ninja depfile consumption check (Ninja not available)")


if __name__ == "__main__":
    main()
