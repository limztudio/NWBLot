# MetaScript

MetaScript parses metadata into arena-backed values and declarations. It supports declarations, nested field assignments, literal arithmetic, symbolic references, lists, dictionaries, and the struct/attribute syntax used by material `.bind` files.

The public API lives in `NWB::Core::Metascript`. Include [parser.h](parser.h) for `Document`, [value.h](value.h) for values, or [lexer.h](lexer.h) for token-level tools. Link the `nwb_metascript` static library:

```cmake
target_link_libraries(your_target PRIVATE nwb_metascript)
```

The target publicly depends on `nwb_alloc` and `nwb::tbb`.

## Authoring metadata

This example illustrates the language; accepted asset types and fields are defined by the consuming cooker.

```text
example asset;

asset.name = "example";
asset.width = 256 * 2;
asset.scale = (2 + 3) * 0.5;
asset.tags = ["opaque", "lit",];
asset.tags += "featured";
asset.tags += ["desktop", "preview"];
asset.parameters.roughness = 0.6;

texture albedo;
albedo.source = "textures/example.png";
asset.albedo = albedo;
```

A declaration has the form `<type> <variable> [= <expression>];`. Type names are recorded as text and do not constrain the stored value. Every variable is available by name through `Document::findField()`, and `findDeclaration()` returns its declared type and variable name. `declarations()` preserves source order. MetaScript gives no special meaning to `asset` or to the first declaration; a cooker may choose `asset` as its own entry convention.

Omitting an initializer creates a `Null` value. Assigning a dotted path creates missing fields and turns `Null` parents into maps. Assignment requires an already declared root, and traversing a scalar or list is an error. Variable names must be unique within the document.

Identifiers use ASCII letters, digits, and underscores, with a letter or underscore first. Statements require semicolons. Whitespace, `//` line comments, and non-nested `/* ... */` block comments are supported.

### Values and literals

| Value type | Source form | Behavior |
| --- | --- | --- |
| `Null` | `example asset;` | Initial state of an uninitialized variable or a newly created field; there is no `null` literal. |
| `Integer` | `42`, `-7` | Signed 64-bit integer. |
| `Double` | `0.5`, `1e3`, `1.2E-3` | Double precision floating point; fractional notation requires digits on both sides of the dot. |
| `String` | `"textures/example.png"` | Double quoted text with no raw newline. |
| `Reference` | `albedo`, `asset.parameters` | Symbolic path rooted at an already declared variable. |
| `List` | `[1, 2]`, `{1, 2}`, `[]`, `{}` | Ordered values; square and brace list forms are both accepted. |
| `Map` | `{"name": "example", "width": 512}` | Dictionary with string keys. |

Lists and maps allow trailing commas. Duplicate dictionary keys replace earlier values. **`{}` is an empty list.** Maps can be created through dotted assignment or a nonempty dictionary literal; there is no empty map literal.

String contents retain backslashes verbatim. For example, `"a\nb"` contains a backslash and `n`, rather than a newline. A backslash lets the lexer skip the following character while finding the closing quote, but the parser does not decode escapes. Use forward slashes for asset paths.

There is no boolean value type or boolean literal. Indexing, function calls, comparisons, logical operators, conditionals, and loops are outside the language.

### Expressions and assignment

Unary `-` binds most tightly, followed by `*` and `/`, then `+` and `-`. Binary operators associate left to right; parentheses group expressions. Unary `+` is unsupported.

| Operator | Accepted operands |
| --- | --- |
| `+` | Numeric pairs, string + string, or list + list. |
| `-`, `*`, `/` | Numeric pairs. |
| `=` | Any value; replaces the target value. |
| `+=` | Numeric pairs, string += string, or a list target. A list right hand side appends its elements; another value appends one element. |
| `-=`, `*=`, `/=` | Numeric pairs. |

Integer arithmetic stays integer, including division that truncates toward zero. Mixing integers and doubles produces a double. The parser rejects division by zero, signed integer overflow, and incompatible operand types. Initialize list targets before using `+=`, for example `asset.items = []; asset.items += 1;`.

### Symbolic references

On the right hand side, identifiers produce `Reference` values containing the dotted path. They do not read or copy the current contents of that variable:

