# `adhd` (`.adhd`) Language Design Note

> [!NOTE]
> Living specification for the `adhd` language (`.adhd`). `.adhd` files compile into C++ source code with generated reflection and automatic arena-based memory management.

---

## 1. Overview & Goals
- **Language Name**: `adhd`
- **File Extension**: `.adhd`
- **Compilation Pipeline**: `.adhd` source -> `adhd` compiler -> generated `.h` / `.cpp` C++ source + reflection tables.
- **Core Guarantees**:
  1. **Zero manual allocator boilerplate**: Every class/component owns a static arena for persistent member allocations; functions use a local `ScratchArena` for temporary allocations.
  2. **First-class Reflection**: Every `class`, `interface`, `enum`, member field, and method is reflected for `.nwb` asset cooking, binary serialization, and ECS inspection.
  3. **SIMD Alignment & Optimal Memory Layout**: All SIMD types are strictly aligned (never unaligned `*U`), and the `adhd` compiler automatically reorders class members to minimize padding.
  4. **Explicit Mutability & Access**: Mandatory member access determiners (`public`, `private`, `protected`) and parameter passing determiners (`in`, `mut`).
  5. **Built-in Parallel Multithreading**: First-class `parallel_for` and `parallel` blocks integrated with `Core::CpuTaskScheduler`.

---

## 2. Type Declarations (`class`, `interface`, `enum`, `type`) & Inheritance Model
- **`class` (Concrete Data + Methods, Always `final`)**:
  - Unifies C++ `class` and `struct` into a single `class` keyword.
  - **No `class`-to-`class` inheritance (Rust-style)**: A `class` **cannot** inherit from another `class` (composition over inheritance). Every generated C++ `class` is `final`, allowing direct non-virtual calls and unobstructed compiler member reordering.
  - A `class` may implement one or more `interface`s (`class ModelComponent : IComponent`).
  - No `virtual` or `override` keywords are needed; implementing an `interface` is satisfied by providing methods with matching signatures.
