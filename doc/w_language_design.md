# `.w` ("Wrapper") Language Design Note

> [!NOTE]
> Living specification for the `.w` language. `.w` files compile into C++ source code with generated reflection and automatic arena-based memory management.

---

## 1. Overview & Goals
- **File Extension**: `.w` (stands for **wrapper**).
- **Compilation Pipeline**: `.w` source -> `.w` compiler -> generated `.h` / `.cpp` C++ source + reflection tables.
- **Core Guarantees**:
  1. **Zero manual allocator boilerplate**: Every class/component owns a static arena for persistent member allocations; functions use a local `ScratchArena` for temporary allocations.
  2. **First-class Reflection**: Every `class`, `interface`, member field, and method is reflected for `.nwb` asset cooking, binary serialization, and ECS inspection.
  3. **SIMD Alignment & Optimal Memory Layout**: All SIMD types are strictly aligned (never unaligned `*U`), and the `.w` compiler automatically reorders class members to minimize padding.
  4. **Explicit Mutability & Access**: Mandatory member access determiners (`public`, `private`, `protected`) and parameter passing determiners (`in`, `mut`).
  5. **Built-in Task Multithreading**: First-class `task` functions integrated with `Core::CpuTaskScheduler`.

---

## 2. Type Declarations (`class`, `interface`, `enum`) & Inheritance Model
- **`class` (Concrete Data + Methods, Always `final`)**:
  - Unifies C++ `class` and `struct` into a single `class` keyword.
  - **No `class`-to-`class` inheritance (Rust-style)**: A `class` **cannot** inherit from another `class` (composition over inheritance). Every generated C++ `class` is `final`, allowing direct non-virtual calls and unobstructed compiler member reordering.
  - A `class` may implement one or more `interface`s (`class ModelComponent : IComponent`).
  - No `virtual` or `override` keywords are needed; implementing an `interface` is satisfied by providing methods with matching signatures.