```text
example asset;
texture albedo;

asset.texture = albedo;             // Reference text: "albedo"
asset.source = albedo.source;       // Reference text: "albedo.source"
```

Only the root variable must exist at parse time; the parser does not validate the referenced fields or resolve the reference. Resolution belongs to the consumer. For the same reason, `number count = 2; asset.width = count + 1;` fails: `count` is a reference operand, not an integer expression. Use literal arithmetic or compound assignment on an initialized numeric target.

## Using the C++ API

Construct a `Document` with a caller-owned `MetaArena`, which aliases `NWB::Core::Alloc::GlobalArena`. Parse before looking up a variable, then validate the expected value types:

```cpp
#include <core/metascript/parser.h>

namespace Meta = NWB::Core::Metascript;

bool ReadExampleWidth(Meta::MetaArena& arena, i64& outWidth){
    Meta::Document document(arena);
    if(!document.parse(Meta::LiteralView(R"(
        example settings;
        settings.width = 256 * 2;
    )"))){
        // Inspect document.errors() here for line, column, and message.
        return false;
    }

    const Meta::Value* settings = document.findField(Meta::LiteralView("settings"));
    if(!settings || !settings->isMap())
        return false;

    const Meta::Value* width = settings->findField(Meta::LiteralView("width"));
    if(!width || !width->isInteger())
        return false;

    outWidth = width->asInteger();
    return true;
}
```

`LiteralView()` creates a length-aware `MStringView` from a character array. For other buffers, use `MStringView(data, size)`.

### Document entry points

| API | Contract |
| --- | --- |
| `parse(MStringView)` | Parses a complete source buffer. Empty sources and sources containing only structs are valid. |
| `parse(IMetaReader&)` | Reads the complete source, then applies the same parsing rules. |
| `findField(name)` | Returns a borrowed pointer to a top-level variable, or `nullptr` when absent. Const and mutable overloads are available; lookup never creates a variable. |
| `findDeclaration(name)` | Returns a borrowed pointer to a variable's declaration (`type`, `variable`), or `nullptr` when absent. |
| `declarations()` | Returns every variable declaration in source order, including declarations with a struct type. |
| `structs()` | Returns the independent struct definition map, keyed by struct name. Each value describes its attributes and fields. |
| `hasErrors()`, `errors()` | Exposes parse diagnostics containing `line`, `column`, and `message`. |

Each parse call clears the previous document contents and diagnostics. A failed parse can leave partially populated values; consume the result only when parsing returns `true`. Source diagnostics use one-based line and column positions; reader failures use `(0, 0)`.

`IMetaReader::read(buffer, maxBytes)` returns `Expected<usize>`. Return a positive byte count for data, zero for EOF, or an unexpected result for a read failure. The document requests chunks of 4096 bytes and buffers all input before parsing. A failure reports `"read error"`; a count larger than requested reports `"reader returned more bytes than requested"`. Exceptions from readers or allocations propagate to the caller.

### Values and ownership

Use `isInteger()`, `isMap()`, and the other `is*()` predicates before calling the corresponding `as*()` accessors. These accessors assert their required types. `toDouble()` accepts either numeric type. `Value::findField(name)` requires a map and returns `nullptr` for a missing field. The free `FindField(value, AStringView)` helper forwards to that lookup.

For programmatic construction, `field(name)` promotes `Null` to a map and creates missing `Null` fields; `append(Value&&)` promotes `Null` to a list. `makeMap()`, `makeList()`, and the `set*()` methods replace the current value. `copyString()` makes an owned string copy. `copyStringList(out)` first validates that every element is a string, then appends copies to the output container; it returns `false` for an incompatible value.

The source buffer needs to remain valid only during the synchronous parse. Parsed text, keys, reference paths, declarations, and diagnostics are copied into the supplied arena. Keep the arena alive for the document and every value or copied string allocated from it. Views and pointers returned by the API borrow their underlying storage and can be invalidated by mutation, reparsing, or destruction.

A `Value` copy constructor uses the source arena. Copy and move assignment retain the destination arena and recursively copy across different arenas; a cross-arena move clears the source after copying. To transfer data into another arena, construct a destination `Value` with that arena and assign into it.

