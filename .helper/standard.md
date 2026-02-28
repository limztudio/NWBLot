# NWBLot Inferred Code Standard

Derived from `core/`, `global/`, and `logger/` source files (excluding `3rd_parties/`).  
Updated: 2026-02-28

## 1. File and module structure
- Use lowercase `snake_case` filenames for C++ source and headers.
- Start files with the project banner style:
  - `// limztudio@gmail.com`
  - A long `////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////` separator line.
- Use `#pragma once` in headers.
- Separate major file sections with the long slash separator and optional section comments.
- Source files must end with `////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////` followed by exactly two blank lines.
- Exact EOF rule for source files: after the final separator line, keep exactly two newline terminators (`\n\n`, or `\r\n\r\n` on Windows). Do not keep one or three.
- Use UTF-8 encoding for source files.
- Use Windows-style newlines (`CRLF`) for all source files.
- Do not commit LF-only or mixed line endings in source files.

## 2. Namespace style
- Use namespace wrapper macros (`NWB_BEGIN`, `NWB_CORE_BEGIN`, `NWB_VULKAN_BEGIN`, etc.) instead of raw namespace blocks in module public files.
- End wrapped namespaces with corresponding `*_END` macros.
- Put private/internal helpers in `namespace __hidden_<module>{ ... }`.

## 3. Naming conventions
- Types (`class`, `struct`, `enum namespaces`) use `PascalCase`.
- Functions, methods, parameters, and local variables use `lowerCamelCase`.
- Member fields use `m_` prefix.
- Non-static global variables use `g_` prefix.
- Constants use `s_` prefix and are usually `constexpr`.
- Enum pattern is typically:
  - `namespace SomeEnum { enum Enum : u8 { ... }; };`
- For engine public/module-facing enums, use the namespace-enum pattern (`namespace X { enum Enum : u8 { ... }; };`) and do not use `enum class`.
- Handle aliases follow `<Type>Handle` naming.
- Global functions start with `Uppercase`.
- Global static variables start with `s_Uppercase`.
- For virtual overrides, explicitly write both `virtual` and `override`.
- If a class or virtual function can reasonably be `final`, use `final` to help devirtualization/unrolling opportunities.

## 4. Formatting conventions
- Indentation is 4 spaces (no tabs observed in sampled files).
- Braces are generally K&R style (`if(...) {`, `switch(...) {`, `class X{`).
- Keep control statements compact; single-line guard clauses are common.
- Constructor initializer lists are split across lines with leading commas.
- Heavy use of visual separators and blank lines between logical blocks.
- Prefer two blank lines between adjacent function definitions in the same class/struct implementation when they are part of the same logical area.
- Use `////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////` between function definitions only when crossing a strong boundary:
  - changing to a clearly different function category/personality, or
  - moving between definitions of different classes/structs in the same `.cpp`.
- Prefer pre-increment/decrement (`++p`, `--p`) over post-increment/decrement (`p++`, `p--`) when behavior is equivalent.
- For single-statement `if/for/while`, put the statement on the next line:
  - `if(condition)`
  - `    execute();`
- Prefer single-line function calls, e.g. `foobar(a, b, c, d);`.
- Split function arguments only when the line would exceed the long separator width (`////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////`) or when argument count is too high.
- When splitting calls, place the closing `)` on a new line:
  - `foobar(`
  - `    a,`
  - `    b,`
  - `);`
- Do not use mixed styles like:
  - `foobar(a,`
  - `    b);`
  - or `foobar(`
  - `    a, b);`

## 5. Include order
- First include the matching local header (`#include "file.h"`).
- Then include project/external headers.
- Keep includes grouped with a blank line between groups.
- Exception for precompiled-header translation units: if the project requires PCH (`/Yu`), include `pch.h` first, then include the matching local header immediately after.