- **`interface` (Pure Polymorphic Contract)**:
  - Pure interface type (maps to NWBLot's lowercase `interface` convention in `global/compile.h`).
  - An `interface` contains **method declarations only** (no member variables).
  - Variables or containers typed by an `interface` (e.g., `Vector<IComponent>`) can hold any implementing `class` instance polymorphically, allocated from the owning class/component arena.
- **`enum`**:
  - Enumerated type declaration (can specify underlying integer type, e.g., `enum Foo : u8`, and automatically gets reflected string/value mapping via `NamedEnumCase`).
- **No `namespace`, No `static`, & No Nested/Scoped Types (Strictly Top-Level File Scope Only)**:
  - There is **no `namespace` keyword** in `.w` (the `.w` compiler emits the target C++ namespace wrapper automatically during codegen).
  - There is **no `static` keyword** in `.w`. Stateless helpers and factory functions are written as **top-level free functions** outside any `class` or `interface`.
  - Declaring a `class`, `interface`, or `enum` **inside** another `class` or `interface`, or **inside any local scope** (such as a function, method, `task`, or `{ ... }` block), is a compile-time error.
  - All `class`, `interface`, `enum`, and free function declarations live strictly at **top-level file scope**.
- **No Raw Pointers & No `null` (`Optional<T>` / `Expected<T, E>` Only)**:
  - Raw pointers (`*`), address-of (`&`), arrow (`->`), `new`, `delete`, and `null` / `nullptr` **do not exist** in `.w`.
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
        public Float4 position;
        public Float4 rotation;
        private u32 dirtyFlags;
        private bool active;
    }
    ```
  - Omitting the determiner is a compile-time error.
- **Automatic Member Reordering (Padding Reduction)**:
  - Because SIMD types require `alignas(16)` or `alignas(32)` and scalar/container fields have alignments of `32`, `16`, `8`, `4`, `2`, or `1` bytes, authored declaration order could introduce large alignment holes.
  - The `.w` compiler **automatically reorders member variables** when emitting the C++ class definition (sorting by descending alignment and size, and packing tail/gap slots) to minimize struct padding and cache footprint.
  - Generated constructors, initializers, binary serializers, and reflection tables use the compiler's resolved layout transparently so user code can declare members in logical/semantic grouping order without paying a memory penalty.
- **Constructors, Destructors & Single-Source Field Initialization**:
  - `class` supports custom constructors (`public ClassName(in f32 s)`) and destructors (`public ~ClassName()`).
  - The `.w` compiler automatically injects the class's static arena into all owning member container initializers (`Vector`, `HashMap`, `String`, etc.) in the generated C++ constructor initializer list.
  - Inline field default values (`public f32 speed = 1.0f;`) are supported.
  - **No Double Initialization (Compile-Time Error)**: If a member variable has an inline default initializer (`public f32 speed = 1.0f;`) **and** is also initialized inside a constructor, the `.w` compiler reports a compile-time error. A member must be initialized either inline or in the constructor, never both.

---

## 4. Parameter Passing (`in` & `mut`), `const` Rules, & Split Implementations
- **Mandatory Parameter Passing Determiners (`in` & `mut`)**:
  - Every function/method parameter **must** explicitly specify `in` or `mut` (there is no C++-style `const T` parameter qualifier):
    - **`in T`**: Read-only input parameter.
      - Emits `CallTraits<T>::param_type` (`const T` by value for scalars/handles, `NWB_SIMD_CALL` register value for SIMD types, or `const T&` / borrowed `Span<const E>` / `AStringView` for aggregates, containers, and strings).
      - Mutation of an `in` parameter inside the function is a compile error.
    - **`mut T`**: Mutable in/out reference parameter.
      - Emits `T&`.
      - Callers do **not** repeat `in` or `mut` at the call site (`step(dt, t);`); the `.w` compiler verifies at the call site that any argument passed to a `mut` parameter is a mutable lvalue (not a literal, temporary, `const`, or `in` parameter).
- **Role of `const` in `.w` (No C++-Style Type `const`)**:
  1. **Compile-Time Constant Variables (`const` -> C++ `constexpr`)**:
     - `const u32 MaxBones = 256;` defines a compile-time constant (`constexpr` in generated C++).
  2. **Member Function Suffix (`void foo() const`)**:
     - Allowed as a suffix on `class` and `interface` methods (`public void foo(in f32 x) const;`) to mark that `this` is read-only, allowing the method to be called on `in` instances and emitting a `const` member function in C++.
  3. **Disallowed Everywhere Else**:
     - C++-style `const` type qualifiers on variables, parameters, or return types are not used.
- **Explicit `this.XXX` Inside Method Scope (Both Member Variables & Member Functions)**:
  - Inside a member function body, accessing any **member variable** (`this.position`) or calling any **member function** (`this.update(dt)`) **must** be explicitly qualified with `this.XXX` (using `.` syntax, which compiles to `this->XXX` in C++).
  - Unqualified identifiers inside a method resolve strictly to parameters, local variables, constants, or top-level free functions—never implicitly to members of `this`.
- **Local Variable Type Deduction (`auto`) & Range-Based `for` Loops**:
  - `auto` is allowed for local variable type deduction (e.g., `auto speed = 1.0f;` or an explicit type `f32 speed = 1.0f;`).
  - Range-based `for` loops require an explicit `in` or `mut` determiner followed by either `auto` or the explicit element type:
    - `for(in auto item : list)` or `for(in Foo item : list)` (read-only iteration).
    - `for(mut auto item : list)` or `for(mut Foo item : list)` (mutable in-place iteration).
- **Split Method Declarations & Implementations (C++ Style)**:
  - Methods can be declared inside a `class` without a body:
    ```cpp
    class Foo {
        public void bar(in f32 x);
    }
    ```
  - Out-of-class implementations use C++ `ClassName::methodName` syntax, either later in the **same `.w` file** or in a **different `.w` file**:
    ```cpp
    void Foo::bar(in f32 x) {
        // ...
    }
    ```
  - **Cross-File Inclusion (C++ Style)**: `.w` files reference declarations from other `.w` files using explicit C++-style `#include` directives.
  - **Codegen Benefit**: Class declarations emit clean `.h` headers while split implementations emit `.cpp` definitions, keeping header dependencies minimal and supporting fast incremental builds.
  - **Compiler Validation**: The `.w` compiler verifies that the out-of-class `ClassName::methodName` definition matches the declared signature (including `in`/`mut` determiners and return type) and that no declared method is left unimplemented or multiply defined.

---

## 5. Built-In Types

### 5.1 Scalar Types
| `.w` Type | C++ Type (`global/type.h`) | Size / Align | Description |
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

### 5.2 SIMD-Like Types (Strictly Aligned Only)
- **Rule**: SIMD-like types in `.w` are **always aligned** (`alignas(16)` for 32-bit lane packs, `alignas(32)` for 64-bit lane packs). They are **never** converted to or stored as unaligned `*U` types (`Float2U`, `Float3U`, `Float4U`, `Float33U`, `Float44U`, etc.).
- **Underlying Aligned Types** (`type_vector_aligned.h`, `type_matrix_storage.h`, `type_register.h`):
  - **16-byte Aligned (`alignas(16)`)**:
    - Vectors: `Float4`, `Int4`, `UInt4`, `Float3Int`, `Float3UInt` (and 2-lane / 3-lane `f32` vectors use 16-byte aligned storage/registers `SIMDVectorSingle`).
    - Matrices: `Float34`, `Float44` / `SIMDMatrix`.
  - **32-byte Aligned (`alignas(32)`)**:
    - Vectors: `Double2`, `Double3`, `Double4`, `Int64x4`, `UInt64x4`, `Double3Int`, `Double3UInt` / `SIMDVectorDouble`.
    - Matrices: `Double33`, `Double34`, `Double44` / `SIMDMatrixDouble`.

