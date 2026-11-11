# `.adhd` Shape and Authoring Guide

Authoritative guide for structuring and writing `.adhd` files in NWBLot. Every `.adhd` file is compiled by `adhd_compiler` (`core/metascript/adhd/`) into generated `.adhd.h` and `.adhd.cpp` C++ sources.

A file containing only literal `Name` constants uses a minimal global header. A file containing only numeric, boolean, or `StringView` constants may use a minimal global header when the values are literals or simple arithmetic using earlier numeric constants. Casts, built-in limits, mixed `Name` and scalar constants, and other declarations keep the standard generated dependencies.

When using `--depfile`, paths must be safe for both Ninja and Make depfiles. The compiler rejects tabs, line breaks, unsupported punctuation, paths starting with `~` or ending in `:` or `\`, and a backslash immediately before `#`, `:`, or `$`. It preserves ordinary backslashes and backslashes before spaces. Rejection leaves existing outputs unchanged.

---

## 1. File Shape and Formatting

- **Filename**: Lowercase `snake_case.adhd`.
- **Encoding & Newlines**: UTF-8 encoding with Windows-style `CRLF` (`\r\n`) line endings on every line.
- **Banner & Section Separators**:
  - Start every file with:
    ```cpp
    // limztudio@gmail.com
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ```
  - Use the exact 128-slash separator (`////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////`) between major file sections.
  - Keep exactly two blank lines before and after every file-scope 128-slash separator (except immediately after `// limztudio@gmail.com`, which has no blank line before the banner separator).
  - End every file with the closing 128-slash separator followed by exactly two newline terminators (`\r\n\r\n`, one blank line at EOF).
- **Indentation, Braces & Line Width**:
  - 4-space indentation (no tabs), K&R braces (`class Foo{`, `if(cond){`, `else{`), and a 260-character soft line width.
  - No space between closing `)` and trailing method qualifiers (`const`, `override`):
    - Correct: `public u32 count()const{`, `public b8 evaluate()const override{`, `public void apply()override;`
    - Wrong: `public u32 count() const{`, `public b8 evaluate() const override{`
- **Canonical Top-to-Bottom Section Order**:
  1. `.adhd` `#include` directives (`#include <...>` or `#include "..."` for other `.adhd` files)
  2. Top-level `[header] native{ ... }` block (C++ `#include`s and forward declarations needed by the generated `.adhd.h`; `[source] native{ ... }` at the bottom when `.adhd.cpp`-only C++ definitions are needed)
  3. File-scope `const` compile-time constants (`const u32 s_MaxItems = 64u;`) and `type` aliases (`type ItemId = u64;`)
  4. `enum<SizeType>` and `flag<SizeType>` declarations
  5. `interface` declarations
  6. `class` declarations
  7. Top-level free functions and out-of-line `ClassName::methodName` definitions

```cpp
// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/id.adhd>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const f32 s_DefaultPadding = 4.0f;

type ElementIndex = u32;

[reflect]
enum<u8> AlignmentMode{
    Leading = 0,
    Center = 1,
    Trailing = 2,
    kCount
}

flag<u32> ElementStateFlags{
    None = 0u,
    Hovered,
    Pressed,
    Disabled,
}

class ElementBox{
    public f32 width = 0.0f;
    public f32 height = 0.0f;
    public AlignmentMode mode = AlignmentMode::Leading;

    public ElementBox() = default;

    public ElementBox(in f32 w, in f32 h, in AlignmentMode m){
        this.width = w;
        this.height = h;
        this.mode = m;
    }

    public f32 area()const{
        return this.width * this.height;
    }
}

f32 ComputePaddedWidth(in ElementBox box){
    return box.width + s_DefaultPadding * 2.0f;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

```

---

## 2. Compiler-Owned Decisions & Prohibited Constructs

