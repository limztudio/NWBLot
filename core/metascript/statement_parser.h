// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expression_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Parses variables, struct descriptions, attributes and assignments without selecting a document root.
class StatementParser{
private:
    using ScratchNameList = Vector<MStringView, Alloc::ScratchArena>;


public:
    StatementParser(MStringView source, MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, MStringMap<Value>& variables, Document::DeclarationList& declarations, Document::StructMap& structs);


public:
    bool parseInto();


private:
    bool parseDeclaration(MStringView typeName, u32 typeLine, u32 typeColumn);
    bool declareVariable(MStringView typeName, MStringView variableName, Value&& initialValue, u32 typeLine, u32 typeColumn, u32 variableLine, u32 variableColumn);
    bool parseStatement();
    bool parseAssignment(MStringView firstName, u32 firstLine, u32 firstColumn);

    Value parseAttributeList();
    Value parseAttribute();
    Value parseAttributeArgument();

    bool parseStructDeclaration(Value&& attributes, u32 structLine, u32 structColumn);
    Value parseStructField(Value&& attributes, ScratchNameList& fieldNames);

    [[nodiscard]] Expected<NotNull<Value*>> resolveTarget(const ExpressionParser::ScratchPath& path, u32 errorLine, u32 errorColumn);


private:
    ParserCursor m_cursor;
    OperationValidator m_validator;
    ExpressionParser m_expressions;

    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    MStringMap<Value>& m_variables;
    Document::DeclarationList& m_declarations;
    Document::StructMap& m_structs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

