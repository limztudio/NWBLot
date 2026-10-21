// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bind_declaration_builder.h"
#include "expression_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Parses declarations, attributes, `.bind` structs and assignments into one document's variables.
class StatementParser{
private:
    using ScratchNameList = Vector<MStringView, Alloc::ScratchArena>;


public:
    StatementParser(MStringView source, MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, MStringMap<Value>& variables, Document::DeclarationList& declarations);


public:
    bool parseInto(MString& outAssetType, MString& outAssetVariable);
    bool parseWithImplicitAsset(MString& outAssetType, MString& outAssetVariable, MStringView assetType, MStringView assetVariable);


private:
    bool parseStatements();
    bool finalizeExplicitDeclaration(MString& outAssetType, MString& outAssetVariable);
    bool parseDeclaration(MStringView typeName, u32 typeLine, u32 typeColumn);
    bool declareVariable(MStringView typeName, MStringView variableName, Value&& initialValue, u32 typeLine, u32 typeColumn, u32 variableLine, u32 variableColumn);
    bool declareImplicitAsset(MString& outAssetType, MString& outAssetVariable, MStringView assetType, MStringView assetVariable);
    bool parseStatement();
    bool parseAssignment(MStringView firstName, u32 firstLine, u32 firstColumn);

    Value parseAttributeList();
    Value parseAttribute();
    Value parseAttributeArgument();

    bool parseStructDeclaration(Value&& attributes, u32 structLine, u32 structColumn);
    Value parseStructField(Value&& attributes, ScratchNameList& fieldNames);
    bool parseStructInstanceDeclaration(MStringView typeName);

    [[nodiscard]] Expected<NotNull<Value*>> resolveTarget(const ExpressionParser::ScratchPath& path, u32 errorLine, u32 errorColumn);


private:
    ParserCursor m_cursor;
    OperationValidator m_validator;
    ExpressionParser m_expressions;
    BindDeclarationBuilder m_bindDeclarations;

    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    MStringMap<Value>& m_variables;
    Document::DeclarationList& m_declarations;

    ScratchNameList m_declaredStructs;
    MStringView m_declaredAssetVariable;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

