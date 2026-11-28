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


def run_layout_cli_tests(compiler: pathlib.Path, root: pathlib.Path) -> None:
    source = root / "layout.adhd"
    consumer = root / "consumer_layout.adhd"
    output = root / "layout_output"
    source.write_text(
        "namespace_wrapper EXAMPLE_LAYOUT;\n"
        '[cpp_header("<tests/common/probe_opaque.h>"), cpp_name("::ProbeOpaque")] extern void ProbeOpaque();\n'
        "class Entry<T, U>{ public T key; public U value; }\n"
        "class Catalog{ private Vector<u8> bytes; [owner_result] public Vector<u8> snapshot()const{ return this.bytes; } }\n"
        "class Item{ [owner_result] public Vector<u8> read()const{ return Vector<u8>(); } }\n"
        "interface IBacking{ b8 valid()const; } class Backing : IBacking{ public b8 valid()const override{ return true; } } "
        "class Converted{ public Converted(in Backing input){} } type ConvertedAlias = Converted; "
        "void Observe(in Converted v){} void Keep(in borrow ConvertedAlias v){} void KeepBase(in borrow IBacking v){} "
        "void KeepStable(in Backing owner, in ConvertedAlias view){ Observe(owner); Keep(view); KeepBase(owner); }\n"
        "class OutputProbe : IGpuTaskGraphOutputLayerContributor{ "
        "public b8 prepareTaskGraphOutputLayer(in AcquiredPresentationFrame frame)override{ ProbeOpaque(); return frame.valid(); } "
        "public void acceptTaskGraphOutputLayer(in u64 generation, in QueueSubmissionToken token)override; }\n"
        "void OutputProbe::acceptTaskGraphOutputLayer(in u64 generation, in QueueSubmissionToken token){}\n"
        "class Consumer{ [cpp_rvalue_overload] public b8 consume(mut u32 value){ ++value; return true; } }\n"
        "class Guard : NoCopy{ public Guard(){ ProbeOpaque(); } public ~Guard(){ ProbeOpaque(); } }\n"
        "class Slot{ private u32 value = 0u; [cpp_explicit_bool] public b8 ready()const{ return this.value != 0u; } "
        "[cpp_take_assignment] public void take(mut u32 incoming){ this.value = incoming; incoming = 0u; } }\n"
        "class CounterRef{ private mut u32 count; public CounterRef(mut u32 count){ this.count = count; } }\n"
        "class Owner{ private u8 first = 0u; private u32 count = 0u; private CounterRef last; "
        "public Owner(){ this.last = CounterRef(this.count); } }\n"
        "[align(8)] class WirePair<T, U>{ public T tag; public U value; }\n"
        "static_assert(offset_of<WirePair<u8, u32>>(value) == 4u);\n"
        "static_assert(align_of<WirePair<u8, u32>>() == 8u);\n"
        "static_assert(offset_of<Entry<u32, f32>>(key) == 0u);\n"
        'static_assert(size_of<Entry<u32, f32>>() == 8u, "entry size");\n'
        "static_assert(align_of<u32>() == 4u);\n"
        "static_assert(is_standard_layout<Entry<u32, f32>>());\n"
        "static_assert(is_trivially_copyable<Entry<u32, f32>>());\n",
        encoding="utf-8",
    )
    consumer.write_text('#include "layout.adhd"\n', encoding="utf-8")

    def generate(path: pathlib.Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(compiler), "--input", str(path), "--output", str(output), "--namespace", "NWB::Example::Layout"],
            cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False,
        )

    for path in (source, consumer):
        result = generate(path)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
    header = (output / "layout.adhd.h").read_text(encoding="utf-8")
    if "#define EXAMPLE_LAYOUT_BEGIN NWB_BEGIN namespace Example::Layout{" not in header or "#define EXAMPLE_LAYOUT_END }; NWB_END" not in header:
        raise AssertionError(f"namespace wrapper lost its configured namespace:\n{header}")
    if "const ::NWB::Core::QueueSubmissionToken& token)noexcept override" not in header or "prepareTaskGraphOutputLayer(const ::NWB::Core::AcquiredPresentationFrame& frame) override" not in header:
        raise AssertionError("SDK overrides must preserve the virtual signature")
    if header.count("static_assert(") < 5 or 'offsetof(AdhdLayoutType, key)' not in header or '"entry size"' not in header:
        raise AssertionError(f"layout checks missing from their defining header:\n{header}")
    generated = (output / "layout.adhd.cpp").read_text(encoding="utf-8")
    if "snapshot()const" not in header or "__adhd_caller_arena = (*this->__adhd_arena)" not in header + generated:
        raise AssertionError("owner_result did not use the declaring object's arena")
    if "    Guard();" not in header or "~Guard()noexcept(false);" not in header:
        raise AssertionError("ordinary constructors and destructors did not retain their exception contracts")
    if "explicit Item(::ArenaBase&" not in header or "read()const" not in header:
        raise AssertionError("owner_result did not retain an arena on the fieldless owner")
    owner_fields = ("u8 first", "u32 count", "CounterRef last")
    owner = header[header.index("class Owner final{"):].split("};", 1)[0]
    positions = [owner.index(field) for field in owner_fields]
    if positions != sorted(positions):
        raise AssertionError("ordered class changed the declared field order")
    if "last(this->count)" not in header + generated:
        raise AssertionError("ordered field lost its constructor borrow of an earlier sibling")
    if "class alignas(8) WirePair" not in header:
        raise AssertionError(f"missing record alignment:\n{header}")
    imported = (output / "consumer_layout.adhd.h").read_text(encoding="utf-8")
    if "#define EXAMPLE_LAYOUT" in imported or '"entry size"' in imported:
        raise AssertionError(f"import redefined a namespace wrapper or assertion:\n{imported}")

    for invalid, diagnostic in (
        ("[owner_result] Vector<u8> Read(){ return Vector<u8>(); }", "instance method"),
        ("class Item{ [owner_result] public u32 read()const{ return 1u; } }", "arena-backed result"),
        ("class Item{ [cpp_explicit_bool] public i32 value()const{ return 1; } }", "returning b8"),
        ("class Item{ [cpp_take_assignment] public void take(in u32 input){} }", "one mutable value"),
        ("class Item{ [cpp_take_assignment] public void take(mut Item item){} }", "another type"),
        ("class Item{ [cpp_explicit_bool] public b8 ready()const{ return true; } [cpp_explicit_bool] public b8 valid()const{ return true; } }", "already exports"),
        ("[cpp_unwind] void BadUnwind(){}", "unsupported attribute on callable"),
        ("class X : IGpuTaskGraphOutputLayerContributor{ public void acceptTaskGraphOutputLayer(in u64 generation, in QueueSubmissionToken token)override{} } [cpp_unwind] void Y(){}", "unsupported attribute on callable"),
        ("void Read(in cpp_ref u32 value){}", "expected"),
        ("[cpp_flags(nothrow)] extern void BadNothrow();", "valid extern flag"),
        ("[cpp_flags(no_copy)] extern class BadNoCopy;", "valid extern flag"),
        ("[cpp_flags(small_value)] extern class BadSmallValue;", "valid extern flag"),
        ("[cpp_flags(scratch_arena)] extern void BadScratchArena();", "valid extern flag"),
        ("[arena] extern class BadExternArena;", "unsupported attribute on class"),
        ("class Item{ [cpp_rvalue_overload] public b8 read(in u32 value){ return true; } }", "one mutable value"),
        ("[cpp_type_alias(Item, u32)] class Item{}", "distinct member name"),
        ("[cpp_type_alias(Value, u32)] class Item{ public u32 Value; }", "distinct member name"),
        ("class A{} class B{ public B(in A a){} } class Retains{ private in B b; public Retains(in B value){ this.b = value; } } "
         "void Bad(in A a){ Retains dangling(a); }", "a retained reference cannot bind to a temporary created by an implicit class conversion"),
        ("[align(3)] class Item{}", "power-of-two"),
        ("[align(0)] class Item{}", "power-of-two"),
        ("[layout(8)] class Item{}", "unsupported attribute on class"),
        ("[ordered] class Item{}", "unsupported attribute on class"),
        ("[move_only] class Item{}", "unsupported attribute on class"),
        ("[modular] class Item{}", "unsupported attribute on class"),
        ('[arena, asset("a")] class Item{}', "prohibited"),
        ("[reflect] enum<u8> E{ A = 0 }", "prohibited"),
        ("[reflect] interface I{ void f(); }", "prohibited"),
        ("[serialize, reflect] class Item{ public u32 v = 0u; }", "prohibited"),
        ("namespace_wrapper NWB;", "reserved"),
        ("namespace_wrapper A; namespace_wrapper B;", "one namespace_wrapper"),
        ("class Item{ private i32 hidden; } static_assert(offset_of<Item>(hidden) == 0u);", "accessible instance field"),
        ('static_assert(true, "bad" + "message");', "message must be a string literal"),
    ):
        source.write_text(invalid, encoding="utf-8")
        result = generate(source)
        if result.returncode == 0 or diagnostic not in result.stdout + result.stderr:
            raise AssertionError(f"expected '{diagnostic}' for {invalid}:\n{result.stdout}\n{result.stderr}")


