# `adhd` (`.adhd`) Language Design Note

> [!NOTE]
> Living specification for the `adhd` language (`.adhd`). `.adhd` sources compile into C++ headers and translation units with automatic arena management, member layout optimization, reflection, asset cooking bindings, parallel task lowering, and typed C++ SDK interop.

---

## 1. Overview & Goals
- **Language Name**: `adhd`
- **File Extension**: `.adhd`
- **Compilation Pipeline**: `.adhd` source -> `core/metascript/adhd` compiler (`nwb_adhd_compiler`) -> generated `.adhd.h` / `.adhd.cpp` C++ source and optional `--emit-cpp-externs` / `--depfile` outputs.
- **Core Guarantees**:
  1. **Automatic arena propagation**: Root arena owners (`[arena]`, `[arena, asset(...)]`, `[arena, subsystem(...)]`, `[arena, component(...)]`, `[arena, system(...)]`) supply persistent storage. Ordinary arena-backed classes borrow their owner's arena automatically, and local temporary allocations borrow a stack-scoped `ScratchArena`. Explicit `*Arena` types are prohibited in `.adhd`.
  2. **Compile-time layout & serialization**: Non-`[serialize]` classes automatically reorder fields to minimize alignment padding. `[layout(alignment)]` fixes power-of-two ABI alignment while keeping field order; `[serialize]` preserves authored order and generates binary codecs, `.nwb` metadata parsing, and `AssetRef<T>` dependency extraction.
  3. **Explicit mutability, borrowing & lifetime checks**: Mandatory member access specifiers (`public`, `private`, `protected`), parameter modes (`in`, `mut`, `in borrow`, `in cpp_ref`, `mut forward`, `mut copy`), mandatory `this.` qualification, and compile-time lifetime checks for borrowed views and references.
  4. **Built-in parallel multithreading**: First-class `parallel_for` loops and `parallel` task DAG blocks lowered directly to `Core::CpuTaskScheduler` and `Core::CpuTaskScope`.
  5. **Typed C++ SDK interop**: Built-in `global/` and `core/` `extern` preludes, optional libclang C++ header import, explicit C++ unwind/operator/alias interop attributes, and separate `.h`/`.cpp` C++ boundary files bound via typed `extern` declarations (`native` blocks are retired and prohibited).

---

## 2. File Shape, Declarations & Inheritance Model

- **File Layout Conventions**:
  - Filenames use lowercase `snake_case.adhd`, UTF-8 encoding (no BOM), and Windows `CRLF` line endings with a banner (`// limztudio@gmail.com` followed by a 128-slash divider) and a closing 128-slash divider followed by `\r\n\r\n`.
  - Canonical top-to-bottom section order:
    1. `.adhd` `#include` directives (`#include <...>` / `#include "..."`) and optional public domain umbrella `namespace_wrapper MACRO_NAME;`
    2. File-scope `const` compile-time constants and `type` aliases
    3. `enum<SizeType>` and `flag<SizeType>` declarations
    4. `interface` declarations
    5. `class` and `extern` declarations
    6. Top-level free functions, out-of-line `ClassName::methodName` definitions, asset/cook registrations, and file-scope `static_assert` checks