### 5.3 Default Containers & Strings
All owning containers are automatically bound to the enclosing class's static arena (for members) or the active function's `ScratchArena` (for local variables):
- **Sequential Containers**:
  - `Vector<T>`
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
- **Views, Value Wrappers & References**:
  - `Span<T>`, `StringView` (`AStringView`)
  - `Optional<T>`, `Expected<T, E>`
  - `AssetRef<T>` (`Core::Assets::AssetRef<T>`)
  - `Atomic<T>`

---

## 6. Arena Memory Management Model
1. **Per-Class / Per-Component Static Arena**:
   - Each generated `.w` class defines/binds a dedicated static arena.
   - All owning container fields and polymorphic `interface` allocations inside the class automatically receive this arena in generated constructors.
2. **Function-Local Scratch Arena**:
   - Local containers/strings inside `.w` functions or `task` bodies automatically allocate from a stack-scoped `Core::Alloc::ScratchArena<>` without heap churn.

---

## 7. Multithreading (`task`)
- **`task` Functions**:
  - Language-level `task` declarations compile into jobs submitted to `Core::CpuTaskScheduler` / `Core::CpuTaskScope` (`schedule`, `parallelFor`).
- **Compile-Time Concurrency Rules**:
  - Within parallel `task` bodies, shared `mut` container access is restricted to thread-safe containers (`ParallelVector`, `ParallelHashSet`, `ParallelHashMap`, `ParallelQueue`, `ParallelBlockQueue`, `Atomic<T>`) or per-worker disjoint slices (`Span<T>`).
  - Read-only `in` captures may be shared freely across worker threads.

---

## 8. Preprocessor & Macro System
- `.w` supports a C/C++-style preprocessor before AST parsing, layout optimization, and reflection generation (no `#pragma` needed in `.w`; the `.w` compiler deduplicates `#include` automatically and emits `#pragma once` in generated C++ headers):
  - **File Inclusion**:
    - `#include "..."` / `#include <...>`
  - **Macro Definitions**:
    - `#define NAME value` (object-like macros)
    - `#define NAME(a, b) ...` (function-like macros)
    - `#undef NAME`
  - **Conditional Compilation**:
    - `#if <expr>`, `#ifdef NAME`, `#ifndef NAME`, `#elif <expr>`, `#else`, `#endif`
    - Supports `defined(NAME)`, integer/boolean constant expressions (`&&`, `||`, `!`, `==`, `!=`, `<`, `>`, bitwise ops), and engine build/platform macros (e.g., `NWB_DEBUG`, `NWB_OPTIMIZE`, `NWB_FINAL`).
  - **Interaction with Member Reordering & Reflection**:
    - Running the preprocessor stage inside the `.w` compiler resolves active `#if` branches and expands `#define` macros before AST construction, allowing the `.w` compiler to reorder the active class members and generate exact reflection tables without layout mismatches.

---

## 9. Attribute System (`[...]`) & `.nwb` Reflection Binding
- C#-style attributes `[...]` can be placed above any **`class`**, **`interface`**, **member variable**, or **function/method**:
  ```csharp
  [asset("model_component")]
  [serialize]
  class ModelComponent : IComponent {
      public AssetRef<Model> asset;

      [transient]
      private u32 runtimeHandle;

      [inline]
      public void validate(in bool strict);
  }
  ```
- **Categories of Attributes**:
  1. **Serialization & Layout Attributes**:
     - **Default (No `[serialize]`)**: Follows natural/SIMD alignment (`16`-byte / `32`-byte for SIMD types, natural alignment for scalars) with automatic compiler member reordering to minimize padding.
     - **`[serialize]`**: Always packs in `1` byte (`1`-byte packed binary representation / serialization layout with zero padding holes), and connects the reflected fields to binary serialization / `.nwb` cooking.
  2. **Reflection & `.nwb` Asset Connection Attributes** (`class` / `interface` / variable / function):
     - `[reflect]` / `[asset("type_name")]`: Exposes the `class` or `interface` to the reflection registry and connects it directly to `.nwb` authoring, dependency scanning, and cooking.
     - `[transient]`: Marks a member variable as runtime-only (excluded from `[serialize]` and `.nwb` authoring).
  3. **Function / Task Attributes** (function / method):
     - `[inline]` / `[noinline]`, `[task]`, or ECS stage/scheduling hooks.