## 6. Type and API usage
- Prefer project scalar aliases (`u32`, `u64`, `usize`, `f32`, etc.) from `global/type.h`.
- Prefer explicit casts (`static_cast`) over C-style casts.
- Use `checked_cast` for pointer downcasts in internal engine code.
- `[[nodiscard]]` is used on important query/accessor return values.
- Const correctness is expected, though both `const T&` and `T const&` forms appear.
- `auto` is allowed, but always spell out qualifiers and reference/pointer intent (`const`, `&`, `*`, `&&`) explicitly when applicable.
- Example style: `for(const auto& i : table){ ... }`.
- Prefer project wrapper/alias types over raw `std::` names (e.g., `Vector` instead of `std::vector`, traits aliases like `IsSame`).
- Before introducing a new direct `std::` usage, check `global/global.h` and related global headers for an existing wrapper/alias.
- If missing and broadly useful, add a project-level alias/wrapper rather than repeating direct `std::` usage across modules.
- For generic core std types used across modules (e.g., `std::max_align_t`), add/use a `global/type.h` alias (`MaxAlign`) instead of direct `std::` usage in module code.
- Prefer project C-runtime wrapper macros from `global/compile.h` for memory/string operations (`NWB_MEMCPY`, `NWB_MEMSET`, `NWB_MEMCMP`, `NWB_STRCPY`, etc.) instead of direct `std::`/CRT calls when equivalent wrappers exist.
- When exposing inherited member functions without changing behavior, prefer `using BaseType::functionName;` over trivial forwarding wrappers like `inline foo(...){ return BaseType::foo(...); }`.
- Keep forwarding wrappers only when they add behavior, transform contracts, or intentionally change the exposed API shape.
- Do not add trivial pass-through accessors that only return a private member (`return m_x;`) when they are not needed as a module boundary API.
- If such access is only needed inside the same module bubble, remove the accessor and use direct member access; use `friend` declarations explicitly where cross-class private access is required.
- Keep trivial accessors only when they are actually used across module boundaries or are part of a deliberate external API contract.
- Apply the same rule to trivial pass-through setters (`setX(...) { m_x = ...; }`): remove them when unused or bubble-only.
- Keep trivial setters only when they are part of a deliberate external API contract and actually needed at module boundaries.
- Strictly distinguish references vs pointers:
  - Parameters:
    - Use references (`T&`, `const T&`) for required, non-null inputs by default.
    - Use `NotNull<T*>` only when pointer semantics are required (e.g., nullable interop boundaries, pointer identity APIs, or reseating semantics) but null is still invalid.
    - For required C-string style inputs (`const char*`, `const tchar*`), prefer `NotNull<const char*>` / `NotNull<const tchar*>` instead of raw pointers.
    - Use raw pointers (`T*`) only when null is a valid and meaningful state.
  - Returns:
    - Return `T&`/`const T&` for always-present objects with externally guaranteed lifetime.
    - Return `NotNull<T*>` only when pointer semantics are intentionally part of the return contract and null is invalid.
    - Return `T*` for optional/maybe-found results.
    - Never return references or pointers to local stack objects.
  - Members:
    - Prefer reference members for mandatory dependencies that never change after construction.
    - For mandatory services/dependencies (e.g., allocators, schedulers, pools, job systems), require constructor-injected references and avoid nullable pointer members/default-null constructor parameters.
    - Use `NotNull<T*>` members when non-null is required but reference members are impractical (e.g., reseating/assignment requirements).
    - Use raw pointer members only for truly optional relationships.
  - For `NotNull<T*>`, do not add redundant null checks before dereference; treat it as non-null by contract.
  - Avoid nullable-pointer APIs followed by immediate non-null assertions; model required inputs as references instead.
- For scoped mutex locking, prefer `ScopedLock lock(m_mutex);` (or `ScopedLock lock(otherMutex);`) instead of `Mutex::scoped_lock ...` when possible.

## 7. Ownership and memory patterns
- Prefer engine ownership types (`UniquePtr`, `RefCountPtr`, `CustomUniquePtr`) over raw owning pointers.
- Use arena/custom allocators in core paths (`Alloc::CustomArena`, scratch allocators, `NewArenaObject`).
- Follow RAII cleanup in destructors for Vulkan/system handles and reset handles to null after destroy/free.

