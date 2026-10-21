// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "parser_cursor.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Records `.bind` struct and instance declarations under the declared asset's `structs` map and `instances` list.
class BindDeclarationBuilder{
public:
    BindDeclarationBuilder(ParserCursor& cursor, MetaArena& arena, MStringMap<Value>& variables)noexcept
        : m_cursor(cursor)
        , m_arena(arena)
        , m_variables(variables)
    {}


public:
    bool addStruct(MStringView assetVariable, MStringView structName, Value&& attributes, Value&& fields, u32 line, u32 column);
    bool addInstance(MStringView assetVariable, MStringView typeName, MStringView instanceName, u32 line, u32 column);


private:
    [[nodiscard]] Expected<NotNull<Value*>> assetRootMap(MStringView assetVariable, u32 line, u32 column);
    bool ensureMapValue(Value& value, u32 line, u32 column, MStringView message);
    bool ensureListValue(Value& value, u32 line, u32 column, MStringView message);


private:
    ParserCursor& m_cursor;
    MetaArena& m_arena;
    MStringMap<Value>& m_variables;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