1. **No `[inline]` / `[noinline]` Attributes and No `inline` / `noinline` Keywords**:
   - Inlining is decided **exclusively and automatically by the `adhd` compiler itself** (`ShouldAutoInlineCallable`), never by the user.
   - Writing `[inline]`, `[noinline]`, `inline`, or `noinline` anywhere in `.adhd` is a compile-time error.
2. **No `namespace` Keyword**:
   - `.adhd` files never declare `namespace`. The C++ namespace is configured on the CMake target via `nwb_target_adhd_sources(<target> NAMESPACE "NWB::...")`.
3. **No `struct` or `union` Keywords**:
   - All record types use `class` (or `[serialize] class` for 1-byte packed binary layout).
4. **No `static` or `virtual` Keywords**:
   - Use top-level `const` for compile-time constants and top-level free functions for stateless helpers.
   - Declare abstract contracts in an `interface` (implicitly pure virtual) and mark implementing `class` methods with `override`.
5. **No `template` Keyword**:
   - Write generic parameters inline on the declaration: `class Pair<T, U>{ ... }`, `void SwapValues<T>(mut T a, mut T b){ ... }`, `class StaticBatch<T, const usize Capacity>{ ... }`.
6. **No Raw Pointers (`*`), Address-Of (`&`), Arrow (`->`), `new`/`delete`, or `null`/`nullptr`**:
   - All variables and `in`/`mut` parameters are non-null by construction and accessed with `.` syntax.
   - Use `Optional<T>`, `Expected<T, E>`, or `AssetRef<T>` when absence or failure is possible.