def run_shared_specialization_cli_test(compiler: pathlib.Path, root: pathlib.Path) -> None:
    folder = root / "shared_specialization"
    folder.mkdir()
    owner = folder / "cell.adhd"
    left = folder / "left.adhd"
    right = folder / "right.adhd"
    output = folder / "generated"
    owner.write_text(
        "class Slot{ public u32 count = 0u; }\n"
        "class Cell<T>{ public T value; public Slot slot; "
        "public void reset(){ mut Slot entry = this.slot; entry.count = 0u; } }\n",
        encoding="utf-8",
    )
    left.write_text(
        '#include "cell.adhd"\n'
        '[cpp_header("<sdk/width.h>"), cpp_name("SDK_WIDTH")] extern const u32 extWidth;\n'
        'type Word = u32;\nclass Left{ public Cell<Array<Word, 2u>> cell; public Array<u32, extWidth> values; }\n',
        encoding="utf-8",
    )
    right.write_text(
        '#include "cell.adhd"\n'
        'class Right{ public Cell<Array<u32, 2>> cell; public Cell<Array<u32, 3u>> wider; }\n',
        encoding="utf-8",
    )
    names = {owner: "Cells", left: "LeftClient", right: "RightClient"}
    namespace_map = folder / "namespaces.txt"
    namespace_map.write_text("".join(f"{path.resolve()}\t{namespace}\n" for path, namespace in names.items()), encoding="utf-8")
    guards = {}
    for path, namespace in names.items():
        result = subprocess.run(
            [str(compiler), "--input", str(path), "--output", str(output), "--namespace", namespace,
             "--namespace-map", str(namespace_map), "-I", str(folder)],
            cwd=folder, capture_output=True, text=True, errors="replace", timeout=30, check=False,
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        header = (output / (path.name + ".h")).read_text(encoding="utf-8")
        guards[path] = {line for line in header.splitlines() if line.startswith("#ifndef NWB_ADHD_CLASS_SPECIALIZATION_Cell_")}
        if path != owner and ("namespace Cells{" not in header or f"namespace {namespace}{{" not in header):
            raise AssertionError(f"specialization in the wrong namespace:\n{header}")
        if path == left and "#include <sdk/width.h>" not in header:
            raise AssertionError(f"array capacity omitted its external constant header:\n{header}")
        if path != owner and "entry->count" not in header:
            raise AssertionError(f"imported specialization did not lower its borrowed local:\n{header}")
    if len(guards[left]) != 1 or len(guards[right]) != 2 or not guards[left] < guards[right]:
        raise AssertionError(f"independent imports disagree on the specialization guards: {guards}")


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


def check_conditional_exception_spec(compiler: pathlib.Path, root: pathlib.Path) -> None:
    source = root / "visitor.adhd"
    output = root / "visitor_output"
    command = [str(compiler), "--input", str(source), "--output", str(output)]
    visitor = "b8 Apply<F>(mut forward F visitor)noexcept_like(){ return cast<b8>(visitor()); }"
    cases = (
        (visitor + "\nb8 Check(){ return Apply(lambda[]() -> b8{ return true; }); }", True, "F&& visitor)noexcept(noexcept(::DeclVal<void(*)(b8)noexcept>()("),
        (visitor.replace("noexcept_like()", "noexcept_like(visitor())"), True, "noexcept(noexcept(visitor()))"),
        (visitor.replace("forward F", "copy F").replace("noexcept_like()", "noexcept_like(visitor(), cast<b8>(visitor()))"), True, "IsNothrowDestructible_V<F> && noexcept(visitor()) && noexcept(static_cast<b8>(visitor()))"),
        (visitor.replace("noexcept_like()", "noexcept_like(unknownArgument)"), False, "requires a visible parameter"),
        (visitor.replace("return cast", "u32 unused = 0u; return cast"), False, "one return expression"),
        (visitor.replace("forward F", "forward u32"), False, "unwrapped type parameter"),
    )
    platform_text = 'const PlatformStringView s_Reason = PlatformTextLiteral("invalid");'
    cases += (
        (platform_text, True, '::PlatformTextLiteral(FOUNDATION_TEXT("invalid"))'),
        ("PlatformStringView Convert(in StringView input){ return PlatformTextLiteral(input); }", False, "platform text requires a string literal"),
    )
    for text, success, expected in cases:
        source.write_text(text, encoding="utf-8")
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        actual = (output / "visitor.adhd.h").read_text(encoding="utf-8") if result.returncode == 0 else result.stdout + result.stderr
        if (result.returncode == 0) != success or expected not in actual:
            raise AssertionError(f"unexpected generic visitor result: {actual}")


def check_unchanged_output_mtime_preserved(compiler: pathlib.Path, root: pathlib.Path) -> None:
    stable_dir = root / "stable_mtime"
    stable_dir.mkdir()
    source = stable_dir / "stable.adhd"
    output = stable_dir / "out"
    command = [str(compiler), "--input", str(source), "--output", str(output)]

    source.write_text("const u32 s_Stable = 1u;\n", encoding="utf-8")
    first = subprocess.run(command, cwd=stable_dir, capture_output=True, text=True, errors="replace", timeout=30, check=False)
    if first.returncode != 0:
        raise AssertionError(f"initial compile failed: {first.stdout}\n{first.stderr}")

    header = output / "stable.adhd.h"
    generated = output / "stable.adhd.cpp"
    old_ns = header.stat().st_mtime_ns - 5_000_000_000
    os.utime(header, ns=(old_ns, old_ns))
    os.utime(generated, ns=(old_ns, old_ns))
    recorded_hdr_mtime = header.stat().st_mtime_ns
    recorded_cpp_mtime = generated.stat().st_mtime_ns

    second = subprocess.run(command, cwd=stable_dir, capture_output=True, text=True, errors="replace", timeout=30, check=False)
    if second.returncode != 0:
        raise AssertionError(f"unchanged recompile failed: {second.stdout}\n{second.stderr}")
    if header.stat().st_mtime_ns != recorded_hdr_mtime or generated.stat().st_mtime_ns != recorded_cpp_mtime:
        raise AssertionError("identical compiler output modified existing file mtime")

    source.write_text("const u32 s_Stable = 2u;\n", encoding="utf-8")
    third = subprocess.run(command, cwd=stable_dir, capture_output=True, text=True, errors="replace", timeout=30, check=False)
    if third.returncode != 0:
        raise AssertionError(f"changed recompile failed: {third.stdout}\n{third.stderr}")
    if "2u" not in header.read_text(encoding="utf-8") or header.stat().st_mtime_ns <= recorded_hdr_mtime:
        raise AssertionError("changed compiler output did not update file content and mtime")


def check_asset_cook_roles(compiler: pathlib.Path, root: pathlib.Path) -> None:
    source = root / "cook_roles.adhd"
    output = root / "cook_role_output"
    command = [str(compiler), "--input", str(source), "--output", str(output)]

    def check(text: str, expected: str, success: bool) -> None:
        source.write_text(text, encoding="utf-8")
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace", timeout=30, check=False)
        actual = (output / "cook_roles.adhd.cpp").read_text(encoding="utf-8") if result.returncode == 0 else result.stdout + result.stderr
        if (result.returncode == 0) != success or expected not in actual:
            raise AssertionError(f"unexpected asset role result: {actual}")

    asset = '[asset("sample")] class Sample{ public b8 loadBinary(in Span<u8> bytes); }\n'
    entry = 'class Entry{ public Name path = s_NameNone; }\n'
    subsystem = '[subsystem("group")] class Group{ public AssetRef<Sample> owner; }\n'
    registration = "register_asset Group using TypedAssetRegistration<IAsset, Group, Entry>; "
    check(asset + subsystem + registration, "Group, Entry> s_GroupAssetRegistrar", True)
    check(asset + subsystem + "register_asset Group using u32;", "requires a codec registrar", False)
    check(asset + "register_asset Sample;", "expected 'using'", False)
    check(asset + "register_asset Entry using AssetCodecRegistration<Entry>;", "requires an asset or a subsystem", False)
    check(asset + subsystem + registration * 2, "duplicate asset registration", False)
    check(asset + "register_asset Sample using NonStandaloneAssetRegistration<Sample>;", "NonStandaloneAssetRegistration<Sample>", True)

    retired_attrs = [
        "[asset_writer] b8 Write(){ return true; }",
        '[cook_entry("sample", s_Label, Read, document_and_value, quiet)] b8 Build(){ return true; }',
        "[cook_sidecar(s_Ext)] b8 Read(){ return true; }",
        "[gather_merge] b8 Merge(){ return true; }",
        "[cook_registrar] b8 Reg(){ return true; }",
        "[cook_document] b8 Parse(){ return true; }",
        "[volume_prepare] b8 Prepare(){ return true; }",
        "[cook_setup] void Setup(){}",
    ]
    for snippet in retired_attrs:
        check(snippet, "unsupported attribute on callable", False)


def main() -> None:
    compiler = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="nwb_adhd_include_") as name:
        root = pathlib.Path(name)
        run_layout_cli_tests(compiler, root)
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

        check_unchanged_output_mtime_preserved(compiler, root)
        check_asset_cook_roles(compiler, root)
        check_conditional_exception_spec(compiler, root)
        run_shared_specialization_cli_test(compiler, root)
        run_cpp_import_cli_tests(compiler, root, ninja)


if __name__ == "__main__":
    main()
