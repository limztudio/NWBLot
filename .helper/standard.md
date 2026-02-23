# NWBLot Inferred Code Standard

Derived from `core/`, `global/`, and `logger/` source files (excluding `3rd_parties/`).  
Updated: 2026-02-23

## 1. File and module structure
- Use lowercase `snake_case` filenames for C++ source and headers.
- Start files with the project banner style:
  - `// limztudio@gmail.com`
  - A long `////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////` separator line.
- Use `#pragma once` in headers.
- Separate major file sections with the long slash separator and optional section comments.
- Source files must end with `////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////` followed by a double newline.
- Use UTF-8 encoding for source files.
- Use Windows-style newlines (`CRLF`).

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

## 10. Practical checklist for new code
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