## 8. Error handling and diagnostics
- Use early returns for invalid states and failed preconditions.
- Check external API results immediately (for Vulkan, check `VkResult` after each call).
- Log through logger macros (`NWB_LOGGER_INFO`, `NWB_LOGGER_WARNING`, `NWB_LOGGER_ERROR`, `NWB_LOGGER_FATAL`).
- Use assertions (`NWB_ASSERT`, `NWB_ASSERT_MSG`) for invariant checking.

## 9. Performance-oriented conventions
- Use `constexpr` for static mappings and constants.
- Reserve container capacity when expected counts are known.
- Use thread pool and chunking thresholds for larger CPU-side memory/data operations.
- Prefer scratch/arena allocations in hot paths to minimize heap churn.
- Prefer `ScratchArena` for containers/temporary objects that only live within a local scope.
- For function-local temporary containers (`Vector`, `HashSet`, `HashMap`, etc.), default to `ScratchArena` + `ScratchAllocator`; use `CustomAllocator` only when data must outlive the current scope.
- Data captured by async jobs/callbacks (e.g., lambda captures submitted to `ThreadPool`/`JobSystem`) is considered outliving the current scope; do not back such captures with `ScratchArena`.
- For parallel containers (`ParallelQueue`, `ParallelVector`, `ParallelHashMap`, etc.), use a cache-aligned allocator that matches the owning arena (`CustomCacheAlignedAllocator`, `MemoryCacheAlignedAllocator`, `ScratchCacheAlignedAllocator`) instead of default allocators when arena ownership exists.

## 10. Practical checklist for new code
- Before every code create/modify action, re-read the current `standard.md` and apply it as the source of truth (do not rely on memory).
- Match file banner and section-separator style.
- Keep naming prefixes consistent (`m_`, `s_`, `g_`).
- Use module namespace wrapper macros and `__hidden_*` internal namespaces.
- Use project types/containers/macros when equivalents exist.
- Validate and log all external API failure paths.

## 11. New project layout (Visual Studio)
- New project source files must be listed in a `.vcxitems` file.
- The project `.vcxproj` must include/import the `.vcxitems`.
- Always separate source listing and project properties.
- `.vcxitems` contains source file entries only.
- `.vcxproj` contains project property/configuration definitions only.
- Design for reuse: one shared `.vcxitems` with multiple platform-specific `.vcxproj` files.

## 12. Class/Struct Layout and Order
- `struct` layout rule: declare member variables first, then member functions.
- `class` layout rule: declare member functions first, then member variables.
- Class-level type aliases (`using ...`) should be placed at the top of the class, before member function declarations.
- After the class-level type-alias block (`using ...`), keep two blank lines before the next access section or member declaration block.
- If class-level aliases and static helper/factory declarations share the same visibility, separate categories by reopening the access label (e.g., `private:` then `private:`).
- If class-level aliases and constructor/function declaration blocks share the same visibility, separate categories by reopening the access label (e.g., `public:` then `public:`).
- If a class-scope `static_assert(...)` appears before an access section, keep two blank lines before the next access label (`public:`, `private:`, `protected:`).
- Friend declarations (`friend class ...`, `friend ...`) must appear at the very beginning of the class.
- Internal helper declarations (`struct`, `enum`, `class` used only internally) should appear before normal function/member sections.
- Static member function declarations should appear before non-static helper/member function declarations in the same access section.
- Do not interleave static and non-static member declarations within the same class category; place all static declarations first, then non-static declarations.
- Keep class-level static helper/constant declarations in an early dedicated section before constructor/destructor declarations.
- When both static member variables and static member functions exist in the static section, declare static variables first, then static functions.
- Static helper/factory functions should be declared in an early dedicated section, not in the middle/bottom of the class after normal API blocks.
- If a static helper must be `private`, place a `private:` helper section near the top of the class (after aliases/helpers) before the main `public` API sections.
- Reusing the same access specifier (`public:`, `private:`, `protected:`) for category separation is allowed and preferred.
- When the category changes significantly, separate sections with two blank lines.
- When the category change is minor, one blank line is enough.
- Static member variables are declared before non-static member variables.
- Constructors/destructor are declared together with no empty line between them.
- If operator overload members exist, place them right after constructor/destructor declarations.
- Prefer `public` sections first, then `private`/`protected` sections, except for an early top `private:` helper section when needed for static helper/factory declarations.
- Once the member-variable section begins, do not declare additional member functions afterward.
- Prefer declaring `operator==` and `operator!=` outside the class/struct scope.