7. **No C++-Style `const` Type Qualifiers or Native C Scalar Names**:
   - Read-only parameters use `in T` (never `const T` or `const T&`).
   - `const` is permitted only on file-level compile-time constants (`const u32 s_Limit = 16u;`), non-type template parameters (`<const usize N>`), and read-only method suffixes (`)const`).
   - Use NWBLot scalar types (`b8`, `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `isize`, `usize`, `f32`, `f64`, `void`); `bool`, `int`, `float`, `double`, `char`, `short`, `long`, `unsigned`, and `signed` are prohibited.
8. **No `Move`, Exceptions (`try`/`catch`/`throw`), `goto`, Ternary (`? :`), C-Style Casts, C-Style Arrays (`T[N]`), or Bitfields**:
   - Last-use `Move(...)` is emitted automatically by the compiler.
   - Use `if`/`else` instead of `? :`.
   - Use `cast<T>(x)` for value conversions and `bit_cast<T>(x)` for bitwise reinterpretation.
   - Use `Array<T, N>`, `FixedVector<T, N>`, or `Vector<T>` instead of `T[N]`.
   - Use `flag<SizeType>` instead of C bitfields.

---

## 3. Classes, Arena Ownership (`[arena]`), Layout Reordering, and `[serialize]`

- **Explicit Access Specifiers on Every Member**:
  - Every field, constructor, destructor, operator, and method inside a `class` must begin with `public`, `private`, or `protected` (C++-style `public:` section labels are prohibited).
- **Single-Source Member Initialization**:
  - Every `class` field must either declare an inline default value (`public f32 speed = 1.0f;`) or be initialized inside the class's constructor body (`this.speed = s;`)—never both.
  - C++ constructor initializer lists (`: speed(s)`) are prohibited; assign `this.field = ...;` inside the constructor `{ ... }` body.
- **Mother-Arena Propagation, `[arena]` Root Owners, and Local `ScratchArena`**:
  - **Root Arena Owners (`[arena]` and `[subsystem("...")]`)**: Mark root arena-owning classes (ECS components, systems, standalone state owners) with `[arena]` so they own `s_ClassArena` (`GlobalArena`), while `[subsystem("...")]` classes allocate from their `TypedSubsystem` `AssetArena`. Root owners remain default-constructible and pass their arena (`::ArenaBase&`) down to every arena-backed field.
  - **Non-Root Classes (Mother-Arena Borrowing)**: Any non-root class (without `[arena]`, `[subsystem("...")]`, `[component_asset("...")]`, or `[serialize]`) that directly or transitively holds arena-backed fields carries a hidden `::NotNull<::ArenaBase*> __adhd_arena` reference (`using uses_leading_arena_t = ::ArenaBase;`) injected automatically as the leading C++ constructor parameter. Nested hierarchies (`[arena] A -> B -> C`) allocate from `A`'s arena, and cross-arena container construction or `=` assignment automatically rebinds elements into the destination's mother arena without stealing foreign arena pointers. Pure scalar/POD classes carry no hidden arena member.
  - **Local `ScratchArena` Borrowing**: Local arena-backed variables, temporaries, non-root class instances, and arena-returning helper calls automatically borrow the enclosing function's stack-scoped `ScratchArena` (`__adhd_scratch`), reclaiming scratch memory when the root scope exits. Functions/methods returning an arena-backed value take a hidden leading `::ArenaBase& __adhd_return_arena` parameter in C++ so callers pass either their local `ScratchArena` or a destination mother arena.
- **Automatic Member Reordering vs. `[serialize]`**:
  - **Default `class` (No `[serialize]`)**: The `adhd` compiler automatically reorders fields by descending alignment and size (`32B -> 16B -> 8B -> 4B -> 2B -> 1B`) to eliminate padding holes. Because the emitted C++ member order may differ from authored order, do not use C++ positional aggregate initialization (`Foo{ a, b }`) on non-`[serialize]` classes; use constructors or named member assignment.
  - **`[serialize] class`**: Emits a 1-byte packed layout (`#pragma pack(push, 1)` when no SIMD fields are present) and generates binary serialization, `.nwb` metadata parsing, and `AssetRef<T>` dependency extraction. Use `[serialize]` for binary payloads, GPU constant/push-constant mirrors, or structs requiring exact authored field order (`[serialize]` cannot be combined with `[arena]`).

---

## 4. Enums, Flags, Interfaces, and Types

- **`enum<SizeType> Name` & `flag<SizeType> Name`**:
  - Must specify an explicit integer size type (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`).
  - Use `kCount` (never `Count`) for enum/flag count sentries.
  - `enum<SizeType> Name` emits `namespace Name{ enum Enum : ::SizeType{ ... }; };` (referenced in C++ as `Name::Enum`).
  - `flag<SizeType> Name` emits `namespace Name{ enum Flag : ::SizeType{ ... }; };` (referenced in C++ as `Name::Flag`), with uninitialized entries auto-shifting `(static_cast<::SizeType>(1) << bitIndex)` and automatic bitwise operators (`|`, `&`, `^`, `~`, `|=`, `&=`, `^=`).
- **`interface`**:
  - Contains method declarations only (no fields, no access specifiers, no method bodies).
  - Used via `in IContract` / `mut IContract` parameters, rebindable local references, or template bounds (`<T : IContract>`).
- **Built-In SIMD, Containers, and Strings**:
  - **SIMD Calculation Types vs. Memory-Layout Types**:
    - **Calculation / Parameter Types (`VectorSingle`, `VectorDouble`, `MatrixSingle`, `MatrixDouble`)**: Lower directly to `::SIMDVectorSingle`, `::SIMDVectorDouble`, `::SIMDMatrix`, and `::SIMDMatrixDouble` (`Expected<VectorSingle>` / `Optional<VectorSingle>` and `VectorDouble` lower to `::SIMDVectorSingleResult` / `::SIMDVectorDoubleResult`). All SIMD arithmetic, multi-lane swizzling (`.xyz`, `.zwxy`), SIMD math intrinsics (`Abs`, `Min`, `Max`, `Clamp`, `Saturate`, `Lerp`, `Sqrt`, `Rsqrt`, `Sin`, `Cos`, `Tan`, `ASin`, `ACos`, `ATan`, `ATan2`, `Exp`, `Exp2`, `Log`, `Log2`, `Pow`, `Floor`, `Ceil`, `Round`, `Dot`, `Cross`, `Length`, `LengthSq`, `Normalize`, `Mul`, `Transpose`, `Inverse`, `Vector3Transform`, `MatrixDeterminant`, `AabbValid`, `TryTransformAabb`, `IsFinite`, `any`, `all`), and SIMD function/method/lambda/`Func` parameters **must** use `VectorSingle`, `VectorDouble`, `MatrixSingle`, or `MatrixDouble`. For finite checks on SIMD registers, prefer `IsFinite(vec)`, `IsFinite(vec.xyz)`, or `IsFinite(vec.xy)` over lane-by-lane scalar `IsFinite` calls: `VectorIsFinite` / `VectorDoubleIsFinite` use integer exponent-mask comparisons, and the `adhd` compiler automatically emits `#pragma STDC FENV_ACCESS ON` for blocks, lambdas, and parallel tasks that evaluate SIMD floating-point expressions so inactive or short-circuited lanes never raise spurious `FE_INVALID` exceptions. Because they are register calculation types, `VectorSingle`, `VectorDouble`, `MatrixSingle`, and `MatrixDouble` **cannot be held in memory** as `class` member fields, container/array/span elements (`Array<VectorSingle, N>`, `Vector<VectorSingle>`, `Span<VectorSingle>`, etc.), or file-scope `const` declarations.
    - **Memory-Layout Types (`f32x4~` and `*u`)**: Aligned storage types (`f32x4`, `i32x4`, `u32x4`, `f32x3_i32`, `f32x3_u32`, `f32x3x4`, `f32x4x4`, `f64x2`, `f64x3`, `f64x4`, `i64x4`, `u64x4`, `f64x3_i64`, `f64x3_u64`, `f64x3x3`, `f64x3x4`, `f64x4x4`) and unaligned storage types (`f16x2u`, `f16x4u`, `f32x2u`, `f32x3u`, `f32x4u`, `i32x2u`, `i32x3u`, `i32x4u`, `u32x2u`, `u32x3u`, `u32x4u`, `f64x2u`, `f64x3u`, `f64x4u`, `i64x2u`, `i64x3u`, `i64x4u`, `u64x2u`, `u64x3u`, `u64x4u`, `f32x3x3u`, `f32x3x4u`, `f32x4x4u`, `f64x3x3u`, `f64x3x4u`, `f64x4x4u`) are required whenever values are held in memory (all `class` member fields, container/array/span elements, and file-scope constants) and are prohibited as SIMD parameters or direct SIMD arithmetic operands. Load storage values into calculation registers with `VectorSingle(storage)` / `VectorDouble(storage)` / `MatrixSingle(storage)` / `MatrixDouble(storage)` (or `VectorSingle(storage, w)`) and store back with `f32x4(vec)` / `f32x3u(vec)` / `f32x3_i32(vec, w)` / `f32x3x4(mat)` / `f32x4x4u(mat)` to avoid redundant `LoadFloat`/`StoreFloat` conversions.
  - **Containers & Generic Wrappers**: `Array<T, N>`, `FixedVector<T, N>`, `Vector<T>`, `ParallelVector<T>`, `ForwardList<T>`, `List<T>`, `Deque<T>`, `Set<T>`, `HashSet<T>`, `ParallelHashSet<T>`, `Map<K, V>`, `HashMap<K, V>`, `ParallelHashMap<K, V>`, `Queue<T>`, `ParallelQueue<T>`, `ParallelBlockQueue<T, BlockSize>`, `CompressedPair<A, B>`, `FixedIndexMap<K>`, `RefCountPtr<T>`, `UniquePtr<T>`, `NotNullUniquePtr<T>`, `ComponentPool<T>`, `TypedAsset<T>`, `AssetCodec<T>`. Owning containers lower to `Container<..., ::ArenaBase>` (accepting any concrete C++ arena via deduced template parameters on read/mut parameters) and automatically bind to the owning class's mother/root arena (fields) or a stack-scoped `ScratchArena` (locals).
  - **Spans**: `Span<T>` in `.adhd` is always read-only (`Span<const T>` in C++). Use `mut Vector<T>` or `mut FixedVector<T, N>` for mutable buffer outputs.
  - **Strings, Paths, and Text Parsing**: UTF-8 exclusively (`String`, `StringView`, `CompactString`, `Name`, `Path`, `ExecutablePaths`, `StagedDirectoryPaths`, `StagedFilePairPaths`, `DirectoryEntry`, `DirectoryIterator`, `RecursiveDirectoryIterator`, `PathIdentity`, `StagedFileWriter`, `DiagnosticEventText`). Write plain `"..."` literals (never `NWB_TEXT("...")`). Arena-backed path types inject their C++ arena constructor argument automatically when needed (or construct directly from `Path` when the C++ type derives its arena from `const Path<ArenaT>&`). `StringFormat` and `StringAppendFormat` automatically adapt `String`, `CompactString`, `Path`, and `Name` format arguments. Built-in text/Unicode/JSON/DOT/CSV/Latin-1/source-text helpers include `TrimView`, `TrimLeftView`, `TrimInPlace`, `TrimCopy`, `EqualsAsciiIgnoreCase`, `ContainsAsciiIgnoreCase`, `ParseName`, `ToName`, `DeriveName`, `BeginDerivedNameHash`, `UpdateDerivedNameHashText`, `UpdateDerivedNameHashDecimal`, `FinishDerivedNameHash`, `ParseI64`, `ParseU64`, `ParseF32`, `ParseF64`, `FormatHex64A`, `AppendUnsignedU64Text`, `AppendUnsignedU64Bytes`, `AppendEscapedJsonString`, `AppendEscapedDotLabel`, `TryDecodeUtf8CodePoint`, `TryDecodeUtf8Scalar`, `IsUtf8ScalarBoundary`, `IsValidUtf8TextWithoutNul`, `CountUtf8ScalarsAssumingValid`, `Utf8ByteOffset`, `BuildGraphemeBoundaries`, `LookupGraphemeBreak`, `LookupIndicConjunct`, plus `.front()`, `.back()`, `.substr(...)`, `.starts_with(...)`, and `.ends_with(...)`.
  - **Built-In `global/` and `core/` Types, Enums, Flags, Functions, and Shadowing**:
    - All cross-platform `.adhd`-usable object types, RHI `*Handle` wrappers, abstract C++ interfaces, enums, bitmask flags, free/static/template functions, SIMD intrinsics (`Vector*`, `VectorDouble*`, `Matrix*`, `Quaternion*`, `DualQuaternion*`, `Bounding*`, `Aabb*`, `Plane*`, `Ray*`, `Triangle*`, `Frustum*`, `Sdf*`), receiver methods, public fields, and `s_*` constants from `global/` and `core/` (`alloc`, `assets`, `common`, `crash`, `ecs`, `filesystem`, `graphics`, `ime`, `input`, `metascript`, `perf`, `task`, `telemetry`) are registered in `export_types.h` and `export_functions.h`.
    - **Built-In C++ Base Interfaces & Abstract Contracts**: Non-final polymorphic C++ interfaces (`IInputEventHandler`, `IFilesystem`, `ILogger`, `ISystem`, `IAssetCodec`, `IMetaReader`, `IFrameGraphContributor`, etc.) are marked `isPolymorphicBase` in `export_types.h` and may be implemented directly by `.adhd` classes (`class Handler : IInputEventHandler`) or used as template bounds (`<T : IInputEventHandler>`). Abstract built-in C++ interfaces (`isAbstract`) are reference-only (`in` / `mut` parameters and rebindable local references) and cannot be instantiated, returned, or stored by value. Built-in structs whose C++ layout holds SIMD register members (`AabbBounds`, `MatrixDecomposition`, `TangentFrameDirections`, etc.) may be used on the stack and passed via `in` / `mut`, and are rejected in `class` fields and heap/sequence containers.
    - **User-Declaration Precedence & On-Demand Header Tracking**: User-declared `.adhd` types, enums, flags, interfaces, type aliases, functions, methods, fields, constants, and locals always shadow built-in `global/` and `core/` exports of the same name. Compiler semantic analysis records `requiredGlobalHeaders` only for unshadowed built-in symbols actually referenced by the translation unit, emitting `<global/...>` headers before `<core/alloc/global.h>` and `<core/...>` headers after `<core/ecs/entity_id.h>` so targets never pull in unlinked optional `core/` subsystems.
  - **Result & Error Types**: `Optional<T>`, `Expected<T>` (defaulting to `Failure`), `Expected<T, E>`, `Failure`, `ErrorCode`, and `MakeUnexpected(...)`.
    - Fallible value producers must return `Expected<T>` (or `Expected<T, E>`) and return `MakeUnexpected(Failure())` (or `MakeUnexpected(error)`) on failure—never `b8` with a `mut outResult` parameter.

---

## 5. Functions, Methods, and Control Flow

- **Function & Method Naming**:
  - Member functions (`class` and `interface` methods) must start with a lowercase letter (`lowerCamelCase` or container `snake_case`).
  - Global/free functions (including built-in global functions) must start with an uppercase letter (`UpperCamelCase`).
- **Mandatory `in` / `mut` Parameters & `Expected<T>` Return Shape**:
  - Every parameter must specify `in` (read-only borrow, lowered via `CallTraits<T>::param_type`) or `mut` (mutable reference `T&`).
  - Default parameter values are prohibited; write explicit overloads instead.
  - Fallible functions and interface/class methods that produce a value must return `Expected<T>` (or `Expected<T, E>`) instead of returning `b8` (or a status enum) with a `mut outResult` parameter. Reserve `mut` parameters for genuine in-out state mutations (`inOutCursor`, `inOutState`), reusable caller-owned buffers, or multi-output unpacking helpers where a dedicated result class is not warranted.
- **Mandatory `this.` Qualification**:
  - Inside any class method, accessing a member field (`this.position`) or calling a member method (`this.reset()`) must be prefixed with `this.`.
- **Mandatory Local Initialization & Loop Syntax**:
  - Every local variable must be initialized at declaration (`u32 count = 0u;`, `auto total = 0.0f;`).
  - Range-based `for` loops require `in` or `mut`: `for(in auto item : list)` or `for(mut Node item : nodes)`.
  - Every `switch` `case` and `default` branch must end with `break;` or `return;`.
- **Lambdas & Callables**:
  - Non-null callable type: `Func<Ret(in A, mut B)>` (wrap in `Optional<Func<...>>` if nullable).
  - Lambda syntax: `lambda[in threshold, mut count](in f32 value) -> b8{ ... }` (all captures and parameters must specify `in` or `mut`).
- **Parallelism & Diagnostics**:
  - `parallel_for(...)` and `parallel taskName : dependencyName{ ... }` lower to `Core::CpuTaskScheduler`.
  - Built-in diagnostics: `assert(cond, "msg", ...)`, `info("msg", ...)`, `warning("msg", ...)`, `error("msg", ...)`, optionally prefixed with `[essential]`.
  - When a diagnostic call with formatting arguments spans multiple lines, place each formatting argument after the message on its own continuation line with a leading comma (never trailing commas):
    ```cpp
    error(
        "MaterialInstanceComponent: parameter '{}' targets interface '{}' but component expects '{}'"
        , parameterNameText
        , materialInterface.resolvedText()
        , component.materialInterface.resolvedText()
    );
    ```

---

## 6. Attributes (`[...]`) and `native` Interop

- **Single-Bracket Attribute Rule**:
  - Combine multiple attributes in a single comma-separated bracket (`[asset("foo"), serialize]`); stacked `[...][...]` blocks on the same declaration are prohibited.
- **Permitted Attributes by Declaration Kind**:
  - `class`: `[arena]`, `[serialize]`, `[reflect]`, `[asset("type_name")]`, `[subsystem("type_name")]`, `[component_asset("type_name")]`
  - `interface`: `[reflect]`, `[asset("type_name")]`
  - `enum` / `flag`: `[reflect]`
  - `field`: `[transient]`, `[reflect]`, `[subsystem("type_name")]`
  - `method` (`class` / `interface`): `[reflect]` (never `[inline]` or `[noinline]`)
  - `parallel` / `parallel_for`: `[core(Any|Heavy|Light|MainThread)]`, `[priority(Normal|Critical|Background)]`
  - `assert` / `info` / `warning` / `error`: `[essential]`
  - top-level `native` (file scope): `[header]` or `[source]` (**mandatory**; omitting `[header]` / `[source]` on a top-level `native` block is a compile error)
- **C++ Interop (`native` Blocks)**:
  - **Top-level `[header] native{ ... }` / `[source] native{ ... }`**: Mandatory explicit target attribute on every file-scope `native` block — `[header]` emits into `.adhd.h` and `[source]` emits into `.adhd.cpp` (automatic header/source guessing is retired). Frame both the outside and inside of every top-level `native{ ... }` block with two blank lines, a 128-slash separator, and two blank lines (and when the entire `native{}` body is wrapped in an outer `#if ...` / `#endif`, place two blank lines + 128-slash + two blank lines inside `#if ...` and before `#endif` as well).
  - **Statement-level `native(in x, mut y = this.member){ ... }`**: Binds `.adhd` variables/fields into a scoped C++ block (`in` as `const` reference/value, `mut` as mutable reference) while preserving `.adhd` mutability and concurrency analysis.

---

## 7. Compiler Export Tables (`core/metascript/adhd/export_*.h`)

- All built-in `.adhd` language tables live in the centralized X-macro headers under `core/metascript/adhd/`:
  - `export_keywords.h`: Access specifiers, parameter passing modes, method qualifiers, nested/prohibited keywords, bool/enum/cast keywords, parallel core/priority kinds, and diagnostic kinds.
  - `export_operators.h`: Operator tokens, symbols, overloadability, assignment/boolean/bitwise classifications, binary precedence, member arity rules, and prohibited operator diagnostics.
  - `export_types.h`: Scalar types and limits, aligned and unaligned SIMD vector/matrix types, `global/` and `core/` object types (including arena constructor modes, format-arg adaptation kinds, abstract C++ interface flags, and polymorphic C++ base flags), built-in `enum` and bitmask `flag` types/entries, generic container/wrapper types, binary hash types, `.nwb` value deserialization rules, implicit conversions, swizzle lanes, and literal token mappings.
  - `export_functions.h`: Built-in function argument rules, `global/` and `core/` free/static/template functions (accessible both unqualified and via `TypeName::StaticMethod(...)` on exported object and enum scopes, including table-driven call lowerings, explicit or deduced `<T>` return rules, `MatrixIdentity()` / `MatrixIdentity<T>()` dual return rules, borrowed-view returns, SIMD-storage rejection, and on-demand `<global/...>` / `<core/...>` required headers), SIMD float/double/int/lane/reduction/zero-preserving intrinsics, built-in member and receiver methods (including `"[]"` indexing), built-in fields, and built-in `s_*` global and `TypeName::s_*` static constants.
- Query these tables through `KeywordSemantics`, `OperatorSemantics`, and `BuiltinTypeSemantics` in `core/metascript/adhd/ast/`, plus the helpers in `ast/global.h` and `sema/type_utils.h`. Never scatter hard-coded string or token checks for built-in keywords, operators, types, or functions across the lexer, parser, semantic analyzer, or codegen.