## Structs and attributes for `.bind` files

Struct declarations describe fields and attributes as metadata:

```text
[material_mutable]
struct Parameters {
    [default("half4(1.0h, 1.0h, 1.0h, 1.0h)")]
    half4 tint;
};

Parameters runtime;
```

Attributes can appear on structs and fields. Each attribute accepts zero or more string arguments, for example `[flag]`, `[flag()]`, or `[name("a", "b")]`; attribute argument lists do not allow a trailing comma. Field declarations consist of a type and name followed by a semicolon.

Struct definitions have their own namespace and require no variable declaration or selected root. The example records this descriptor in `document.structs()`:

```text
Parameters:
    attributes: [{"name": "material_mutable", "arguments": []}]
    fields:
        - {"type": "half4", "name": "tint",
           "attributes": [{"name": "default",
                           "arguments": ["half4(1.0h, 1.0h, 1.0h, 1.0h)"]}]}
```

`Parameters runtime;` is an ordinary variable declaration: `document.findDeclaration("runtime")` records the type `Parameters`, and `document.findField("runtime")` returns its initially `Null` value. Initializers and assignments follow the same rules as any other variable. The parser does not instantiate fields, check whether a declared type names a struct, or enforce a struct's field schema. Struct definitions can appear before or after variable declarations.

Duplicate struct names, field names within a struct, and variable names are errors. A struct and a variable may share a name. Struct definitions do not create variables named `structs`, `instances`, or `asset`, and variable assignments cannot change the struct definition map.

The material bind consumer uses struct descriptors and typed variable declarations to construct its own structs and instances. Attribute meanings, supported field types, instance initialization restrictions, and binding layouts are validated by material code. Its source can contain just the structs and instances shown above.

## Integration and source layout

MetaScript handles syntax and generic value operations. File access, UTF-8 BOM removal, asset type dispatch, schema validation, dependency expansion, and reference resolution belong to callers. The asset metadata entry point is [cook_metadata.cpp](../assets/cook_metadata.cpp); bunch expansion is in [core/assets/bunch](../assets/bunch). Concrete schemas live under `impl/assets_*`, including the [shader](../../impl/assets_shader/README.md) and [UI skin](../../impl/assets_ui_skin/README.md) guides. The FBX metadata refresh utility also uses the lexer directly.

| Files | Responsibility |
| --- | --- |
| `global.h`, `arena_names.h` | Arena/container aliases, source reader interface, and allocation/diagnostic names. |
| `value.h`, `value.cpp` | Tagged value storage, arithmetic, construction, and arena ownership. |
| `lexer.h`, `lexer.cpp` | Tokenization and source positions. Token text views borrow the lexer source. |
| `parser.h`, `parser.cpp` | `Document`, parse entry points, and reader buffering. |
| `parser_cursor.*` | Shared token cursor and diagnostics. |
| `expression_parser.*`, `operation_validator.*` | Literal/reference expressions and operand validation. |
| `statement_parser.*` | Variable declarations, assignments, attributes, and independent struct descriptions. |

## Build and tests

Run from the repository root with the normal NWBLot build prerequisites. For Windows ARM64:

```powershell
python -m launcher build nwb_metascript_tests --configure-preset windows-clang-arm64 --config dbg
ctest --preset windows-clang-arm64-dbg -R '^nwb_metascript_tests$' --output-on-failure
```

For Windows x64, use `windows-clang-x64` for configuration and `windows-clang-dbg` for CTest. For Linux x64:

```sh
python3 -m launcher build nwb_metascript_tests --configure-preset linux-clang-x64 --config dbg
ctest --preset linux-clang-dbg -R '^nwb_metascript_tests$' --output-on-failure
```

The full presets enable tests. Engine or project presets require `-D NWB_BUILD_TESTS=ON`. Build `nwb_metascript` instead if only the library is needed. See the [launcher guide](../../launcher/README.md) and [test guide](../../tests/README.md) for other configurations.

The [unit suite](../../tests/unit/metascript) covers arena transfer and aliasing, bind declaration boundaries, invalid assignment targets, and reader failure handling.