## 13. Declaration/Definition Order
- In `.cpp` files, function definitions must follow the declaration order from the corresponding class/struct declaration.
- Allowed:
  - Declaration order: `X()`, then `Y()`
  - Definition order: `X()`, then `Y()`
- Not allowed:
  - Declaration order: `X()`, then `Y()`
  - Definition order: `Y()`, then `X()`

## 14. Mutex Selection Guide (`global/sync.h`)
- Default choice: use `Futex` for non-recursive mutual exclusion in engine/runtime code.
- Keep `Mutex` (`tbb::mutex`) mainly for legacy compatibility and interop with code that is already standardized on TBB mutex types.
- Use `ScopedLock`/`LockGuard` for simple scope-based locking; use `UniqueLock` only when lock state must be manually controlled (unlock/relock, condition wait patterns).

### 14.1 Quick decision order
- If the same thread must re-enter the same lock by design, use `RecursiveMutex`.
- Else if the lock is read-mostly and read/write split is meaningful, use a shared mutex type:
  - `SharedMutex` for very short read/write critical sections and low latency spin behavior.
  - `SharedQueuingMutex` when fairness under contention is more important than lowest raw latency.
- Else if the lock protects initialization/static allocator bootstrap memory with zero-init/no-constructor constraints, use `MallocMutex`.
- Else if the critical section is extremely short and never blocks/sleeps/allocates, use `SpinMutex`.
- Else if contention is high and fairness matters for short sections, use `QueuingMutex`.
- Otherwise, use `Futex`.

### 14.2 Type-by-type guidance
- `Futex`:
  - Best for general-purpose engine locks.
  - Good when contention may happen and threads should park instead of hot-spinning.
  - Preferred default for `m_mutex` fields in systems code.
- `Mutex` (`tbb::mutex`):
  - Use for compatibility with existing TBB-oriented code paths or when changing the lock type would add migration risk without measurable gain.
  - Do not pick it by default for new code.
- `SpinMutex`:
  - Use only for tiny critical sections (few instructions, metadata/state flips).
  - Do not hold across calls that may block, allocate heavily, or trigger OS waits.
- `QueuingMutex`:
  - Use for short sections with sustained contention where FIFO-like fairness helps avoid starvation.
- `SharedMutex` (`tbb::spin_rw_mutex`):
  - Use for read-heavy data with tiny read-side critical sections and infrequent writes.
  - Avoid if read sections can become long or block.
- `SharedQueuingMutex` (`tbb::queuing_rw_mutex`):
  - Use for read-heavy scenarios with heavier contention/fairness needs than `SharedMutex`.
- `RecursiveMutex`:
  - Use only when reentrancy is required by API/call-graph design.
  - Do not use as a workaround for lock-order or ownership bugs.
- `MallocMutex`:
  - Use only where zero-initialized storage and constructor-free mutex behavior is required (allocator/bootstrap/early init paths).

### 14.3 Condition variable pairing
- Prefer `ConditionVariableAny` with `Futex`, `SpinMutex`, `QueuingMutex`, `RecursiveMutex`, or TBB mutex wrappers.
- `ConditionVariable` (`std::condition_variable`) requires `std::unique_lock<std::mutex>`; do not pair it with `Futex`/TBB mutex types.

### 14.4 Practical defaults
- If unsure, start with `Futex`.
- Upgrade to `QueuingMutex`/`SharedQueuingMutex` only after observing contention/fairness issues.
- Use spin-based locks (`SpinMutex`, `SharedMutex`) only for measured hot-path wins with very short lock hold times.
