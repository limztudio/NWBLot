# `.adhd` Shape and Authoring Guide

Authoritative guide for structuring and writing `.adhd` files in NWBLot. Every `.adhd` file is compiled by `adhd_compiler` (`core/metascript/adhd/`) into generated `.adhd.h` and `.adhd.cpp` C++ sources.

A file containing only literal `Name` constants uses a minimal global header. A file with only numeric, boolean, or `StringView` constants may use a minimal global header. This applies when the values are literals or simple arithmetic using earlier numeric constants. Casts, built-in limits, mixed `Name` and scalar constants, and other declarations keep the standard generated dependencies.

When using `--depfile`, paths must be safe for both Ninja and Make depfiles. The compiler rejects tabs, line breaks, and unsupported punctuation. It also rejects paths starting with `~` or ending in `:` or `\`. It also rejects a backslash immediately before `#`, `:`, or `$`. It preserves ordinary backslashes and backslashes before spaces. Rejection leaves existing outputs unchanged.

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
- **No Comments**:
  - Do not write explanatory, behavioral, section-heading, or workaround/hacky-fix comments in `.adhd` files. Use clear, descriptive names for functions, classes, types, constants, and variables instead. Preserve only `// limztudio@gmail.com`, exact 128-slash separators, and required third-party license notices.
- **Indentation, Braces & Line Width**:
  - 4-space indentation (no tabs), K&R braces (`class Foo{`, `if(cond){`, `else{`), and a 260-character soft line width.
  - No space between closing `)` and trailing method qualifiers (`const`, `override`):
    - Correct: `public u32 count()const{`, `public b8 evaluate()const override{`, `public void apply()override;`
    - Wrong: `public u32 count() const{`, `public b8 evaluate() const override{`
- **Canonical Top-to-Bottom Section Order**:
  1. `.adhd` `#include` directives (`#include <...>` or `#include "..."` for other `.adhd` files)
  2. Declarations from a C++ SDK use its typed prelude. For implementation, utility, asset pipeline, and test code under `impl/`, `utilities/`, `pipeline/` (outside `adhd_compiler`), and `tests/` (outside compiler string fixtures), include the owning `.adhd` file directly and write declarations, control flow, and implementations in pure ADHD without `native{}` blocks. The ADHD compiler (`core/metascript/adhd/`, `pipeline/adhd_compiler/`) and its command-line bootstrap use C++ so they can build before ADHD sources. This also applies to shared types and cross-module calls.
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
   - A public domain umbrella may declare `namespace_wrapper NWB_IMPL;` once to export its paired C++ `NWB_IMPL_BEGIN` and `NWB_IMPL_END` macros for that configured namespace. Include the umbrella directly from other ADHD modules. The compiler keeps the wrapper in the declaring header and uses the engine root macros for NWB namespaces.
3. **No `struct` or `union` Keywords**:
   - All record types use `class` (or `[serialize] class` for authored-order binary layout).
4. **No `static` or `virtual` Keywords**:
   - Use top-level `const` for compile-time constants and top-level free functions for stateless helpers.
   - Declare abstract contracts in an `interface` (implicitly pure virtual) and mark implementing `class` methods with `override`.
5. **No `template` Keyword**:
   - Write generic parameters inline on the declaration: `class Pair<T, U>{ ... }`, `void SwapValues<T>(mut T a, mut T b){ ... }`, `class StaticBatch<T, const usize Capacity>{ ... }`.
6. **No Raw Pointers (`*`), Address-Of (`&`), Arrow (`->`), `new`/`delete`, `null`/`nullptr`, or `NotNull`**:
   - All variables and `in`/`mut` parameters are non-null by construction and accessed with `.` syntax (`NotNull` is prohibited in `.adhd`; use `in T` or `mut T`).
   - Use `Optional<T>`, `Expected<T, E>`, or `AssetRef<T>` when absence or failure is possible.