- **`class` (Concrete Data + Methods)**:
  - Unifies C++ `class` and `struct` under `class`.
  - Ordinary `class` declarations are emitted `final` in C++ and cannot inherit from another `class` (composition over inheritance), permitting devirtualized calls and member layout reordering.
  - **Opt-in concrete inheritance (`[extensible]`)**: A non-template `[extensible] class` may serve as a single public authored concrete base for derived classes (`class Derived : Base`). `[extensible]` classes are noncopyable and nonmovable, construct the base before derived fields (forwarding the caller's arena when needed), require a default constructor on the base, support virtual destruction through the base, allow derived access to `public` and `protected` base members, and allow overriding inherited `interface` methods. Mark an intermediate derived class `[extensible]` to permit further derivation.
  - **Implementing `interface`s & `NoCopy`**: A `class` may implement one or more `interface`s and opt into noncopyable semantics via the built-in `NoCopy` base (`class Widget : IWidget, NoCopy`). `[arena]`, `[asset(...)]`, `[subsystem(...)]`, and `[system(...)]` classes inherit noncopyable framework bases automatically.
  - **External polymorphic C++ bases (`[cpp_base(...)]`)**: A class may derive from a direct external `[cpp_flags(polymorphic_base)]` `extern class` or `interface` and forward constructor parameters via `[cpp_base(ExternalBase, first, second)]` on an inline constructor.
  - **Mandatory `override` (No `virtual` Keyword)**: `interface` methods are abstract contracts without `virtual`. Every overriding method in an implementing or derived class must end with `override` (`public void execute()override;`, `public u32 id()const override;`). Omitting `override` on an overridden method, or marking a non-overriding method `override`, is a compile-time error.
- **`interface` (Pure Polymorphic Contract — No Heap Boxing)**:
  - Contains method declarations only (no member fields) and cannot be instantiated by value or heap-boxed. An `interface` may itself inherit one or more base `interface`s (`interface IChild : IBase`).
  - `interface` types are used as:
    1. **Parameters**: `in Base` (`const Base&`) and `mut Base` (`Base&`).
    2. **Template constraints**: `<T : Base>` and `<T : IFirst + ISecond>`.
    3. **Rebindable non-null local references**:
       ```cpp
       Impl b = Impl();
       Impl c = Impl();
       mut Base a = b;
       a = c;
       in Base ro = b;
       ```
- **`enum<SizeType> Name` & `flag<SizeType> Name` (Mandatory Sized Type, Direct Scoped Access)**:
  - Every `enum` and `flag` must specify an underlying integer size via `<SizeType>`:
    ```cpp
    enum<u8> Mode{
        Idle = 0,
        Active = 1,
        kCount
    }

    flag<u32> RenderMask{
        None = 0u,
        Opaque,
        Shadow,
    }
    ```
  - Enumerators are accessed via `Type::Entry` or `var::Entry`. Count sentries must be named `kCount` (using `Count` is a compile error).
  - `flag<SizeType>` auto-shifts uninitialized entries after `0` (`1 << 0`, `1 << 1`, ...) and emits bitwise operators (`|`, `&`, `^`, `~`, `|=`, `&=`, `^=`) automatically. Both `enum` and `flag` lower to NWB's `namespace Name{ enum Enum : SizeType{ ... }; };` pattern (`::Enum` or `::Flag` inside `[reflect]` or `extern` bindings).
- **`type` (Type Alias)**:
  - Declares a type alias or generic type alias (lowers to C++ `using` / `template<...> using`):
    ```cpp
    type EntityId = u64;
    type CompareFn = Func<b8(in i32, in i32)>;
    type NameMap<V> = HashMap<Name, V>;
    ```
- **No `namespace`, No `static`, No `native`, & Strict Top-Level Scope**:
  - `.adhd` has no `namespace`, `struct`, `union`, `static`, `template`, `inline`, `noinline`, or `native` keywords (`native{}` blocks are retired and prohibited; place C++ code in separate `.h`/`.cpp` files bound via typed `extern`).
  - Declaring a `class`, `interface`, `enum`, `flag`, `type`, or function inside another type or local block is a compile-time error; all declarations live at top-level file scope, with stateless helpers written as free functions.
  - A public domain umbrella header may declare `namespace_wrapper NWB_IMPL;` once to export its paired C++ `NWB_IMPL_BEGIN` and `NWB_IMPL_END` macros for that target namespace.
- **No Raw Pointers & No `null` (`Optional<T>` / `Expected<T, E>` Only)**:
  - Raw pointers (`*`), address-of (`&`), arrow (`->`), `new`, `delete`, `NotNull`, `null`, and `nullptr` are prohibited in `.adhd`.
  - Variables and `in`/`mut` parameters are non-null by construction; member and method access always uses `.` syntax (`obj.field`, `this.method()`).
  - Absence and failure use `Optional<T>` (including `Optional<in T>` / `Optional<mut T>` to borrow existing objects), `Expected<T>` / `Expected<T, E>` (including `Expected<in T>` / `Expected<mut T>`), and `AssetRef<T>`.
- **Compile-Time Checks (`static_assert`)**:
  - File-scope `static_assert(condition, "diagnostic");` (message optional) runs after generated C++ types are complete, supporting:
    - `size_of<T>()`, `align_of<T>()`, `offset_of<T>(field)`, `is_standard_layout<T>()`, `is_trivially_copyable<T>()`
    - `is_cpp_type_alias<Owner, Expected>(Alias)`
    - `has_nothrow_rvalue_member<Owner, Result, Argument>(method)`
    - `is_nothrow_call<in Mesh, mut usize>(FunctionName)` / `is_nothrow_call(FunctionName)` (`in T` tests const lvalue, `mut T` tests mutable lvalue, unqualified `T` tests rvalue)
    - `is_cpp_constructible<Owner, in Arg>()` / `is_cpp_constructible<Owner>()`

---

## 3. Member Access, Layout, Ownership, Constructors & Operators

- **Mandatory Access Specifiers**:
  - Every field, constructor, destructor, operator, and method inside a `class` must begin with `public`, `private`, or `protected` (C++ `public:` section labels are prohibited):
    ```cpp
    class TransformComponent : IComponent{
        public f32x4 position;
        public f32x4 rotation;
        private u32 dirtyFlags = 0u;
        private b8 active = true;
    }
    ```
- **Class Layout Modes**:
  - **Default `class`**: Automatically reorders fields by descending alignment and size (`32B -> 16B -> 8B -> 4B -> 2B -> 1B`) to minimize padding. Because emitted C++ member order may differ from authored order, do not construct non-`[serialize]` classes with C++ positional aggregate initialization (`Foo{ a, b }`).
  - **`[ordered]`**: Preserves declaration order so fields construct in authored order and destruct in reverse order when a later field borrows an earlier sibling or depends on its lifetime. In the constructor's leading assignment prefix, a field assignment may borrow an earlier initialized sibling.
  - **`[layout(alignment)]`**: Fixes a plain value record with a power-of-two byte alignment, authored field order, natural member alignment, and tail padding (for C or shader ABI contracts). Combinable with `[serialize]`.
  - **`[serialize]`**: Preserves authored field order, emits `#pragma pack(push, 1)` when fields do not require over-aligned types, and generates binary serialization, `.nwb` metadata parsing, and `AssetRef<T>` dependency extraction. Cannot be combined with `[arena]`.
  - **No Bitfields**: C-style bitfields (`u8 a : 3;`) are prohibited; use `flag<SizeType>`.
- **Ownership & Peer Access**:
  - **`[move_only]` & `MoveInto<T>(destination, source)`**: Marks an ordinary value class as noncopyable and movable (fields must support moves). Moving or move-assigning arena-backed values keeps the destination's arena, transferring container storage directly when arenas match. Call `MoveInto<T>(destination, source)` with two `mut` lvalues of type `T` to transfer ownership explicitly after validation. A `[move_only]` class with direct `NoCopy` base can move-construct when its fields allow it.
  - **`[owner_result]`**: Marks an instance method on a mother-arena class whose return value is independently allocated from the receiver's mother arena rather than caller scratch. A local initialized directly from such a call on `this` or an owned field retains that owner arena for its scope; assignment into an existing variable keeps the destination arena.
  - **`[friend(PeerA, PeerB)]`**: Grants one-way access to `private` and `protected` members to named non-template peer classes in the same namespace (even if defined in separate `.adhd` sources).
- **Constructors, Destructors & Initialization**:
  - Classes support overloaded constructors (`public ClassName(...)`, `public ClassName() = default;`) and destructors (`public ~ClassName()`).
  - Inline field defaults (`public f32 speed = 1.0f;`) are supported. Initializing the same member both inline and in a constructor is a compile-time error, as is leaving any field uninitialized across a constructor.
  - C++ constructor initializer lists (`: m_x(x)`) are prohibited in `.adhd` syntax; assign `this.field = ...;` inside the constructor body (the compiler automatically lifts qualifying leading `this.field = ...;` assignments into C++ member initializers).
- **Operator Overloading & C++ Operator Exports**:
  - Member operators only (`this` is the left-hand side; free/static two-argument operators are prohibited):
    - Allowed: `+`, `-`, `*`, `/`, `%`, `+=`, `-=`, `*=`, `/=`, `%=`, `&`, `|`, `^`, `~`, `<<`, `>>`, `&=`, `|=`, `^=`, `<<=`, `>>=`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `[]`.
    - Forbidden: unary `*`, `->`, unary `&`, `,`, `&&`, `||`, `new`, `delete`, implicit conversion operators.
  - **C++ Interop Exports**:
    - `[cpp_explicit_bool]` on a parameterless `const` method returning `b8` also emits C++ `explicit operator b8()const`.
    - `[cpp_take_assignment]` on a mutable `void` method taking one `mut T` parameter emits a C++ `operator=` accepting values constructible as `T`, preserving ordinary copy and move assignment.
    - `[cpp_rvalue_overload]` on an ordinary instance method taking one `mut` value parameter and returning a scalar, enum, or `void` also emits an rvalue overload for C++ callers.
    - `[cpp_type_alias(Name, Type)]` on a class publishes a public nested C++ `using Name = ...;` alias.

---

## 4. Parameters, `const`, Control Flow, Exceptions, Diagnostics & Split Methods

- **Parameter Passing Modes**:
  - Every typed parameter must declare its mode. Authored callables prohibit default parameter values (use overloads); typed `extern` declarations may represent C++ defaults with placeholders such as `= {}`, and parameters after the first default must also have defaults:
    - `in T`: Read-only borrow (`CallTraits<T>::param_type`: value for small scalars/handles/SIMD vectors, `const T&` for larger types).
    - `mut T`: Mutable reference (`T&`); call sites must supply a mutable lvalue.
    - `in borrow T name`: Retained read-only reference; generated C++ always passes `const T&` even for small types. Inline constructors infer this automatically when assigning an `in` parameter or its subobject into a retained `in` field; out-of-line declarations and forwarding wrappers declare `in borrow` explicitly. Callers must pass a directly bindable lvalue with sufficient lifetime.
    - `in cpp_ref T`: Used on `extern` SDK methods/overrides to preserve a C++ `const T&` parameter signature for small values in virtual overrides.
    - `mut forward T` / `mut copy T`: Used in generic free functions (`noexcept_like(...)`) to accept either mutable lvalues or temporaries (`T&&` forwarding) or pass generic/integer parameters by value.
  - **Fallible Return Shape**: Fallible value-producing functions and methods return `Expected<T>` (or `Expected<T, E>`) with `MakeUnexpected(...)` on failure rather than returning `b8` with an output parameter. Reserve `mut` parameters for in-out state mutations or caller-owned reusable buffers.
- **Role of `const`**:
  1. **File-scope compile-time constants (`const T s_Name = expr;`)**: Lowers to `constexpr` / `inline constexpr` in C++.
  2. **Immutable local variables (`const T value = expr;` / `const auto value = expr;`)**: Lowers to `const` on the local C++ variable.
  3. **Non-type template parameters (`<const usize N>`)**.
  4. **Read-only method qualifier (`public f32 area()const{ ... }`)**: Attached directly to `)` with no space (`)const`, `)const override`).
- **Mandatory `this.` Qualification & Local Initialization**:
  - Inside any class method, every access to a member field or member method must use `this.` (`this.position`, `this.reset()`). Unqualified names resolve only to locals, parameters, constants, or free functions.
  - Every local variable must be initialized at declaration (`u32 count = 0u;`, `auto total = 0.0f;`).
- **Control Flow & Casting**:
  - Supports `if` / `else if` / `else`, `for`, range-based `for(in auto item : list)` / `for(mut T item : list)`, `while`, `do ... while`, `break;`, `continue;`, and `switch`.
  - Ternary `? :` and `goto` are prohibited.
  - Casting uses `cast<T>(x)` (`static_cast`) and `bit_cast<T>(x)` (`BitCast`); C-style casts, `as`, and `dynamic_cast` are prohibited.
  - Every `switch` `case` and `default` branch must terminate with `break;` or `return;`. `switch` accepts integers, `enum`s, strings (`String`, `StringView`, `CompactString`, `Name`), and equality-comparable classes.
- **Exception Model (`noexcept`, `[cpp_unwind]`, `noexcept_like`)**:
  - `try`, `catch`, and `throw` statements are prohibited in `.adhd`.
  - Generated functions, methods, constructors, destructors, and lambdas emit C++ `noexcept` by default.
  - `[cpp_unwind]` allows an ordinary function, method, constructor, destructor, `interface` method, or override to propagate C++ exceptions (`noexcept(false)` on marked destructors; unmarked `noexcept` omitted on marked callables and nested lambdas). Overrides and derived interfaces cannot loosen a nonthrowing base declaration.
  - Generic free function definitions may follow parameters with `noexcept_like(expr, ...)` (or `noexcept_like()` on a single-return body) to emit conditional `noexcept(...)` computed from unevaluated expressions and parameter destruction.
- **Built-In Diagnostics**:
  - `assert(cond);` / `assert(cond, "msg", ...);` -> `FOUNDATION_ASSERT` / `FOUNDATION_ASSERT_MSG`
  - `[essential] assert(...);` -> `FOUNDATION_FATAL_ASSERT` / `FOUNDATION_FATAL_ASSERT_MSG`
  - `info("msg", ...);` / `[essential] info("msg", ...);` -> `NWB_LOGGER_INFO` / `NWB_LOGGER_ESSENTIAL_INFO`
  - `warning("msg", ...);` / `[essential] warning("msg", ...);` -> `NWB_LOGGER_WARNING` / `NWB_LOGGER_CRITICAL_WARNING`
  - `error("msg", ...);` / `[essential] error("msg", ...);` -> `NWB_LOGGER_ERROR` / `NWB_LOGGER_FATAL`
  - Multiline diagnostic calls with format arguments put the message on its own indented line and each format argument on its own continuation line with a leading comma:
    ```cpp
    error(
        "Shader '{}': missing stage '{}'"
        , shaderName
        , stageName
    );
    ```
- **Callables (`Func<...>`) & Lambdas**:
  - `Func<Ret(in A, mut B)>` represents a non-null callable (wrap in `Optional<Func<...>>` when nullable).
  - Lambdas require explicit capture and parameter modes:
    ```cpp
    auto filter = lambda[in threshold, mut hitCount](in f32 distance) -> b8{
        if(distance < threshold){
            ++hitCount;
            return true;
        }
        return false;
    };
    ```
  - Capture-flow analysis lowers stateless callables to `InplaceFunction<0, Ret(Args...)>` (`8` bytes, implicitly convertible to `Ret(*)(Args...) noexcept`) and capturing callables to `InplaceFunction<64, Ret(Args...)>`.
  - `MetascriptDocument.visitVariable`, `MetascriptValue.visitField`, and `forEachMapEntry` accept lambdas, `Func<...>`, unambiguous non-template free functions, or local aliases whose return type does not need a caller arena.
  - **Last-Use Moves & Compiler-Managed Inlining**: The compiler emits `Move(...)` automatically on eligible last-use values and decides automatically whether generated callables are emitted inline in `.adhd.h` or out-of-line in `.adhd.cpp`.
- **Split Methods & `[modular]` Classes**:
  - Out-of-line implementations use `ClassName::methodName` in the declaring file (after the `class`) or in another `.adhd` file when the owning class allows out-of-file definitions: `[arena, asset(...)]`, `[arena, system(...)]`, or non-template `[modular]` classes (`[modular]` requires templated or arena-polymorphic methods to be defined in the declaring class file).
  - Cross-file `.adhd` dependencies use `#include <...>` / `#include "..."`.

---

## 5. Built-In Types & Library Contracts

### 5.1 Scalar Types & Limits
| `adhd` Type | C++ Type (`global/type.h`) | Size / Align | Description |
| :--- | :--- | :--- | :--- |
| `b8` | `b8` | 1 B | Boolean (`true` / `false`) |
| `i8` / `u8` | `i8` / `u8` | 1 B | 8-bit signed / unsigned integer |
| `i16` / `u16` | `i16` / `u16` | 2 B | 16-bit signed / unsigned integer |
| `i32` / `u32` | `i32` / `u32` | 4 B | 32-bit signed / unsigned integer |
| `i64` / `u64` | `i64` / `u64` | 8 B | 64-bit signed / unsigned integer |
| `isize` / `usize` | `isize` / `usize` | 8 B (64-bit) | Pointer-sized signed / unsigned integer |
| `f32` | `f32` | 4 B | 32-bit IEEE-754 float |
| `f64` | `f64` | 8 B | 64-bit IEEE-754 double |
| `void` | `void` | — | Return-only empty type |

Numeric limits use postfix constants `T.min` and `T.max` (e.g., `u32.max`, `f32.min`). C scalar keywords (`bool`, `int`, `float`, `double`, `char`, `short`, `long`, `unsigned`, `signed`) are prohibited.

### 5.2 SIMD Calculation Types vs. Memory Storage Types
- **Calculation / Register Types**:
  - `VectorSingle` (`::SIMDVectorSingle`), `VectorDouble` (`::SIMDVectorDouble`), `MatrixSingle` (`::SIMDMatrix`), `MatrixDouble` (`::SIMDMatrixDouble`). Used for parameters, local math, and return values (lowering inside `Optional`/`Expected` to `::SIMDVectorSingleResult` / `::SIMDVectorDoubleResult`). Rejected in `class` fields, containers, and file-scope `const` declarations.
  - Built-in stack-only aggregate results (`MatrixDecomposition`, etc.) that hold SIMD registers are similarly allowed as locals and `in`/`mut` parameters, not stored in classes or heap containers.
- **Aligned Memory-Layout Types**:
  - `16`-byte aligned: `f32x4` (`Float4`), `i32x4` (`Int4`), `u32x4` (`UInt4`), `f32x3_i32` (`Float3Int`), `f32x3_u32` (`Float3UInt`), `f32x3x4` (`Float34`), `f32x4x4` (`Float44`).
  - `32`-byte aligned: `f64x2` (`Double2`), `f64x3` (`Double3`), `f64x4` (`Double4`), `i64x4` (`Int64x4`), `u64x4` (`UInt64x4`), `f64x3_i64` (`Double3Int`), `f64x3_u64` (`Double3UInt`), `f64x3x3` (`Double33`), `f64x3x4` (`Double34`), `f64x4x4` (`Double44`).
- **Unaligned Memory-Layout Types (`*u`)**:
  - Vectors: `f16x2u`, `f16x4u`, `f32x2u`, `f32x3u`, `f32x4u`, `i32x2u`, `i32x3u`, `i32x4u`, `u32x2u`, `u32x3u`, `u32x4u`, `f64x2u`, `f64x3u`, `f64x4u`, `i64x2u`, `i64x3u`, `i64x4u`, `u64x2u`, `u64x3u`, `u64x4u`.
  - Matrices: `f32x3x3u`, `f32x3x4u`, `f32x4x4u`, `f64x3x3u`, `f64x3x4u`, `f64x4x4u`.
- **Load/Store, Swizzling & Safety**:
  - Construct calculation values via `VectorSingle(storage)` / `VectorDouble(storage)` / `MatrixSingle(storage)` / `MatrixDouble(storage)` (and lane constructors), access lanes `.x`/`.y`/`.z`/`.w` and `.r`/`.g`/`.b`/`.a` or multi-lane swizzles (`v.xyz`, `v.zyx`, `v.xyzw`), and store via `f32x4(v)`, `f32x3u(v)`, `f32x3x4(m)`, etc.
  - Math/SIMD intrinsics (`Min`, `Max`, `Abs`, `Clamp`, `Saturate`, `Floor`, `Ceil`, `Round`, `Sqrt`, `Rsqrt`, `Lerp`, `Dot`, `Cross`, `Length`, `LengthSq`, `Normalize`, `Mul`, `Transpose`, `Inverse`, `Sin`, `Cos`, `Tan`, `ASin`, `ACos`, `ATan`, `ATan2`, `Exp`, `Exp2`, `Log`, `Log2`, `Pow`, `IsFinite`, `DivideUp`, `AlignUp`, `ModF`, `MatrixIdentity`, plus checked integer ops and `global/simd/*` collision/quaternion/rect helpers) lower directly.
  - `IsFinite(v)` uses integer bit checks (`VectorIsFinite` / `VectorDoubleIsFinite`) so signaling NaNs do not raise `FE_INVALID`, and the compiler emits `#pragma STDC FENV_ACCESS ON` around scopes evaluating SIMD floating-point expressions.

### 5.3 Containers, Strings & Framework Handles
- **Containers** (no C-style `T[N]`):
  - Sequential: `Array<T, Capacity>`, `FixedVector<T, Capacity>`, `Vector<T>`, `ParallelVector<T>`, `ForwardList<T>`, `List<T>`, `Deque<T>`
  - Associative: `Set<T>`, `HashSet<T>`, `ParallelHashSet<T>`, `Map<K, V>`, `HashMap<K, V>`, `ParallelHashMap<K, V>`
  - Queues & utilities: `Queue<T>`, `ParallelQueue<T>`, `ParallelBlockQueue<T, BlockSize>`, `CompressedPair<A, B>`, `FixedIndexMap<K>`
  - Owning containers lower to arena-backed `Container<..., ::ArenaBase>` (or `Container<..., ArenaCacheAlignedAllocator<...>>` for `Parallel*`), rebinding across arenas automatically on construction/assignment.
- **Views & Implicit Conversions**:
  - `Span<T>` is read-only (`Span<const T>`). `Vector<T>`, `Array<T, N>`, and `FixedVector<T, N>` convert implicitly to `Span<T>`.
  - `StringView` (`AStringView`) is a read-only UTF-8 view; `String` and `CompactString` convert implicitly to `StringView`.
- **UTF-8 Strings, Paths & Text Helpers**:
  - Source literals are plain UTF-8 `"..."` (`\xHH`, `\uXXXX`, `\UXXXXXXXX`). `PlatformTextLiteral("text")` produces a static `PlatformStringView` (`::TStringView`) for platform callbacks without `FOUNDATION_TEXT`.
  - Built-in types: `String` (`AString`), `CompactString` (`ACompactString`), `StringView` (`AStringView`), `PlatformStringView` (`::TStringView`), `Name` (`::Name`), `Path` (`::Path`), `TimerDelta` (`::TimerDelta`). Text helpers (`StringFormat`, `StringAppendFormat`, trimming, case-insensitive ASCII checks, numeric/Name parsing, JSON/DOT/UTF-8/grapheme utilities) and filesystem types (`ExecutablePaths`, `DirectoryEntry`, `DirectoryIterator`, `StagedDirectoryPaths`, `StagedFilePairPaths`, `StagedFileWriter`, etc.) are available directly.
- **Handles, Wrappers & Graph Tasks**:
  - `Optional<T>` (including `Optional<in T>` / `Optional<mut T>`), `Expected<T>` / `Expected<T, E>` (with `Failure` default error, including `Expected<in T>` / `Expected<mut T>`), `AssetRef<T>`, `Atomic<T>`, `UniquePtr<T>`, `NotNullUniquePtr<T>`, `RefCountPtr<T>`, `ComponentPool<T>`, `TypedAsset<T>`, `AssetCodec<T>`.
  - `Shared<T>` & `SharedPool`: `pool.make<T>(...)` returns `Expected<Shared<T>>`; `pool.makePrepared<T>(value)` transfers a prepared value (keep its original arena and borrowed dependencies alive until the last `Shared<T>` handle releases).
  - `PlanReservation<Control, Plan>`: Moves ownership of a reservation; call `release()` after acceptance or `discard()` / scope exit to return it.
  - GPU task payloads: `graph.addPayloadTask(desc, Payload(...))`, `graph.addPreparedPayloadTask(desc, payload)`, and `graph.addPayloadTaskFrom(...)` accept payload types implementing `commandRequirements()const` and `record(mut CommandList, in GpuTaskRecordContext)const`, plus optional `accepted(in QueueSubmissionToken)` and `discarded()` (lifecycle-only payloads may omit recording methods).

---

## 6. Arena Memory Management Model

1. **No Explicit `*Arena` Types**:
   - Referencing `ArenaBase`, `AssetArena`, `CookArena`, `GlobalArena`, `GraphicsArena`, `MetaArena`, `PersistentArena`, `ScratchArena`, or `TelemetryArena` anywhere in `.adhd` is a compile-time error.
2. **Root Owners (`[arena]`, `[arena, asset(...)]`, `[arena, subsystem(...)]`, `[arena, component(...)]`, `[arena, system(...)]`)**:
   - Asset, subsystem, component, and system classes must pair their role attribute with `arena`.
   - Standalone `[arena]` and `[arena, component("...")]` classes own a `s_ClassArena` (`GlobalArena`).
   - `[arena, asset("...")]` classes derive from `TypedAsset<T>` (or `TypedAsset<T, BaseAssetInterface>`), and `[arena, subsystem("...")]` classes derive from `TypedAsset<T, ISubsystem>` (`parentRefs()`, `editParentRefs()`, `subsystems`); both allocate from `AssetArena`. Candidate asset locals inside their methods bind to `*this->__adhd_arena` so `this = candidate;` moves cleanly while preserving `IAsset` state.
   - `[arena, system("...")]` classes derive from `Core::ECS::ISystem` and forward `__adhd_mother_arena`.
3. **Mother-Arena Borrowing & `[owner_result]`**:
   - Ordinary non-root classes holding arena-backed fields receive a hidden `::NotNull<::ArenaBase*> __adhd_arena` (`uses_leading_arena_t`) and borrow their parent owner's or local caller's arena automatically.
   - Methods marked `[owner_result]` allocate their returned arena-backed snapshot from the receiver's mother arena rather than the caller's scratch arena.
4. **Local `ScratchArena` & Return Arena Forwarding**:
   - Local arena-backed variables, temporaries, and helper calls automatically borrow a stack-scoped `ScratchArena` (`__adhd_scratch`), reclaiming scratch storage when the root scope exits.
   - Functions returning arena-backed values receive a hidden leading `::ArenaBase& __adhd_return_arena` parameter in generated C++ so callers pass either their local scratch arena or a destination owner arena.

---

## 7. Multithreading (`parallel_for` & `parallel`)

- **`parallel_for`**: Accepts index (`parallel_for(usize i = 0u; i < count; ++i)`) and range (`parallel_for(in auto item : list)` / `parallel_for(mut auto item : list)`) forms, lowering to `Core::CpuTaskScheduler::parallelFor`.
- **`parallel` DAG Tasks & Callable Dispatch**:
  - Named task blocks support dependency edges (`parallel downstream : upstream{ ... }`) where prerequisites precede dependents, plus `[core(Heavy | Light | Any | MainThread)]` and `[priority(Critical | Normal | Background)]`. Active tasks auto-join before scope exit or at explicit `wait(...)` boundaries.
  - Callable dispatch (`parallel workerFn(i, offset);`) evaluates `in` arguments immediately by value on the submitting thread while passing `mut` arguments and captures by reference into `Core::CpuTaskScope`.
- **Compile-Time Concurrency Rules**: The checker rejects shared mutation of known non-thread-safe containers across `parallel_for` and concurrent `parallel` blocks. Disjoint slice safety and foreign/C++ aliasing inside external `extern` calls remain the author's responsibility.

---

## 8. Preprocessor & Macro System

- Preprocessing runs before AST parsing, member reordering, and code generation:
  - `#include` resolves `.adhd` files with automatic deduplication (while C++ headers are bound via `[cpp_header("<...>")]` / `extern "<...>"`).
  - Supports `#define` (object-like and function-like), `#undef`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, and `defined(...)`.
  - Macro definitions come from `#define` directives, `--define` / `-D` CLI flags, and CMake `nwb_target_adhd_definitions`. Do not assume C++ target/platform build macros exist in `.adhd` unless explicitly defined for the ADHD pass.

---

## 9. Attributes, Asset Registration & Cooking Roles

- **Single-Bracket Rule**: Multiple attributes on a declaration must be comma-separated inside one `[...]` (`[arena, asset("mesh")]`); stacked `[...][...]` blocks are prohibited.
- **Attributes by Declaration Kind**:
  - `class`: `[arena]`, `[modular]`, `[extensible]`, `[ordered]`, `[layout(alignment)]`, `[serialize]`, `[reflect]`, `[move_only]`, `[friend(PeerA, ...)]`, `[cpp_type_alias(Name, Type)]`, `[asset("type_name")]`, `[subsystem("type_name")]`, `[component("type_name")]`, `[system("type_name")]`
  - `interface`: `[reflect]`, `[asset("type_name")]`
  - `enum` / `flag`: `[reflect]`
  - Member field: `[reflect]`, `[transient]`
  - Method / constructor / destructor: `[reflect]`, `[owner_result]`, `[cpp_unwind]`, `[cpp_rvalue_overload]`, `[cpp_explicit_bool]`, `[cpp_take_assignment]`, `[cpp_base(Base, param0, ...)]`
  - Free function: `[cpp_unwind]`, `[asset_writer]`, `[cook_entry(...)]`, `[cook_sidecar(s_Ext)]`, `[gather_merge]`, `[cook_registrar]`, `[cook_document]`, `[volume_prepare]`, `[cook_setup]`
  - Diagnostics & scheduling: `[essential]` on `assert`/`info`/`warning`/`error`; `[core(...)]`, `[priority(...)]` on `parallel` / `parallel_for`
  - `extern`: `[cpp_name("...")]`, `[cpp_header("<...>")]`, `[cpp_flags(...)]` on `extern` declarations
- **Asset Runtime & Cooking Declarations**:
  - Standalone and family asset registration: `register_asset Type using Registrar;` at file scope using `AssetCodecRegistration<Codec>`, `TypedAssetRegistration<Base, Type, Codec>` (or authored alias), `CompositeRootRegistration<Type>`, `NonStandaloneAssetRegistration<Type>`, or cook-side `FamilyCookRegistration<Family, Member, Codec, Policy>`.
  - Cook attributes emit `NWB_COOK` registry/codec bindings from typed ADHD free functions:
    - `[asset_writer]` on `b8 Write(in Asset asset, mut Vector<u8> output)`
    - `[cook_entry("label", s_Prefix, ReadValue, document_only | document_and_value, quiet | log_failure [, CodecAlias])]` on entry builders returning `Expected<Asset>`
    - `[cook_sidecar(s_Extension)]`, `[gather_merge]`, `[cook_registrar]`, `[cook_document]`, `[volume_prepare]`, `[cook_setup]`
  - **Framework Registration Macros**: `extern registration MacroAlias(arg0, arg1);` with `[cpp_header("<...>"), cpp_name("C_MACRO")]` allows file-scope `registration MacroAlias(Label, Callback);` (e.g., `NWB_ADHD_GTEST` / `NWB_ADHD_GTEST_F` in `tests/common/gtest_macros.adhd`).

---

## 10. C++ Interop: Typed `extern` Preludes & Header Importer

- **Typed `extern` Declarations**:
  - `extern enum`, `extern flag`, `extern class`, `extern` functions/templates, `extern const`, and `extern registration` bind C++ APIs into `.adhd` type checking using `[cpp_header("<...>")]`, `[cpp_name("...")]`, and `[cpp_flags(...)]`:
    - Type flags: `self_defaulted`, `small_value`, `borrow_view`, `binary_serializable`, `brace_init`, `equality_switch`, `non_thread_safe`, `map_key`, `arena_first_ctor`, `arena_omit_ctor`, `no_copy`, `no_assign`, `abstract`, `polymorphic_base`.
    - Callable flags: `scratch_arena`, `borrow_view`, `platform_literal_arg0`, `byte_view_arg0`, `byte_view_arg1`, `unwind`.
  - **Built-in Preludes & On-Demand Headers**: `export_global_prelude.adhd` (collecting `global/**/*.adhd`) and `export_core_prelude.adhd` (collecting `core/**/*.adhd`) are automatically parsed with the compiler. Semantic analysis tracks referenced unshadowed symbols (`requiredGlobalHeaders`) so generated headers include only the headers actually used. User declarations always shadow built-in exports.
- **Authoring Boundary**:
  - Raw C++ `native` blocks are retired and prohibited in `.adhd`. C++ code (such as third-party SDK adapters, C-ABI interop, OS window/entry bootstrap in `loader/` and `logger/`, the ADHD compiler `core/metascript/`, and `pipeline/adhd_compiler/`) lives in separate `.h`/`.cpp` files bound into `.adhd` via typed `extern` declarations (`loader/project_entry.adhd`, `logger/rgd/decode.adhd`, `utilities/*_sdk.adhd`, etc.).
- **libclang C++ Header Importer (`core/metascript/adhd/clang/`)**:
  - `--cpp-header`, `--cpp-include-dir`, `--cpp-define`, `--cpp-arg`, and `--emit-cpp-externs` parse C++ headers using the vendored static libclang (`3rd_parties/llvm`), translate supported declarations into pointer-free `extern` `.adhd` AST nodes (respecting pointer-free, arena, and container mapping rules in `export_cpp_importer.h`), and optionally emit generated `.adhd` `extern` sources.

---

## 11. Templates & Generics

- Generic parameters are written inline in `<...>` without `template<typename ...>`:
  ```cpp
  class Pair<T, U>{
      public T first;
      public U second;
      public void reset(in T defaultFirst, in U defaultSecond);
  }

  void Pair<T, U>::reset(in T defaultFirst, in U defaultSecond){
      this.first = defaultFirst;
      this.second = defaultSecond;
  }
  ```
- **Interface Constraints**: Multiple constraints separate with `+` (`<T : IComponent + IValidator>`).
- **Compile-Time Value Parameters**: `<T, const usize Capacity>`.
- **Generic Free Function Exceptions**: Generic free function definitions may use `noexcept_like(...)` together with `mut forward T` and `mut copy T`.
- **Per-Instantiation Layout & Reflection**: Each concrete template instantiation computes its own member layout optimization and reflection tables.

---

## 12. Compiler Architecture, CLI & Build Integration

1. **Shared Foundation & `.nwb` Evaluation (`core/metascript/`)**:
   - `lexer.*`, `parser_cursor.*`, `preprocessor.*`, `preprocessor_includes.*`, `preprocessor_macro_expander.*`, `preprocessor_eval.cpp`, `preprocessor_semantics.h`, `attribute_parser.*`, `literal_decoder.*`, `value.*`, `operation_validator.*`, `expression_parser.*`, `statement_parser.*`, `parser.*`, and their companion `.adhd` `extern` bindings.
2. **`adhd` Compiler Library (`core/metascript/adhd/`)**:
   - **Entry & Centralized Tables**: `compiler.h` / `compiler.cpp`, `global.h`, `export_keywords.h`, `export_operators.h`, `export_contracts.h`, `export_types.h`, `export_functions.h` (each split into `export_global_*.h` + `export_core_*.h`), plus `export_global_prelude.adhd` and `export_core_prelude.adhd`.
   - **AST (`ast/`)**: `ast_common.*`, `ast_types.h`, `ast_expr.*`, `ast_stmt.*`, `ast_decl.h`, `builtin_type_semantics.h`, `contract_semantics.h`, `keyword_semantics.h`, `operator_semantics.h`.
   - **C++ Importer (`clang/`)**: `export_cpp_importer.h`, `cpp_header_importer.h`, `cpp_header_importer.cpp`, `cpp_header_importer_internal.h`, `cpp_header_importer_type_mapper.cpp`, `cpp_header_importer_record_builder.cpp`, `cpp_header_importer_formatter.cpp`.
   - **Parser (`parser/`)**: `type_parser.*`, `expr_parser.*`, `stmt_parser.*`, `stmt_parser_special.cpp`, `decl_parser.*`, `decl_parser_class.cpp`, `decl_parser_callable.cpp`.
   - **Semantic Analysis & Layout (`sema/`)**: `symbol_table.*`, `const_eval.*`, `type_utils.*`, `type_comparator.*`, `type_resolver.*`, `type_substitution.*`, `type_layout_analyzer.*`, `concrete_type_analyzer.*`, `asset_type_analyzer.*`, `declaration_dependency_analyzer.*`, `template_instantiation_collector.h`, `borrow_lifetime_analyzer.*`, `last_use_move_analyzer.*`, `lambda_capture_analyzer.*`, `lambda_expression_checker.*`, `callable_argument_checker.*`, `constructor_argument_checker.*`, `callable_declaration_linker.*`, `callable_overload_selector.*`, `callable_template_deduction.*`, `binary_expression_checker.*`, `local_variable_checker.*`, `return_statement_checker.*`, `type_checker.*` / `type_checker_*.cpp`, `layout_optimizer.*`, `layout_instantiation.cpp`, `parallel_graph.*`.
   - **Code & Reflection Generation (`codegen/`)**: `codegen_symbols.h`, `cpp_type_emitter.*`, `header_preamble_emitter.*`, `header_emitter.*`, `header_emitter_decl.cpp`, `header_emitter_class.cpp`, `asset_class_header_emitter.*`, `asset_cook_role_emitter.*`, `binary_serialization_emitter.*`, `constructor_argument_emitter.*`, `builtin_call_emitter.*`, `reflection_emitter.*`, `reflection_emitter_nwb.cpp`, `nwb_reflection_emitter.h`, `parallel_statement_emitter.h`, `switch_statement_emitter.*`, `simd_*`, `source_emitter.*`, `source_emitter_*.cpp`.
3. **Pipeline Compiler Tool (`pipeline/adhd_compiler/`)**:
   - `global.h`, `compile.h` / `compile.cpp`, `output_transaction.h` / `output_transaction.cpp` (atomic staged writes with backup rollback), and `module.cpp` (using bootstrap `pipeline/command_line.*` + `pipeline/main.cpp`).
   - **CLI Options**: `--input`, `-o`/`--output`/`--output-dir`/`--output-directory`, `-I`/`--include-dir`, `-D`/`--define`, `--namespace`, `--namespace-map`, `--depfile`, `--cpp-header`, `--cpp-include-dir`, `--cpp-define`, `--cpp-arg`, `--emit-cpp-externs`.
   - **`--depfile` Safety**: Rejects paths containing tabs or newlines, unsupported punctuation, leading `~`, trailing `:` or `\`, or `\` immediately before `#`, `:`, or `$`, keeping existing outputs untouched on error.
   - **Minimal Constant-Unit Preambles**: Files containing only literal `Name` constants or only literal/simple-arithmetic scalar, boolean, or `StringView` constants emit minimal global headers rather than full core preambles.
4. **CMake Build Integration (`configuration/Targets.cmake`)**:
   - `nwb_target_adhd_sources(<target> [NAMESPACE "NWB::..."] SOURCES ...)`: Invokes `nwb_adhd_compiler` per `.adhd` source with generated depfiles, emits `.adhd.h` / `.adhd.cpp` under `${CMAKE_BINARY_DIR}/adhd_gen/<target>/`, attaches public header and private source dependencies, and tracks `_NWB_CORE_PRELUDE_ADHD_FILES` (`global/**/*.adhd`, `core/**/*.adhd`) and `nwb_impl_adhd_headers`.
   - `nwb_target_adhd_definitions(<target> ...)`: Adds compile definitions to both the C++ target and `NWB_ADHD_DEFINITION_ARGUMENTS` (`--define=...`).
   - `nwb_finalize_adhd_namespaces()`: Emits `${CMAKE_BINARY_DIR}/adhd_namespaces.txt` mapping `.adhd` sources to target namespaces so cross-module includes resolve exact C++ namespaces.