- **`interface` (Pure Polymorphic Contract)**:
  - Pure interface type (maps to NWBLot's lowercase `interface` convention in `global/compile.h`).
  - An `interface` contains **method declarations only** (no member variables).
  - Variables or containers typed by an `interface` (e.g., `Vector<IComponent>`) can hold any implementing `class` instance polymorphically, allocated from the owning class/component arena.
- **`enum<SizeType>` (Mandatory Sized Type, NWB-Style `Enum` & `Flag`)**:
  - Every `enum` declaration **must** explicitly specify its underlying integer size via `<SizeType>` (e.g., `enum<u8> Name`).
  - Inside `enum<SizeType> Name`, entries are prefixed with `enum` (ordinal/value enumerators) or `flag` (bitflag enumerators):
    ```cpp
    enum<u8> Name {
        enum Foo = 1,
        flag Bar = 1 << 2,
    };
    ```
  - **C++ Codegen Lowering (NWB Style)**:
    ```cpp
    namespace Name {
        enum Enum : u8 {
            Foo = 1,
        };
        enum Flag : u8 {
            Bar = 1 << 2,
        };
    };
    ```
  - **Usage & Sized Operator Interop**:
    - Variable declaration and scoped access:
      ```cpp
      Name var;
      var::Enum::Foo;
      var::Flag::Bar;
      ```
    - Because the underlying integer size (`u8`, `u32`, etc.) is explicitly specified, all arithmetic, bitwise, and comparison operators (`==`, `!=`, `+=`, `|=`, etc.) work directly with the matching sized integer type:
      ```cpp
      u8 counter = 0;
      counter += Name::Enum::Foo;
      ```
- **`type` (Rust-Style Type Alias)**:
  - Declares a type alias or generic type alias (lowers to C++ `using` / `template<...> using`):
    ```rust
    type EntityId = u64;
    type CompareFn = Func<bool(in i32, in i32)>;
    type NameMap<V> = HashMap<Name, V>;
    ```
- **No `namespace`, No `static`, & No Nested/Scoped Types (Strictly Top-Level File Scope Only)**:
  - There is **no `namespace` keyword** in `adhd` (the `adhd` compiler emits the target C++ namespace wrapper automatically during codegen).
  - There is **no `static` keyword** in `adhd`. Stateless helpers and factory functions are written as **top-level free functions** outside any `class` or `interface`.
  - Declaring a `class`, `interface`, or `enum` **inside** another `class` or `interface`, or **inside any local scope** (such as a function, method, `parallel` block, or `{ ... }` block), is a compile-time error.
  - All `class`, `interface`, `enum`, and free function declarations live strictly at **top-level file scope**.
- **No Raw Pointers & No `null` (`Optional<T>` / `Expected<T, E>` Only)**:
  - Raw pointers (`*`), address-of (`&`), arrow (`->`), `new`, `delete`, and `null` / `nullptr` **do not exist** in `adhd`.
  - Every `class` and `interface` variable or parameter (`in` / `mut`) is **guaranteed non-null** (backed by value semantics or `NotNull` in generated C++). Uninitialized `interface` variables are a compile-time error.
  - All member and method access uses `.` syntax (`obj.field`, `obj.method()`, `this.field`).
  - When a value can be absent or an operation can fail, code must explicitly use:
    - **`Optional<T>`** for optional values/lookups (`opt.hasValue()`, `opt.value()`).
    - **`Expected<T, E>`** (`global/expected.h`) for fallible results.
    - **`AssetRef<T>`** (`core/assets/asset_ref.h`, `.valid()`) for asset references.

---

## 3. Member Access Determiners, Layout Optimization & Constructors
- **Mandatory C#-Style Access Determiners**:
  - Every member declaration (field or method) **must** explicitly specify `public`, `private`, or `protected`:
    ```csharp
    class TransformComponent : IComponent {
        public f32x4 position;
        public f32x4 rotation;
        private u32 dirtyFlags;
        private bool active;
    }
    ```
  - Omitting the determiner is a compile-time error.
- **Automatic Member Reordering (Padding Reduction)**:
  - Because SIMD types require `alignas(16)` or `alignas(32)` and scalar/container fields have alignments of `32`, `16`, `8`, `4`, `2`, or `1` bytes, authored declaration order could introduce large alignment holes.
  - The `adhd` compiler **automatically reorders member variables** when emitting the C++ class definition (sorting by descending alignment and size, and packing tail/gap slots) to minimize struct padding and cache footprint.
  - Generated constructors, initializers, binary serializers, and reflection tables use the compiler's resolved layout transparently so user code can declare members in logical/semantic grouping order without paying a memory penalty.
- **Constructors, Destructors & Single-Source Field Initialization**:
  - `class` supports custom constructors (`public ClassName(in f32 s)`) and destructors (`public ~ClassName()`).
  - The `adhd` compiler automatically injects the class's static arena into all owning member container initializers (`Vector`, `HashMap`, `String`, etc.) in the generated C++ constructor initializer list.
  - Inline field default values (`public f32 speed = 1.0f;`) are supported.
  - **No Double Initialization (Compile-Time Error)**: If a member variable has an inline default initializer (`public f32 speed = 1.0f;`) **and** is also initialized inside a constructor, the `adhd` compiler reports a compile-time error. A member must be initialized either inline or in the constructor, never both.
- **Operator Overloading (Instance Member Methods Only — Never Static/Free)**:
  - Built-in SIMD (`f32x4`, `f64x4`, `f32x4x4`, etc.), container, and string types natively support arithmetic, comparison, and indexing operators.
  - User-defined `class` types may overload safe value/math/comparison/indexing operators strictly as **instance member methods** (`this` is always the left-hand side):
    ```cpp
    class WeightPair {
        public f32 a;
        public f32 b;

        public WeightPair operator+(in WeightPair rhs) const;
        public bool operator==(in WeightPair rhs) const;
        public void operator+=(in WeightPair rhs);
    }
    ```
  - **No Free / Static Operator Overloads**: Defining a free or static two-argument operator such as `bool operator==(in A lhs, in B rhs)` is strictly prohibited.
  - **Allowed Operators**:
    - Arithmetic: `+`, `-`, `*`, `/`, `%`, `+=`, `-=`, `*=`, `/=`, `%=`
    - Bitwise: `&`, `|`, `^`, `~`, `<<`, `>>`, `&=`, `|=`, `^=`, `<<=`, `>>=`
    - Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
    - Indexing: `[]`
  - **Forbidden Operators**:
    - Unary `*` (dereference), `->` (arrow), unary `&` (address-of), `,` (comma), `&&` / `||` (short-circuit logic), `new` / `delete`, and implicit type-conversion operators (`operator bool()`).

---

## 4. Parameter Passing (`in` & `mut`), `const` Rules, & Split Implementations
- **Mandatory Parameter Passing Determiners (`in` & `mut`)**:
  - Every function/method parameter **must** explicitly specify `in` or `mut` (there is no C++-style `const T` parameter qualifier):
    - **`in T`**: Read-only input parameter.
      - Emits `CallTraits<T>::param_type` (`const T` by value for scalars/handles, `NWB_SIMD_CALL` register value for SIMD types, or `const T&` / borrowed `Span<const E>` / `AStringView` for aggregates, containers, and strings).
      - Mutation of an `in` parameter inside the function is a compile error.
    - **`mut T`**: Mutable in/out reference parameter.
      - Emits `T&`.
      - Callers do **not** repeat `in` or `mut` at the call site (`step(dt, t);`); the `adhd` compiler verifies at the call site that any argument passed to a `mut` parameter is a mutable lvalue (not a literal, temporary, `const`, or `in` parameter).
- **Role of `const` in `adhd` (No C++-Style Type `const`)**:
  1. **Compile-Time Constant Variables (`const` -> C++ `constexpr`)**:
     - `const u32 MaxBones = 256;` defines a compile-time constant (`constexpr` in generated C++).
  2. **Member Function Suffix (`void foo() const`)**:
     - Allowed as a suffix on `class` and `interface` methods (`public void foo(in f32 x) const;`) to mark that `this` is read-only, allowing the method to be called on `in` instances and emitting a `const` member function in C++.
  3. **Disallowed Everywhere Else**:
     - C++-style `const` type qualifiers on variables, parameters, or return types are not used.
- **Explicit `this.XXX` Inside Method Scope (Both Member Variables & Member Functions)**:
  - Inside a member function body, accessing any **member variable** (`this.position`) or calling any **member function** (`this.update(dt)`) **must** be explicitly qualified with `this.XXX` (using `.` syntax, which compiles to `this->XXX` in C++).
  - Unqualified identifiers inside a method resolve strictly to parameters, local variables, constants, or top-level free functions—never implicitly to members of `this`.
- **Local Variable Type Deduction (`auto`), Mandatory Initialization, Range-Based `for` Loops & Control Flow**:
  - **Mandatory Initialization (No Uninitialized Variables)**:
    - Every local variable **must** have an explicit initial value at the point of declaration (`u32 count = 0;`, `Vector<i32> items(6);`). Declaring an uninitialized variable (`u32 count;`) is a **compile-time error**.
    - Every `class` member variable **must** either declare an inline default value (`public f32 speed = 1.0f;`) or be initialized in the class's constructor(s); leaving any member uninitialized is a **compile-time error**.
  - `auto` is allowed for local variable type deduction (e.g., `auto speed = 1.0f;` or an explicit type `f32 speed = 1.0f;`).
  - Range-based `for` loops require an explicit `in` or `mut` determiner followed by either `auto` or the explicit element type:
    - `for(in auto item : list)` or `for(in Foo item : list)` (read-only iteration).
    - `for(mut auto item : list)` or `for(mut Foo item : list)` (mutable in-place iteration).
  - **No Ternary Operator (`a ? b : c` Prohibited)**:
    - The conditional ternary operator `a ? b : c` is not allowed in `adhd`; use explicit `if` / `else` statements instead.
  - **Explicit Type Casting (`cast<T>(x)` & `bit_cast<T>(x)` Only — No `dynamic_cast`)**:
    - Explicit value conversions must use `cast<T>(x)` (lowers to `static_cast<T>(x)`) and bitwise reinterpretation must use `bit_cast<T>(x)` (lowers to `BitCast<T>(x)` in `global/bit.h`).
    - Rust-style `x as T`, C-style `(T)x` casts, and runtime `dynamic_cast` / `interface`-to-`class` downcasting are strictly prohibited.
  - **`switch` / `case` Statements (Mandatory `break;` & Any Equality-Comparable Type)**:
    - Every `case` (and `default`) branch **must** terminate with `break;` (or `return`); implicit fallthrough is a compile-time error.
    - `switch` accepts not only integers and `enum`s, but **also strings (`String`, `CompactString`, `StringView`, `Name`) and any custom `class` that supports equality comparison (`operator==`)**.
    - For non-integral types (strings and custom classes), the `adhd` compiler automatically lowers the `switch` statement into an `if` / `else if` / `else` chain in generated C++.
  - **No Exceptions (`try` / `catch` / `throw` Prohibited)**:
    - Exceptions (`try`, `catch`, `throw`) are not allowed in `adhd`. Fallible functions return `Expected<T, E>`, and all generated `adhd` functions emit `noexcept` in C++.
- **Function Pointers / Callables (`Func<...>`) & Lambdas**:
  - **Non-Null Callable Type (`Func<Ret(in A, mut B)>`)**:
    - Replaces raw C++ function pointers (`void(*)(int)`):
      ```cpp
      // Non-null callable taking (in i32, in i32) and returning bool
      Func<bool(in i32, in i32)> compare = lessThan;

      // Optional callback (when a callback may be unset)
      Optional<Func<void(mut TransformComponent)>> onUpdate;
      ```
  - **Lambdas (`lambda[in ..., mut ...](in ..., mut ...)` & `lambda(in ..., mut ...)`)**:
    - Captures and parameters must explicitly specify `in` or `mut` (no implicit `[=]` or `[&]` captures):
      ```cpp
      // With captures: explicitly state 'in' or 'mut' for each captured variable
      auto filter = lambda[in threshold, mut hitCount](in f32 distance) -> bool {
          if(distance < threshold) {
              ++hitCount;
              return true;
          }
          return false;
      };

      // Without captures:
      auto lessThan = lambda(in i32 a, in i32 b) -> bool {
          return a < b;
      };
      ```
  - **Extended `InplaceFunction<InlineStorageSize, Signature = void()>` & Automatic Compiler `Func<...>` Lowering**:
    - `global/inplace_function.h` is extended to `template<usize InlineStorageSize, typename Signature = void()> class InplaceFunction` (copyable when captures are copyable, `const`-invocable via `operator()(Args...) const`, with a `0`-byte specialization `InplaceFunction<0, Ret(Args...)>`).
    - Programmers only write `Func<Ret(in A, mut B)>` in `.adhd`; the compiler automatically chooses the lowering based on capture flow analysis:
      1. **Stateless Path (No Captures Flow In)**: Lowers to `InplaceFunction<0, Ret(Args...)>` (`8` bytes, copyable, zero wrapper overhead, and implicitly convertible to a raw C function pointer `Ret(*)(Args...) noexcept`).
      2. **Stateful Path (Captures Flow In)**: When a `lambda[in ..., mut ...](...)` flows into `Func<...>`, the compiler lowers it to `InplaceFunction<64, Ret(Args...)>`.
    - **Primitive Scalar `in` ABI Rule**: Primitive scalars (`bool`, `i8`..`u64`, `isize`, `usize`, `f32`, `f64`) lower `in` to pass-by-value (`u32`, `f32`) while non-scalars (`f32x4`, `class`, `String`) lower `in` to `const T&`, so `Func<...>` signatures match C/C++ function pointer ABIs directly.
  - **No `Move` Keyword (Rust-Style Borrowing & Automatic Last-Use Move)**:
    - Like Rust, `adhd` has **no `Move(x)` or rvalue reference (`T&&`) syntax**.
    - Function parameters strictly use `in` (read-only borrow) or `mut` (mutable borrow).
    - Whenever a local variable is transferred by value at its last use, the `adhd` compiler automatically emits `Move(...)` in the generated C++.
- **Split Method Declarations & Implementations (C++ Style)**:
  - Methods can be declared inside a `class` without a body:
    ```cpp
    class Foo {
        public void bar(in f32 x);
    }
    ```
  - Out-of-class implementations use C++ `ClassName::methodName` syntax, either later in the **same `.adhd` file** or in a **different `.adhd` file**:
    ```cpp
    void Foo::bar(in f32 x) {
        // ...
    }
    ```
  - **Cross-File Inclusion (C++ Style)**: `.adhd` files reference declarations from other `.adhd` files using explicit C++-style `#include` directives.
  - **Codegen Benefit**: Class declarations emit clean `.h` headers while split implementations emit `.cpp` definitions, keeping header dependencies minimal and supporting fast incremental builds.
  - **Compiler Validation**: The `adhd` compiler verifies that the out-of-class `ClassName::methodName` definition matches the declared signature (including `in`/`mut` determiners and return type) and that no declared method is left unimplemented or multiply defined.

---

## 5. Built-In Types

### 5.1 Scalar Types
| `adhd` Type | C++ Type (`global/type.h`) | Size / Align | Description |
| :--- | :--- | :--- | :--- |
| `bool` | `bool` | 1 B | Boolean |
| `i8` / `u8` | `i8` / `u8` | 1 B | 8-bit signed / unsigned integer |
| `i16` / `u16` | `i16` / `u16` | 2 B | 16-bit signed / unsigned integer |
| `i32` / `u32` | `i32` / `u32` | 4 B | 32-bit signed / unsigned integer |
| `i64` / `u64` | `i64` / `u64` | 8 B | 64-bit signed / unsigned integer |
| `isize` / `usize` | `isize` / `usize` | 8 B (x64) | Pointer-sized signed / unsigned integer |
| `f32` | `f32` | 4 B | 32-bit IEEE-754 float |
| `f64` | `f64` | 8 B | 64-bit IEEE-754 double |
| `void` | `void` | — | Return-only empty type |

### 5.2 SIMD-Like Types (Strictly Aligned Only — `f32x4` Naming)
- **Rule**: SIMD-like types in `adhd` use `<scalar>x<lanes>` naming (e.g., `f32x4`, never `Float4`) and are **always aligned** (`alignas(16)` for 32-bit lane packs, `alignas(32)` for 64-bit lane packs). They are **never** converted to or stored as unaligned `*U` types (`Float2U`, `Float3U`, `Float4U`, `Float33U`, `Float44U`, etc.).
- **Aligned `adhd` SIMD Types & C++ Lowering** (`type_vector_aligned.h`, `type_matrix_storage.h`, `type_register.h`):
  - **16-byte Aligned (`alignas(16)`)**:
    - Vectors: `f32x4` (`Float4` / `SIMDVectorSingle`), `i32x4` (`Int4`), `u32x4` (`UInt4`), `f32x3_i32` (`Float3Int`), `f32x3_u32` (`Float3UInt`).
    - Matrices: `f32x3x4` (`Float34`), `f32x4x4` (`Float44` / `SIMDMatrix`).
  - **32-byte Aligned (`alignas(32)`)**:
    - Vectors: `f64x2` (`Double2`), `f64x3` (`Double3`), `f64x4` (`Double4` / `SIMDVectorDouble`), `i64x4` (`Int64x4`), `u64x4` (`UInt64x4`), `f64x3_i64` (`Double3Int`), `f64x3_u64` (`Double3UInt`).
    - Matrices: `f64x3x3` (`Double33`), `f64x3x4` (`Double34`), `f64x4x4` (`Double44` / `SIMDMatrixDouble`).
- **SIMD Construction & Shader-Style Swizzling**:
  - Direct construction from lanes: e.g., `f32x4(1.0f, 2.0f, 3.0f, 1.0f)`.
  - Single-lane access via `.x`, `.y`, `.z`, `.w` and `.r`, `.g`, `.b`, `.a`.
  - Shader-style multi-lane swizzling (e.g., `v.xyz`, `v.zyx`, `v.xxxx`, `v.xyzw`, `v.rgba`) is supported and lowers directly to `global/simd/vector_swizzle.h`.

### 5.3 Default Containers & Strings
All owning containers are automatically bound to the enclosing class's static arena (for members) or the active function's `ScratchArena` (for local variables):
- **Sequential Containers** (Option 1 container syntax only; C-style `T[N]` / `new T[N]` is prohibited):
  - `Array<T, Capacity>` (compile-time fixed size, inline storage via `global/array.h`: e.g., `Array<i32, 6> a;`)
  - `Vector<T>` (runtime size, arena-allocated: e.g., `Vector<i32> b(6);`)
  - `FixedVector<T, Capacity>`
  - `ParallelVector<T>`
  - `ForwardList<T>`
  - `List<T>`
  - `Deque<T>`
- **Associative / Unordered Containers**:
  - `Set<T>`
  - `HashSet<T>`
  - `ParallelHashSet<T>`
  - `Map<K, V>`
  - `HashMap<K, V>`
  - `ParallelHashMap<K, V>`
- **Queues**:
  - `Queue<T>`
  - `ParallelQueue<T>`
  - `ParallelBlockQueue<T, BlockSize>`
- **Strings & Identifiers**:
  - `Name` (`Core::Common::Name`)
  - `Path` (`Path`)
  - `String` (`BasicString<char, ArenaAllocator<...>>`)
  - `CompactString` (`ACompactString`)
- **Views, Value Wrappers, Callables & References**:
  - `Span<T>`, `StringView` (`AStringView`)
  - `Optional<T>`, `Expected<T, E>`
  - `Func<Ret(in A, mut B)>` (non-null function pointer / callable; use `Optional<Func<...>>` when a callback may be unset)
  - `AssetRef<T>` (`Core::Assets::AssetRef<T>`)
  - `Atomic<T>`

---

## 6. Arena Memory Management Model (Zero Manual Configuration)
1. **Per-Class / Per-Component Static Arena**:
   - Each generated `adhd` class defines/binds a dedicated static `Core::Alloc::GlobalArena` named after the class.
   - All owning container fields and polymorphic `interface` allocations inside the class automatically receive this arena in generated constructors.
2. **Automatic Cache Alignment for Parallel Containers**:
   - `ParallelVector`, `ParallelHashSet`, `ParallelHashMap`, `ParallelQueue`, and `ParallelBlockQueue` automatically use `ContainerDetail::ArenaCacheAlignedAllocator` (`global/containers.h`) by default, while standard containers use `ContainerDetail::ArenaAllocator`.
3. **Function-Local Scratch Arena**:
   - Local containers/strings inside `adhd` functions or `parallel` blocks automatically allocate from a stack-scoped `Core::Alloc::ScratchArena<>` without heap churn.
   - Because arenas scale and align automatically (`alignof(T)` and cache-aligned for `Parallel*`), no user-facing arena customization attribute is required.

---

## 7. Multithreading (`parallel_for` & `parallel`)
- **`parallel_for` (Same Grammar as `for`)**:
  - Supports both range-based (`parallel_for(in auto item : list)` / `parallel_for(mut auto item : list)`) and index-based (`parallel_for(usize i = 0; i < count; ++i)`) loops.
  - Lowers directly to `Core::CpuTaskScheduler::parallelFor(usize begin, usize end, const Func& function)` (`core/task/cpu/scheduler.h`).
- **`parallel` (No Separate `dispatch` Keyword Needed)**:
  - Each `parallel` block is itself a task node submitted to `Core::CpuTaskScope` / `Core::CpuTaskScheduler`:
    ```cpp
    [core(Heavy), priority(Critical)]
    parallel cull {
        frustumCull(camera, visibleMeshes);
    }

    [core(Light), priority(Background)]
    parallel audio {
        updateAudio(dt, audioState);
    }

    // Runs after cull finishes; audio continues concurrently
    [core(Heavy)]
    parallel lod : cull {
        selectLods(camera, visibleMeshes);
    }
    ```
  - **Core & Priority Attributes** (`core/task/cpu/types.h`):
    - `[core(Heavy | Light | Any | MainThread)]` maps to `CpuTaskCost` (`Heavy`, `Light`, `Any`) and `CpuTaskTarget` (`Worker`, `MainThread`).
    - `[priority(Critical | Normal | Background)]` maps to `CpuTaskPriority`.
  - **Dependencies (`parallel B : A`)**:
    - Prerequisite `parallel` blocks must be declared first (`A` before `B : A`), guaranteeing zero dependency cycles by construction and allowing workers to begin executing root tasks immediately.
    - Active `parallel` blocks automatically join before exiting the enclosing function scope (or at an explicit `wait(...)` boundary).
  - **`parallel` Callable Dispatch (`parallel funcWithParam(args...);`)**:
    - Inside a `for` loop or function body, `parallel` can directly dispatch a `lambda` or function call with arguments (without a task name or `: dep` clause):
      ```cpp
      for(usize i = 0; i < count; ++i) {
          u32 offset = cast<u32>(i * 64);

          auto funcWithParam = lambda[mut results](in usize index, in u32 chunkOffset) -> void {
              processChunk(index, chunkOffset, results);
          };

          [core(Heavy), priority(Normal)]
          parallel funcWithParam(i, offset);
      }
      ```
    - **Loop-Safe Argument Evaluation**: `in` arguments are evaluated immediately on the submitting thread and captured **by value** into the `Core::CpuTaskScope::submit(...)` closure (preventing loop-counter data races), while `mut` arguments and `[mut ...]` captures are bound by reference.
- **Compile-Time Concurrency Rules**:
  - Within `parallel_for` or concurrent `parallel` bodies, shared `mut` container access is restricted to thread-safe containers (`ParallelVector`, `ParallelHashSet`, `ParallelHashMap`, `ParallelQueue`, `ParallelBlockQueue`, `Atomic<T>`) or per-worker disjoint elements/slices (`Span<T>`).
  - Read-only `in` captures may be shared freely across worker threads.

---

## 8. Preprocessor & Macro System
- `adhd` supports a C/C++-style preprocessor before AST parsing, layout optimization, and reflection generation (no `#pragma` needed in `.adhd`; the `adhd` compiler deduplicates `#include` automatically and emits `#pragma once` in generated C++ headers):
  - **File Inclusion (`.adhd` vs C++)**:
    - Outside `native {}`, `#include "..."` / `#include <...>` includes **`.adhd` files only**.
    - C++-side `#include` directives must be placed inside a `native { ... }` block.
  - **Macro Definitions**:
    - `#define NAME value` (object-like macros)
    - `#define NAME(a, b) ...` (function-like macros)
    - `#undef NAME`
  - **Conditional Compilation**:
    - `#if <expr>`, `#ifdef NAME`, `#ifndef NAME`, `#elif <expr>`, `#else`, `#endif`
    - Supports `defined(NAME)`, integer/boolean constant expressions (`&&`, `||`, `!`, `==`, `!=`, `<`, `>`, bitwise ops), and engine build/platform macros (e.g., `NWB_DEBUG`, `NWB_OPTIMIZE`, `NWB_FINAL`).
  - **Interaction with Member Reordering & Reflection**:
    - Running the preprocessor stage inside the `adhd` compiler resolves active `#if` branches and expands `#define` macros before AST construction, allowing the `adhd` compiler to reorder the active class members and generate exact reflection tables without layout mismatches.

---

## 9. Attribute System (`[...]`) & `.nwb` Reflection Binding
- **Single `[...]` Bracket with Comma-Separated Attributes**:
  - Attributes `[...]` can be placed above any **`class`**, **`interface`**, **member variable**, **function/method**, or **`parallel`** block.
  - When a declaration has multiple attributes, they **must** be comma-separated inside a single `[...]` bracket (stacked `[...]` `[...]` blocks on the same declaration are prohibited):
    ```csharp
    [asset("model_component"), serialize]
    class ModelComponent : IComponent {
        public AssetRef<Model> asset;

        [transient]
        private u32 runtimeHandle;

        [inline]
        public void validate(in bool strict);
    }
    ```
- **Automatic `.nwb` & `[serialize]` Codegen**:
  - When a `class` is marked with `[asset("type_name")]` and/or `[serialize]`, the `adhd` compiler automatically generates:
    1. `.nwb` property parsing for all `public` (non-`[transient]`) fields,
    2. Automatic `AssetRef<T>` dependency extraction for `dependency_computer`, and
    3. `1`-byte-packed binary serialization and deserialization.
- **Categories of Attributes**:
  1. **Serialization & Layout Attributes**:
     - **Default (No `[serialize]`)**: Follows natural/SIMD alignment (`16`-byte / `32`-byte for SIMD types, natural alignment for scalars) with automatic compiler member reordering to minimize padding.
     - **`[serialize]`**: Always packs in `1` byte (`1`-byte packed binary representation / serialization layout with zero padding holes), and connects the reflected fields to binary serialization / `.nwb` cooking.
  2. **Reflection & `.nwb` Asset Connection Attributes** (`class` / `interface` / variable / function):
     - `[reflect]` / `[asset("type_name")]`: Exposes the `class` or `interface` to the reflection registry and connects it directly to `.nwb` authoring, dependency scanning, and cooking.
     - `[transient]`: Marks a member variable as runtime-only (excluded from `[serialize]` and `.nwb` authoring).
  3. **Function & Scheduling Attributes**:
     - `[inline]` / `[noinline]`
     - `[core(...)]` / `[priority(...)]` (for `parallel` and `parallel_for`)

---

## 10. C++ Interop (`native` & `native(in ..., mut ...)` Blocks)
- C++-side `#include` directives, calling C++ functions, or embedding native C++ statements inside `.adhd` **must** always be enclosed in an explicit `native` block.
- **Passing `.adhd` Variables / Members into `native` (`native(in ..., mut ...)` Capture List)**:
  - When a `native` block inside a function accesses `.adhd` variables or `this.XXX` members, they are explicitly bound in the `native(...)` header using `in` or `mut`:
    ```cpp
    native {
        #include <global/simdmath.h>
    }

    void TransformComponent::applyTranslation(in f32x4 delta) {
        native(in delta, mut pos = this.position) {
            const SIMDVectorSingle current = LoadFloat(pos.raw);
            const SIMDVectorSingle offset  = LoadFloat(delta.raw);
            StoreFloat(pos.raw, VectorAdd(current, offset));
        }
    }
    ```
  - The `adhd` compiler binds `in` captures as `const` C++ references/values and `mut` captures as mutable C++ references inside the generated C++ block.
- **Passing Containers / Values as `void*` or `const void*` Inside `native`**:
  - Because the body of `native(...) { ... }` is raw C++, any contiguous container (`Array<T, N>`, `Vector<T>`, `FixedVector<T, N>`, `Span<T>`, `String`, `CompactString`) captured via `in` or `mut` exposes `.data()` and `.size()`:
    - `in` capture: `buf.data()` yields `const T*` (implicitly convertible to `const void*`).
    - `mut` capture: `buf.data()` yields `T*` (implicitly convertible to `void*`).
    ```cpp
    Array<i32, 6> a;
    Vector<i32> b(6);
    native(in a, mut b) {
        // b.data() -> i32* (void*), a.data() -> const i32* (const void*)
        NWB_MEMCPY(b.data(), b.size() * sizeof(i32), a.data(), a.size() * sizeof(i32));
    }
    ```
- **Passing Callbacks (`Func<...>`) into `native`**:
  - **Stateless `Func<...>` (`InplaceFunction<0, Ret(Args...)>`)**: Implicitly converts to a raw C function pointer `Ret(*)(Args...) noexcept`, so `cb` can be passed directly to both C function pointer APIs (`c_api(cb)`) and C++ callable APIs (`cpp_api(cb)`).
  - **Stateful `Func<...>` (`InplaceFunction<64, Ret(Args...)>`)**:
    - Passes directly to C++ callable APIs (`cpp_api(cb)`) without needing `Move(cb)`.
    - For C APIs that expect `(callback, void* userData)`, `InplaceFunction` provides `.cTrampolineLast()` (`Ret(*)(Args..., void*) noexcept`), `.cTrampolineFirst()` (`Ret(*)(void*, Args...) noexcept`), and `.userData()` (`void*`):
      ```cpp
      native(in cb) {
          c_api_with_userdata(cb.cTrampolineLast(), cb.userData());
      }
      ```
- **Benefits of `native(in ..., mut ...) { ... }`**:
  - Clearly separates `.adhd` `#include`s (parsed by the `adhd` compiler) from C++ `#include`s (emitted into the generated C++ file).
  - Clearly isolates raw C++ engine calls from pure `adhd` code (similar to `unsafe` blocks in Rust/C#).
  - Because `native(in ..., mut ...)` declares which `.adhd` variables are read (`in`) vs. mutated (`mut`), the `adhd` compiler can still enforce `in`/`mut` parameter safety, `const` method rules, and `parallel` data-race checks without needing a full C++ parser inside `{ ... }`.

---

## 11. Templates & Generics (`<T>`, `<T : IInterface>`, `<T, const usize N>`)
- `adhd` uses Rust-style inline `<...>` type parameters (no `template<typename ...>` keyword prefix):
  1. **Inline `<T>` Syntax on Classes, Interfaces & Functions**:
     ```cpp
     class Pair<T, U> {
         public T first;
         public U second;
         public void reset(in T defaultFirst, in U defaultSecond);
     }

     void Pair<T, U>::reset(in T defaultFirst, in U defaultSecond) {
         this.first = defaultFirst;
         this.second = defaultSecond;
     }

     void swapValues<T>(mut T a, mut T b) {
         auto temp = a;
         a = b;
         b = temp;
     }
     ```
  2. **Interface Bounds (`<T : IInterface>` & `<T : IFirst + ISecond>`)**:
     - If a generic class or function invokes methods on `T`, `T` must be constrained by one or more `interface`s (separated by `+`):
     ```cpp
     void attach<T : IComponent>(mut Entity entity, in T component) {
         component.onAttach(entity);
     }

     class AssetPool<T : IComponent + IValidator> {
         private Vector<T> items;
     }
     ```
  3. **Compile-Time Constant Parameters (`<T, const usize N>`)**:
     - Non-type template parameters use `const` (matching `adhd`'s compile-time constant keyword):
     ```cpp
     class StaticBatch<T, const usize Capacity> {
         private FixedVector<T, Capacity> elements;
     }
     ```
  - **Per-Instantiation Member Reordering & Reflection**:
    - For each concrete instantiation (e.g., `Pair<u8, f32x4>`), the `adhd` compiler resolves the concrete sizes and alignments of the template arguments, reorders the instantiated class's members to minimize padding, and generates reflection tables for that concrete instantiation.

---

## 12. Compiler Architecture & Domain-Split Module Layout (`core/metascript` & `pipeline/adhd_compiler`)
Every source and header file is strictly split by domain responsibility so that no single file becomes monolithic:

1. **Shared Foundation & `.nwb` Evaluation (`core/metascript/`)**:
   - `lexer.h` / `lexer.cpp`: Unified lexical scanner and token definitions.
   - `parser_cursor.h` / `parser_cursor.cpp`: Token stream navigation and `ParseError` diagnostics.
   - `preprocessor.h` / `preprocessor.cpp`: Evaluates `#include` (with automatic deduplication), `#define`, `#undef`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, and `#endif` for both `.nwb` and `.adhd`.
   - `attribute_parser.h` / `attribute_parser.cpp`: Parses single-bracket comma-separated attribute lists (`[asset("..."), serialize]`).
   - `value.*`, `operation_validator.*`, `expression_parser.*`, `statement_parser.*`, `parser.*`: `.nwb` document data evaluation.

2. **`adhd` Language & Compiler Library (`core/metascript/adhd/`)**:
   - **AST (`core/metascript/adhd/ast/`)**:
     - `ast_common.h`: Source spans, attributes, access specifiers, and `in` / `mut` / `const` modifiers.
     - `ast_types.h`: Scalar, SIMD (`f32x4`, `f32x4x4`), `Func<...>`, `enum<SizeType>`, and template bound (`<T : IBar, const usize N>`) nodes.
     - `ast_expr.h`: Expressions (`this.XXX`, calls, operators, `cast<T>`, `bit_cast<T>`, `lambda`, vector swizzles).
     - `ast_stmt.h`: Statements (`if`/`else`, `switch`/`case`, `for`, `parallel_for`, `parallel`, `native`, variable declarations).
     - `ast_decl.h`: Top-level declarations (`class`, `interface`, `enum`, `type`, free functions, out-of-line `Class::method`).
   - **Parser (`core/metascript/adhd/parser/`)**:
     - `type_parser.h` / `type_parser.cpp`: Parses type references, template parameter lists, and `Func<...>` signatures.
     - `expr_parser.h` / `expr_parser.cpp`: Parses expressions, `lambda`, `cast<T>` / `bit_cast<T>`, and SIMD swizzles.
     - `stmt_parser.h` / `stmt_parser.cpp`: Parses control flow, `parallel_for`, `parallel` task blocks, and `native` blocks.
     - `decl_parser.h` / `decl_parser.cpp`: Parses `class`, `interface`, `enum<SizeType>`, `type`, functions, and translation units.
   - **Semantic Analysis & Layout (`core/metascript/adhd/sema/`)**:
     - `symbol_table.h` / `symbol_table.cpp`: Top-level symbol registry, interface conformance, and template instantiation table.
     - `type_checker.h` / `type_checker.cpp`: Enforces no-raw-pointer rules, `in` / `mut` parameters, mandatory `this.XXX`, `enum<SizeType>` arithmetic, swizzle lanes, and `switch`/`case` `break` rules.
     - `layout_optimizer.h` / `layout_optimizer.cpp`: Computes field alignment/size and reorders `class` members (including per-instantiation templates) to minimize padding.
     - `parallel_graph.h` / `parallel_graph.cpp`: Validates `parallel name : dep` task DAGs, cycle detection, and `[core(...), priority(...)]` attributes.
   - **Code & Reflection Generation (`core/metascript/adhd/codegen/`)**:
     - `header_emitter.h` / `header_emitter.cpp`: Emits `.adhd.h` (`class ... final`, `namespace EnumName`, `NWB_DEFINE_ARENA_ALLOCATOR`, inline templates).
     - `source_emitter.h` / `source_emitter.cpp`: Emits `.adhd.cpp` method bodies, `ScratchArena<>` injection, `CpuTaskScheduler` lowering for `parallel_for` / `parallel`, and `native` blocks.
     - `reflection_emitter.h` / `reflection_emitter.cpp`: Emits `[serialize]` 1-byte packed binary serialization, `.nwb` property parsing, `AssetRef<T>` dependency extraction, and `[asset("...")]` registration.

3. **CLI Executable (`pipeline/adhd_compiler/`)**:
   - `compiler_options.h` / `compiler_options.cpp`: CLI argument parsing (input `.adhd`, include directories, output `.adhd.h` / `.adhd.cpp`, `--namespace`, `--validate-only`).
   - `compiler_driver.h` / `compiler_driver.cpp`: Orchestrates file I/O and invokes the `core/metascript/adhd` pipeline.
   - `main.cpp`: Standalone executable entry point invoked by CMake and build/validation tools.

4. **Build & C++ Integration Rules**:
   - **CLI / CMake Namespace (`--namespace`)**: Since `.adhd` has no `namespace` keyword, the generated C++ declarations are wrapped in the C++ namespace passed via `--namespace <ns>` (defaulting to `NWB`).
   - **Cross-File `#include "other.adhd"`**: The compiler parses included `.adhd` files (with automatic deduplication) to resolve `interface` methods, `enum<SizeType>` sizes, and exact `sizeof` / `alignof` of embedded `class` types during member layout optimization, and emits `#include "other.adhd.h"` in the generated header.
   - **No Positional Aggregate Init (`Foo{a, b}`)**: Because `layout_optimizer` reorders class fields automatically, classes are constructed via constructors (`Foo(a, b)`) or default construction + named member assignment (`Foo f; f.x = ...;`).
   - **CMake Helper (`nwb_target_adhd_sources`)**: Runs `nwb_adhd_compiler` per `.adhd` source, emits `.adhd.h` / `.adhd.cpp` into the target's generated build directory, and attaches the generated C++ sources and include paths to the target.