7. **No C++-Style `const` Type Qualifiers or Native C Scalar Names**:
   - Read-only parameters use `in T` (never `const T` or `const T&`).
   - `const` is permitted only on file-level compile-time constants (`const u32 s_Limit = 16u;`), non-type template parameters (`<const usize N>`), and read-only method suffixes (`)const`).
   - Use NWBLot scalar types (`b8`, `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `isize`, `usize`, `f32`, `f64`, `void`); `bool`, `int`, `float`, `double`, `char`, `short`, `long`, `unsigned`, and `signed` are prohibited.
8. **No `Move`, Exceptions (`try`/`catch`/`throw`), `goto`, Ternary (`? :`), C-Style Casts, C-Style Arrays (`T[N]`), or Bitfields**:
   - The compiler may emit `Move(...)` at last use when its analysis permits it.
   - Use `if`/`else` instead of `? :`.
   - Use `cast<T>(x)` for value conversions and `bit_cast<T>(x)` for bitwise reinterpretation.
   - Use `Array<T, N>`, `FixedVector<T, N>`, or `Vector<T>` instead of `T[N]`.
   - Use `flag<SizeType>` instead of C bitfields.
9. **No Explicit `*Arena` Types (`ArenaBase`, `AssetArena`, `CookArena`, `GlobalArena`, `GraphicsArena`, `MetaArena`, `PersistentArena`, `ScratchArena`, `TelemetryArena`)**:
   - Arenas are managed **exclusively and automatically by the `adhd` compiler** (`[arena]` mother-arena propagation and local `__adhd_scratch` borrowing).
   - Referencing any of these explicit `*Arena` types in non-native `.adhd` parameters, variables, class fields, type aliases, or expressions is a compile-time error. For explicit C++ arena or third-party SDK interop outside `loader/`, `logger/`, and project interop (`CoolStuff/Testbed/project_world_interop.adhd`), keep C++ adapters in domain-owned `.h`/`.cpp` files paired with typed `extern` `.adhd` declarations (such as `pipeline/cook_workspace.{h,adhd}`, `pipeline/volume_workspace.{h,adhd}`, `pipeline/dependency_computer/dependency_workspace.{h,adhd}`, `utilities/cli/tool_cli.{h,adhd}`, or domain `*_sdk.{h,adhd}` wrappers).

---

## 3. Classes, Arena Ownership (`[arena]`), Layout Reordering, and `[serialize]`

- **Explicit Access Specifiers on Every Member**:
  - Every field, constructor, destructor, operator, and method inside a `class` must begin with `public`, `private`, or `protected` (C++-style `public:` section labels are prohibited).
- **Single-Source Member Initialization**:
  - Every `class` field must either declare an inline default value (`public f32 speed = 1.0f;`) or be initialized inside the class's constructor body (`this.speed = s;`)—never both.
  - C++ constructor initializer lists (`: speed(s)`) are prohibited; assign `this.field = ...;` inside the constructor `{ ... }` body.
- **Mother-Arena Propagation, `[arena]` Root Owners, `[arena, asset("...")]` Classes, and Local `ScratchArena`**:
  - **Root Arena Owners (`[arena]`, `[arena, asset("...")]`, `[arena, subsystem("...")]`, `[arena, component("...")]`, and `[arena, system("...")]`)**: Every `asset("...")`, `subsystem("...")`, `component("...")`, and `system("...")` class must also declare `arena` (`[arena, asset("...")]`, `[arena, subsystem("...")]`, `[arena, component("...")]`, `[arena, system("...")]`). Standalone root arena-owning classes and `[arena, component("...")]` classes own `s_ClassArena` (`GlobalArena`), `[arena, asset("...")]` classes derive from `Core::Assets::TypedAsset<T>` (or `Core::Assets::TypedAsset<T, BaseAssetInterface>`), `[arena, subsystem("...")]` classes derive from `Core::Assets::TypedAsset<T, Impl::ISubsystem>` (with `TypedSubsystemAssetCodec<T>` / `TypedSubsystemAutoRegistrar<T>` in `impl/assets_subsystem/asset.adhd`), and both asset and subsystem classes allocate from their `AssetArena` (`__adhd_arena`). `[arena, system("...")]` classes derive from `Core::ECS::ISystem` and borrow their mother arena (`__adhd_arena`). Root owners pass their arena (`::ArenaBase&`) down to every arena-backed field, and inside methods of an `[arena, asset("...")]` class, local arena-backed variables and candidate asset instances automatically bind to `*this->__adhd_arena` so `this = candidate;` performs an O(1) move while preserving the `IAsset` base subobject (`virtualPath()`).
  - **`[arena, asset("type_name")]` / `[arena, component("type_name")]` / `[arena, system("type_name")]` Lowering**:
    - An `[arena, asset("type_name")] class Name` automatically emits `NWB_DEFINE_ASSET_TYPE("type_name")`, public `using` aliases for any `.adhd` `type` aliases used by its fields (and their inner template argument `type` aliases), inherited `IAsset` methods (`virtualPath()`, `assetType()`, `checkVirtualPath(...)`), and `NWB_DEFINE_ASSET_CODEC(<CodecName>, Name);` when `b8 loadBinary(in Span<u8> binary)` is declared and the class does not implement a custom `[asset("...")] interface` (plus `AssetCodecAutoRegistrar` in the `.adhd.cpp` that defines `Name::loadBinary`). When a class implements an `[asset("...")] interface` (such as `IComponent` or `IShader`), it derives from `Core::Assets::TypedAsset<Name, BaseAssetInterface>`.
    - An `[arena, component("type_name")] class Name` marks an ECS runtime component class, emits `NWB_DEFINE_COMPONENT_TYPE("type_name")` (`s_ComponentTypeText`, `s_ComponentTypeName`, `ComponentTypeName()`), and owns `s_ClassArena`.
    - An `[arena, system("type_name")] class Name` derives from `Core::ECS::ISystem`, emits `NWB_DEFINE_SYSTEM_TYPE("type_name")` (`s_SystemTypeText`, `s_SystemTypeName`, `SystemTypeName()`) and `systemType()const noexcept override`, forwards its mother arena (`::ArenaBase& __adhd_mother_arena`) to `Core::ECS::ISystem(__adhd_mother_arena)`, and exposes `ISystem` methods (`readAccess<T>()`, `writeAccess<T>()`, `registerAccess(...)`, `prepare(...)`, `update(...)`, `taskOptions()`, `accesses()`, `systemType()`).
  - **Non-Root Classes (Mother-Arena Borrowing)**: Any non-root class (without `[arena]` or `[serialize]`) that directly or transitively holds arena-backed fields carries a hidden `::NotNull<::ArenaBase*> __adhd_arena` reference (`using uses_leading_arena_t = ::ArenaBase;`) injected automatically as the leading C++ constructor parameter. Nested hierarchies (`[arena] A -> B -> C`) allocate from `A`'s arena, and cross-arena container construction or `=` assignment automatically rebinds elements into the destination's mother arena without stealing foreign arena pointers. Pure scalar/POD classes carry no hidden arena member.
  - **Local `ScratchArena` Borrowing**: Local arena-backed variables, temporaries, non-root class instances, and arena-returning helper calls automatically borrow the enclosing function's stack-scoped `ScratchArena` (`__adhd_scratch`), reclaiming scratch memory when the root scope exits. Functions/methods returning an arena-backed value take a hidden leading `::ArenaBase& __adhd_return_arena` parameter in C++ so callers pass either their local `ScratchArena` or a destination mother arena.
- A method marked `[owner_result]` returns an independently owned copy allocated from the receiver's mother arena. The class retains its mother arena even when its other fields only borrow collaborators. Use this for a published snapshot whose memory must stay charged to a registry or resource owner. The owner arena must outlive the returned value. A local initialized directly by such a method on `this` or one of its owned fields keeps that arena for its scope. Assignment into an existing owned value preserves the destination arena.
- Mark an ordinary value class `[move_only]` when it transfers ownership and must prohibit copies. Its fields must support the moves the class uses. Arena-backed values keep the destination allocator on move assignment. Use `MoveInto<T>(destination, source)` to transfer a move-only, move-assignable value after validation. Both arguments must be mutable lvalues of type `T`. The destination keeps its arena, and same-arena container moves transfer storage. Self-move leaves the value intact. A `[move_only]` class with the direct `NoCopy` base can move-construct when its fields allow it. The base prohibits move assignment.
- A class can grant its private and protected members to named non-template peers in its own namespace with `[friend(Publisher, Builder)]`. Each peer may be defined in its own source. The permission is one-way.
- **Class field order and alignment**:
  - **Default `class` (No `[serialize]`)**: The `adhd` compiler automatically reorders fields by descending alignment and size (`32B -> 16B -> 8B -> 4B -> 2B -> 1B`) to reduce padding. Because the emitted C++ member order may differ from authored order, do not use C++ positional aggregate initialization (`Foo{ a, b }`) on non-`[serialize]` classes; use constructors or named member assignment.
  - A `SharedPool` constructs values with `pool.make<T>(...)`. Check the returned `Expected<Shared<T>>` before keeping a handle. To transfer an existing value with `pool.makePrepared<T>(value)`, keep the value's original arena and borrowed dependencies alive until the last handle is released.
  - A `PlanReservation<Control, Plan>` owns a successful reservation. Moving it transfers that responsibility. After the control accepts the plan, call `release()`. Calling `discard()` or dropping a pending reservation returns it to its control.
  - An ordinary non-template `[extensible]` class permits one public authored concrete base per derived class. The base constructs before the derived fields using the caller's arena when needed. Provide a default constructor on the base. These classes are noncopyable and nonmovable and support virtual destruction through the base. A derived class may use its base's public and protected members and override inherited interface methods. Mark an intermediate base `[extensible]` to allow another level.
  - Use `[ordered]` when a field borrows a sibling or relies on its lifetime. Fields construct in declaration order and destruct in reverse order. Declare the owner before its borrowers. In the leading field assignments of the constructor, a field constructor can borrow an earlier sibling that is already initialized.
  - Use `[layout(8)] class` for a value record with a prescribed C or shader ABI. The integer is a power-of-two alignment in bytes. Fields keep their declared order and natural alignment. The class size includes any tail padding. Combine `layout` and `serialize` when such a record also needs binary serialization.
  - **`[serialize] class`**: Preserves authored field order, emits `#pragma pack(push, 1)` only when the fields do not contain over-aligned built-in types, and generates binary serialization, `.nwb` metadata parsing, and `AssetRef<T>` dependency extraction. Use `[serialize]` for binary payloads, GPU constant/push-constant mirrors, or structs requiring exact authored field order (`[serialize]` cannot be combined with `[arena]`).

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
    - **Calculation / Parameter Types (`VectorSingle`, `VectorDouble`, `MatrixSingle`, `MatrixDouble`)**: Lower directly to `::SIMDVectorSingle`, `::SIMDVectorDouble`, `::SIMDMatrix`, and `::SIMDMatrixDouble` (`Expected<VectorSingle>` / `Optional<VectorSingle>` and `VectorDouble` lower to `::SIMDVectorSingleResult` / `::SIMDVectorDoubleResult`). All SIMD arithmetic, multi-lane swizzling (`.xyz`, `.zwxy`), SIMD math intrinsics (`Abs`, `Min`, `Max`, `Clamp`, `Saturate`, `Lerp`, `Sqrt`, `Rsqrt`, `Sin`, `Cos`, `Tan`, `ASin`, `ACos`, `ATan`, `ATan2`, `Exp`, `Exp2`, `Log`, `Log2`, `Pow`, `Floor`, `Ceil`, `Round`, `Dot`, `Cross`, `Length`, `LengthSq`, `Normalize`, `Mul`, `Transpose`, `Inverse`, `Vector3Transform`, `MatrixDeterminant`, `AabbValid`, `TryTransformAabb`, `IsFinite`), and SIMD function/method/lambda/`Func` parameters **must** use `VectorSingle`, `VectorDouble`, `MatrixSingle`, or `MatrixDouble`. For finite checks on SIMD registers, prefer `IsFinite(vec)`, `IsFinite(vec.xyz)`, or `IsFinite(vec.xy)` over lane-by-lane scalar `IsFinite` calls: `VectorIsFinite` / `VectorDoubleIsFinite` use integer exponent-mask comparisons that avoid `FE_INVALID` for signaling NaNs. The `adhd` compiler emits `#pragma STDC FENV_ACCESS ON` for blocks, lambdas, and parallel tasks that evaluate SIMD floating-point expressions. Other SIMD operations still need safe inputs in every evaluated lane. Because they are register calculation types, `VectorSingle`, `VectorDouble`, `MatrixSingle`, and `MatrixDouble` **cannot be held in memory** as `class` member fields, container/array/span elements (`Array<VectorSingle, N>`, `Vector<VectorSingle>`, `Span<VectorSingle>`, etc.), or file-scope `const` declarations.
    - **Memory-Layout Types (`f32x4~` and `*u`)**: Aligned storage types (`f32x4`, `i32x4`, `u32x4`, `f32x3_i32`, `f32x3_u32`, `f32x3x4`, `f32x4x4`, `f64x2`, `f64x3`, `f64x4`, `i64x4`, `u64x4`, `f64x3_i64`, `f64x3_u64`, `f64x3x3`, `f64x3x4`, `f64x4x4`) and unaligned storage types (`f16x2u`, `f16x4u`, `f32x2u`, `f32x3u`, `f32x4u`, `i32x2u`, `i32x3u`, `i32x4u`, `u32x2u`, `u32x3u`, `u32x4u`, `f64x2u`, `f64x3u`, `f64x4u`, `i64x2u`, `i64x3u`, `i64x4u`, `u64x2u`, `u64x3u`, `u64x4u`, `f32x3x3u`, `f32x3x4u`, `f32x4x4u`, `f64x3x3u`, `f64x3x4u`, `f64x4x4u`) are required whenever values are held in memory (all `class` member fields, container/array/span elements, and file-scope constants) and are prohibited as SIMD parameters or direct SIMD arithmetic operands. Load storage values into calculation registers with `VectorSingle(storage)` / `VectorDouble(storage)` / `MatrixSingle(storage)` / `MatrixDouble(storage)` (or `VectorSingle(storage, w)`) and store back with `f32x4(vec)` / `f32x3u(vec)` / `f32x3_i32(vec, w)` / `f32x3x4(mat)` / `f32x4x4u(mat)` to avoid redundant `LoadFloat`/`StoreFloat` conversions.
  - **Containers & Generic Wrappers**: `Array<T, N>`, `FixedVector<T, N>`, `Vector<T>`, `ParallelVector<T>`, `ForwardList<T>`, `List<T>`, `Deque<T>`, `Set<T>`, `HashSet<T>`, `ParallelHashSet<T>`, `Map<K, V>`, `HashMap<K, V>`, `ParallelHashMap<K, V>`, `Queue<T>`, `ParallelQueue<T>`, `ParallelBlockQueue<T, BlockSize>`, `CompressedPair<A, B>`, `FixedIndexMap<K>`, `PlanReservation<Control, Plan>`, `RefCountPtr<T>`, `Shared<T>`, `UniquePtr<T>`, `NotNullUniquePtr<T>`, `ComponentPool<T>`, `TypedAsset<T>`, `AssetCodec<T>`. Owning containers lower to `Container<..., ::ArenaBase>` (accepting any concrete C++ arena via deduced template parameters on read/mut parameters) and automatically bind to the owning class's mother/root arena (fields) or a stack-scoped `ScratchArena` (locals).
  - **Spans**: `Span<T>` in `.adhd` is always read-only (`Span<const T>` in C++). Use `mut Vector<T>` or `mut FixedVector<T, N>` for mutable buffer outputs.
  - **Strings, Paths, and Text Parsing**: UTF-8 exclusively (`String`, `StringView`, `CompactString`, `Name`, `Path`, `PlatformStringView`, `ExecutablePaths`, `StagedDirectoryPaths`, `StagedFilePairPaths`, `DirectoryEntry`, `DirectoryIterator`, `RecursiveDirectoryIterator`, `PathIdentity`, `StagedFileWriter`, `DiagnosticEventText`). Write plain `"..."` literals (never `FOUNDATION_TEXT("...")`; use `PlatformTextLiteral("...")` where a static `PlatformStringView` is required). Arena-backed path types inject their C++ arena constructor argument automatically when needed (or construct directly from `Path` when the C++ type derives its arena from `const Path<ArenaT>&`). `StringFormat` and `StringAppendFormat` automatically adapt `String`, `CompactString`, `Path`, and `Name` format arguments. Built-in text/Unicode/JSON/DOT/CSV/Latin-1/source-text helpers include `TrimView`, `TrimLeftView`, `TrimInPlace`, `TrimCopy`, `EqualsAsciiIgnoreCase`, `ContainsAsciiIgnoreCase`, `ParseName`, `ToName`, `DeriveName`, `BeginDerivedNameHash`, `UpdateDerivedNameHashText`, `UpdateDerivedNameHashDecimal`, `FinishDerivedNameHash`, `ParseI64`, `ParseU64`, `ParseF32`, `ParseF64`, `FormatHex64A`, `AppendUnsignedU64Text`, `AppendUnsignedU64Bytes`, `AppendEscapedJsonString`, `AppendEscapedDotLabel`, `TryDecodeUtf8CodePoint`, `TryDecodeUtf8Scalar`, `IsUtf8ScalarBoundary`, `IsValidUtf8TextWithoutNul`, `CountUtf8ScalarsAssumingValid`, `Utf8ByteOffset`, `BuildGraphemeBoundaries`, `LookupGraphemeBreak`, `LookupIndicConjunct`, plus `.front()`, `.back()`, `.substr(...)`, `.starts_with(...)`, and `.ends_with(...)`.
  - **Built-In `global/` and `core/` Types, Enums, Flags, Functions, and Shadowing**:
    - All cross-platform `.adhd`-usable object types, RHI `*Handle` wrappers, abstract C++ interfaces, enums, bitmask flags, free/static/template functions, SIMD intrinsics (`Vector*`, `VectorDouble*`, `Matrix*`, `Quaternion*`, `DualQuaternion*`, `Bounding*`, `Aabb*`, `Plane*`, `Ray*`, `Triangle*`, `Frustum*`, `Sdf*`), receiver methods, public fields, and `s_*` constants from `global/` and `core/` (`alloc`, `assets`, `common`, `crash`, `ecs`, `filesystem`, `frame`, `graphics`, `input`, `mesh`, `metascript`, `os`, `perf`, `task`, `telemetry`) live in per-header `global/**/*.adhd` and `core/**/*.adhd` `extern` mirror files collected by `core/metascript/adhd/export_global_prelude.adhd` and `core/metascript/adhd/export_core_prelude.adhd`, while `export_global_{types,functions}.h` and `export_core_{types,functions}.h` (under umbrella headers `export_types.h` / `export_functions.h`) retain intrinsic compiler contracts only.
    - **Built-In C++ Base Interfaces & Abstract Contracts**: Non-final polymorphic C++ interfaces (`IInputEventHandler`, `IFilesystem`, `ILogger`, `ISystem`, `IAssetCodec`, `IMetaReader`, `IFrameGraphContributor`, etc.) are marked `[cpp_flags(polymorphic_base)]` in their per-header `global/**/*.adhd` or `core/**/*.adhd` `extern` declarations (collected by `export_global_prelude.adhd` / `export_core_prelude.adhd`) and may be implemented directly by `.adhd` classes (`class Handler : IInputEventHandler`) or used as template bounds (`<T : IInputEventHandler>`). Abstract built-in C++ interfaces (`[cpp_flags(abstract)]`) are reference-only (`in` / `mut` parameters and rebindable local references) and cannot be instantiated, returned, or stored by value. Built-in structs whose C++ layout holds SIMD register members (`AabbBounds`, `MatrixDecomposition`, `TangentFrameDirections`, etc.) may be used on the stack and passed via `in` / `mut`, and are rejected in `class` fields and heap/sequence containers.
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
  - On an authored class, `[cpp_type_alias(Name, Type)]` publishes a public nested alias in its C++ header. Use an existing named type as the target. ADHD callers use that target directly.
  - Use `[cpp_rvalue_overload]` on an ordinary instance method with one mutable value argument and a scalar, enum, or void result to accept temporaries from C++. The overload forwards the original argument as a mutable lvalue and preserves the method’s exception specification.
  - A retained parameter requires a directly bindable lvalue. When a converting constructor would create a temporary, first construct an owned value whose lifetime covers the borrow.
  - A typed SDK method can describe its existing C++ virtual contract with `[cpp_flags(unwind)]` and `in cpp_ref T`. The first permits an ADHD override to use `[cpp_unwind]`. The second preserves a C++ const-reference parameter for a small value in an override. Calls may pass temporary values, and the receiver may read the argument for the duration of the call.
  - Use `[cpp_unwind]` on an ordinary function or method when an imported C++ operation must propagate an exception to its caller. The callable and its nested lambdas omit the generated C++ `noexcept` promise. Mark every intervening ADHD function on that call path. Declare the attribute on a class method in the class definition and repeat it on each separate free function declaration. An authored interface method can declare `[cpp_unwind]` to allow its implementations to propagate C++ exceptions through virtual calls. Mark each override that can propagate an exception. An unmarked override keeps its nonthrowing contract. An override and a derived interface method must respect any nonthrowing base declaration. Ordinary class constructors and destructors can also declare `[cpp_unwind]`. A marked destructor emits C++ `noexcept(false)`. An implemented interface keeps its nonthrowing destructor contract. The `NoCopy` base permits a marked destructor. Registered asset, subsystem, component, and system constructors and destructors keep their framework contract. Operators and asset cook roles have their own contracts. Parallel worker boundaries retain their existing exception specification.
  - A generic free function can use `noexcept_like(expression)` after its parameters to inherit the C++ exception specification of one or more comma-separated operations. `noexcept_like()` on a single-return body includes conversion to the return type. Declare and define the function together. Use `mut forward T` for a generic argument that can be a mutable lvalue or a temporary, such as a synchronous visitor. Use `mut copy T` to own a generic argument or integer parameter by value. Conditional exception specifications include destruction of copied parameters.
  - An ordinary class may export a named method to a C++ operator. `[cpp_explicit_bool]` on a const parameterless `b8` method also emits an explicit C++ boolean conversion. `[cpp_take_assignment]` on a mutable `void` method with one `mut T` parameter materializes an incoming C++ value of type `T` and passes it to that method. The assignment accepts inputs constructible as `T` and preserves ordinary copy and move assignment of the owner. Each export may appear once per class. ADHD code can call the named method directly.
  - Use `in borrow T name` when a callable retains a read-only reference to the caller’s object. Generated C++ passes even small types by `const T&`. An inline ADHD constructor infers this contract when assigning an `in` field directly from a parameter or its subobject. Declare retained parameters explicitly on out-of-line constructor declarations and when forwarding a small input to another retaining callable. A retained argument must be a stable lvalue.
  - `Optional<in T>` and `Expected<mut T>` can store a reference to existing data. After `has_value()` succeeds, `value()` accesses the original object. The stored reference mode determines whether the object can be modified.
  - GPU graph tasks register an owned payload with `graph.addPayloadTask(desc, Payload(...))`. Use `graph.addPreparedPayloadTask(desc, payload)` to transfer a populated mutable payload to the graph. A recording payload implements `commandRequirements()const` and `record(mut CommandList, in GpuTaskRecordContext)const`. It can implement `accepted(in QueueSubmissionToken)` and `discarded()` to update mutable state. A lifecycle-only payload may omit both recording methods. Use `[cpp_unwind]` when the scheduler must receive an exception from a backend call.
  - An in-class constructor may use `[cpp_base(ExternalBase, first, second)]` to initialize a direct external polymorphic base. List one or more `mut` or `in borrow` constructor parameters in the order the SDK constructor expects. The generated C++ checks the SDK constructor. The caller must keep a service or storage retained by the base alive until the derived instance is destroyed.
  - Lambda syntax: `lambda[in threshold, mut count](in f32 value) -> b8{ ... }` (all captures and parameters must specify `in` or `mut`).
  - `MetascriptDocument.visitVariable` accepts a visitor for `MetascriptValue`. A mutable visitor needs a mutable document.
  - `MetascriptValue.visitField` accepts a read-only value visitor. `forEachMapEntry` accepts read-only `StringView` and `MetascriptValue` parameters.
  - These visitors need known parameter counts and modes. Use a lambda, a typed `Func`, an unambiguous non-template free function, or a local alias of one. Lambda parameters may use `auto`. A visitor return value cannot need a caller arena.
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

### Compile-time layout checks

Use `PlatformTextLiteral("text")` when a framework callback needs a static `PlatformStringView` in the platform character width.

Use a file-scope `static_assert(condition, "diagnostic");` for binary and GPU contracts. The diagnostic string is optional. The compiler emits the check after the generated types are complete, so the C++ build verifies the layout for the target platform. Use ADHD type names with `size_of<T>()`, `align_of<T>()`, `offset_of<T>(field)`, `is_standard_layout<T>()`, and `is_trivially_copyable<T>()`. An offset names one accessible instance field.
Use `is_cpp_type_alias<Owner, Expected>(Alias)` to check a public nested C++ alias. Use `has_nothrow_rvalue_member<Owner, Result, Argument>(method)` to check that a public non-const method has the exact single-rvalue member signature and accepts that rvalue without unwinding.
Use `is_nothrow_call<in Mesh, mut usize>(FunctionName)` to inspect the C++ exception specification of a declared free function with unevaluated arguments. An `in` type supplies a const lvalue, a `mut` type supplies a mutable lvalue, and an unqualified type supplies an rvalue. Use `is_nothrow_call(FunctionName)` for a function with no arguments.
Use `is_cpp_constructible<Owner, in Arg>()` to check constructibility without constructing a value. Supply only the owner to test default construction.

A framework can declare a namespace-scope registration macro with `[cpp_header("<owner/registration.h>"), cpp_name("OWNER_REGISTER")]` and `extern registration Register(label, callback);`. A call such as `registration Register(Label, Verify);` supplies identifiers and emits the registration once in the owning source. Keep the macro and its binding with the framework. GoogleTest suites include `tests/common/gtest_macros.adhd` and use `NWB_ADHD_GTEST(Suite, Case, Verify)` or `NWB_ADHD_GTEST_F(Fixture, Case, Verify)`. Ordinary fixtures can declare public `setup()` and `teardown()` hooks. A fixture can also use an existing GoogleTest base class.

## 6. Attributes (`[...]`), `extern`, and `native` Interop

- **Single-Bracket Attribute Rule**:
  - Combine multiple attributes in a single comma-separated bracket (`[arena, asset("foo")]`); stacked `[...][...]` blocks on the same declaration are prohibited.
- **Permitted Attributes by Declaration Kind**:
  - `class`: `[arena]`, `[layout(alignment)]`, `[modular]`, `[ordered]`, `[extensible]`, `[move_only]`, `[serialize]`, `[reflect]`, `[friend(PeerA, PeerB)]`, `[cpp_type_alias(Name, Type)]`, `[asset("type_name")]`, `[subsystem("type_name")]`, `[component("type_name")]`, `[system("type_name")]`
  - `extern class`: `[arena]`, `[cpp_name("...")]`, `[cpp_header("<...>")]`, `[cpp_flags(...)]` (`self_defaulted`, `small_value`, `borrow_view`, `binary_serializable`, `brace_init`, `equality_switch`, `non_thread_safe`, `map_key`, `arena_first_ctor`, `arena_omit_ctor`, `no_copy`, `no_assign`, `abstract`, `polymorphic_base`)
  - `interface`: `[reflect]`, `[asset("type_name")]` (plus `[cpp_name("...")]`, `[cpp_header("<...>")]` when `extern`)
  - `enum` / `flag`: `[reflect]` (plus `[cpp_name("...")]`, `[cpp_header("<...>")]` when `extern`)
  - `field`: `[transient]`, `[reflect]` (`[subsystem("type_name")]` belongs on `class`, not on fields)
  - `method` (`class` / `interface`): `[reflect]`, `[owner_result]`, `[cpp_unwind]`, `[cpp_rvalue_overload]`, `[cpp_explicit_bool]`, `[cpp_take_assignment]`, `[cpp_base(Base, param...)]` (constructors) (never `[inline]` or `[noinline]`); `extern` methods also accept `[cpp_name("...")]`, `[cpp_header("<...>")]`, `[cpp_flags(...)]` (`unwind`, `scratch_arena`, `borrow_view`, `platform_literal_arg0`, `byte_view_arg0`, `byte_view_arg1`), and variadic `..` / `...` parameter lists when their parameter types are unconstrained
  - free functions: `[cpp_unwind]`, `[asset_writer]`, `[cook_entry(...)]`, `[cook_sidecar(extConst)]`, `[gather_merge]`, `[cook_registrar]`, `[cook_document]`, `[volume_prepare]`, `[cook_setup]`
  - `extern` free functions: `[cpp_name("...")]`, `[cpp_header("<...>")]`, `[cpp_flags(...)]` (`unwind`, `scratch_arena`, `borrow_view`, `platform_literal_arg0`, `byte_view_arg0`, `byte_view_arg1`), with optional `= {}` default parameter placeholders
  - `extern const` / `extern type` / `extern interface` / `extern registration`: `[cpp_name("...")]`, `[cpp_header("<...>")]`
  - `parallel` / `parallel_for`: `[core(Any|Heavy|Light|MainThread)]`, `[priority(Normal|Critical|Background)]`
  - `assert` / `info` / `warning` / `error`: `[essential]`
  - top-level `native` (file scope): `[header]` or `[source]` (**mandatory**; omitting `[header]` / `[source]` on a top-level `native` block is a compile error)
- **C++ Interop (`native` Blocks)**:
  - **Top-level `[header] native{ ... }` / `[source] native{ ... }`**: Mandatory explicit target attribute on every file-scope `native` block — `[header]` emits into `.adhd.h` and `[source]` emits into `.adhd.cpp` (automatic header/source guessing is retired). Frame both the outside and inside of every top-level `native{ ... }` block with two blank lines, a 128-slash separator, and two blank lines (and when the entire `native{}` body is wrapped in an outer `#if ...` / `#endif`, place two blank lines + 128-slash + two blank lines inside `#if ...` and before `#endif` as well).
  - **Statement-level `native(in x, mut y = this.member){ ... }`**: Binds `.adhd` variables/fields into a scoped C++ block (`in` as `const` reference/value, `mut` as mutable reference). The compiler checks the declared capture modes, but it does not analyze the C++ body. C++ constness can be shallow, so the author must keep data reached through an `in` capture read-only and uphold parallel safety.

---

### Asset runtime and cooking roles

Use `register_asset Type using Registrar;` in the source that owns registration. Include the asset and registrar declarations directly. Use `AssetCodecRegistration<Codec>` for a family codec and `TypedAssetRegistration<Base, Type, Codec>` or its authored alias for a family member. Ordinary standalone assets register through their `loadBinary` definition. Volume entry sources register composite roots with `CompositeRootRegistration<Type>` and authored types that cannot stand alone with `NonStandaloneAssetRegistration<Type>`.
    - A concrete subsystem stores its parent references in `subsystems`. Code that accepts the family interface reads `parentRefs()` or edits `editParentRefs()`. Subsystem fields supply their descriptors and indexed accessors in declaration order. A cook module registers its family policy with `register_asset Member using FamilyCookRegistration<Family, Member, Codec, Policy>;`.

Cook modules use typed free functions and ordinary ADHD includes for asset readers, builders, and dependency functions. The compiler emits their C++ registry and codec entry points under `NWB_COOK`.

- Place `[asset_writer]` on `b8 Write(in Asset asset, mut Vector<u8> output)`. The asset must have an automatically generated codec. The compiler verifies the incoming asset type before calling the writer.
- Place `[cook_entry("label", s_DiagnosticPrefix, ReadValue, document_only, quiet)]` on a function taking one entry record and returning `Expected<Asset>`. The reader has the signature `Expected<Entry> ReadValue(in Name virtualPath, in Path sourcePath, in MetascriptValue asset)`. A reader that schedules parallel work may take a fourth parameter, `in CpuTaskScheduler scheduler`. Document parsing uses the core metadata reader, then calls the same value reader. A `document_only` kind that needs the source document or readable path may instead supply `Expected<Entry> ReadDocument(in Path root, in StringView virtualRoot, in Path source, in MetascriptDocument document)`. A family member supplies its concrete codec type alias as the optional sixth argument. Choose `document_and_value` for assets allowed inside a bunch. Choose `log_failure` when the registry should report a build failure. Keep the attributed builder in the volume registration target so linking that target determines availability.
- Place `[cook_sidecar(s_Extension)]` on the metadata reader to register a paired sidecar extension. The extension must be a `StringView` constant.
    - `[gather_merge] b8 Merge(in Name path, in BinaryByteView incoming, mut BinaryUpdateContext storage)` registers a gathered-payload merge. The storage context supplies the existing bytes and keeps replacement bytes and temporary work in the arenas chosen by the gatherer.
- Place `[cook_registrar]` on `b8 Register(mut CookEntryRegistry registry)` or `b8 Register(mut CookDependencyRegistry registry)` for an explicit group of registrations. Only the defining translation unit registers the function.
  - Use `[cook_document]` on an `AssetMetadataParseResult(mut AssetDocumentMetadataParseContext)` reader and `[volume_prepare]` on a `b8(mut CookVolumePrepareContext)` preparer. A `[cook_setup]` function has signature `void()` and registers cook-only rules once when its module loads.

## 7. Compiler Export Tables (`core/metascript/adhd/export_*.h`, `export_global_prelude.adhd` & `export_core_prelude.adhd`)

- All built-in `.adhd` language and preprocessor tables live in centralized headers and the built-in `global/` and `core/` `extern` preludes under `core/metascript/` and `core/metascript/adhd/`, split between `global/` (`export_global_*.h` + `export_global_prelude.adhd` collecting `global/**/*.adhd`) and `core/` (`export_core_*.h` + `export_core_prelude.adhd` collecting `core/**/*.adhd`) with self-guarding umbrella headers (`export_*.h`):
  - `core/metascript/export_preprocessor.h`: Preprocessor `#` directive names, bool literals (`true`/`false`), raw-brace/raw-string tokens, and include file extension rules queried via `PreprocessorSemantics` (`core/metascript/preprocessor_semantics.h`).
  - `export_keywords.h` (`export_global_keywords.h` + `export_core_keywords.h`): Access specifiers, parameter passing modes, method qualifiers, nested/prohibited/grammar/postfix keywords, `extern` attributes (`cpp_name`, `cpp_header`, `cpp_flags`) and `cpp_flags` entries (`NWB_ADHD_EXPORT_EXTERN_TYPE_FLAG`, `NWB_ADHD_EXPORT_EXTERN_CALLABLE_FLAG`), bool/enum/enum-sentry/cast/unresolved-arena keywords, parallel core/priority kinds, class/interface `asset`/`system`/`subsystem`/`component` attributes, and diagnostic kinds.
  - `export_operators.h` (`export_global_operators.h` + `export_core_operators.h`): Operator tokens, symbols, bracket operators (`"[]"`), overloadability, assignment/boolean/bitwise classifications, binary precedence, member arity rules/diagnostics, and prohibited operator diagnostics.
  - `export_contracts.h` (`export_global_contracts.h` + `export_core_contracts.h`): Class role contracts (`Asset`, `System`, `Component`, `Subsystem`), auto-override method lists, interface constructor forward rules, asset codec contracts, subsystem codegen contracts, preamble header groups, namespace preambles, and file/scope conventions (including `GlobalExternPreludePath`, `CoreExternPreludePath`, and `MaskScopeSuffix`) queried via `ContractSemantics` (`ast/contract_semantics.h`).
  - `export_global_prelude.adhd` & `export_core_prelude.adhd`: Built-in `.adhd` `extern` umbrella preludes (collecting per-header `global/**/*.adhd` and `core/**/*.adhd` files alongside their C++ headers, including metascript visitor parameter declarations) automatically loaded and parsed by `CoreExternRegistry` (`sema/type_utils.cpp`) for regular `extern enum`/`flag` definitions and entries, `extern class` types (with `[cpp_flags]`, fields, methods, `operator[]`, and typed signatures), `extern` free/template functions (with `unwind`, `scratch_arena`, `borrow_view`, `platform_literal_arg0`, and `byte_view_arg0`/`1` call lowerings and typed/visitor parameters), and `extern const` globals.
  - `export_types.h` (`export_global_types.h` + `export_core_types.h`) & `export_functions.h` (`export_global_functions.h` + `export_core_functions.h`): Compiler-intrinsic contracts including scalar types, object types (`PlatformStringView`, `Optional`, `Expected`, `Shared`, `SharedPool`, `PlanReservation`, `AssetRef`, `Func`, etc.), `MOVE_ONLY_TYPE`, SIMD types, and visitor rule macros.
  - `export_types.h` (`export_global_types.h` + `export_core_types.h`): Scalar types and limits, special built-in types (`SPECIAL_TYPE`), arena object types (`ARENA_TYPE`), pointer-like generics (`POINTER_LIKE_GENERIC_TYPE`), subsystem vector containers, literal-const object types, format-arg wrap rules, aligned and unaligned SIMD vector/matrix types, compiler-special object types (`String`, `StringView`, `CompactString`, `Name`, `Path`, `TimerDelta`), non-copyable base interfaces (with C++ export names), generic container/wrapper types, binary hash types, `.nwb` value deserialization rules, implicit conversions, swizzle lanes, and literal token mappings (`export_global_types.h` and `export_core_types.h` retain intrinsic compiler contracts only).
  - `export_functions.h` (`export_global_functions.h` + `export_core_functions.h`): Built-in function argument rules, numeric scalar and container arg constraint descriptors, visitor rules, special function/method semantics, SIMD lane-zero rules, compiler-intrinsic free/template functions (`PromoteSimd`, `LaneScalar`, `MatrixMultiply`, `Unexpected`, `CallTemplateArg0OrFixedType`, `wrapsFormatArgs`, `rejectsSimdStorage`, custom `callLowering`, and deduced-`<T>` intrinsics), SIMD float/double/int/lane/reduction/zero-preserving intrinsics, built-in member and intrinsic receiver methods (`Name`, `String`, `CompactString`, `StringView`, `Path`, `AssetRef`, and container `MEMBER_METHOD`), and built-in fields (`AssetRef::virtualPath`) (`export_global_functions.h` and `export_core_functions.h` retain intrinsic compiler contracts only).
  - `clang/export_cpp_importer.h` & `codegen/codegen_symbols.h`: C++ header importer mappings/flags/rules (`CppImporterSemantics`) and emitted C++ runtime/SIMD codegen helper symbols (`CodegenSymbols`).
- Visitor parameters (`in Func<...>`) in `export_core_prelude.adhd` (and `export_functions.h`) encode value type, optional key type, and whether a mutable receiver can pass a mutable value. The checker uses these rules for direct and stored callables.
- Query these tables through `PreprocessorSemantics`, `KeywordSemantics`, `OperatorSemantics`, `BuiltinTypeSemantics`, and `ContractSemantics`, plus the helpers in `ast/ast_common.h`, `sema/type_utils.h`, `clang/cpp_header_importer_internal.h`, and `codegen/codegen_symbols.h`. Never scatter hard-coded string or token checks for built-in keywords, operators, types, functions, contracts, or runtime helper symbols across the lexer, preprocessor, parser, C++ importer, semantic analyzer, or codegen.
