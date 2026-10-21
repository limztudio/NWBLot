// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "statement_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_statement_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_InitialReferencePathSegmentCount = 4u;
constexpr usize s_InitialStructFieldCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsNameInList(const Vector<MStringView, Alloc::ScratchArena>& names, MStringView name)noexcept{
    for(const MStringView currentName : names){
        if(currentName == name)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StatementParser::StatementParser(MStringView source, MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, MStringMap<Value>& variables, Document::DeclarationList& declarations)
    : m_cursor(source, arena, scratchArena, errors)
    , m_validator(m_cursor)
    , m_expressions(m_cursor, m_validator, variables)
    , m_bindDeclarations(m_cursor, arena, variables)
    , m_arena(arena)
    , m_scratchArena(scratchArena)
    , m_variables(variables)
    , m_declarations(declarations)
    , m_declaredStructs(scratchArena)
{}


bool StatementParser::parseInto(MString& outAssetType, MString& outAssetVariable){
    if(!parseStatements())
        return false;
    return finalizeExplicitDeclaration(outAssetType, outAssetVariable);
}

bool StatementParser::parseWithImplicitAsset(MString& outAssetType, MString& outAssetVariable, const MStringView assetType, const MStringView assetVariable){
    if(!declareImplicitAsset(outAssetType, outAssetVariable, assetType, assetVariable))
        return false;
    return parseStatements();
}


bool StatementParser::parseStatements(){
    while(m_cursor.m_current.type != TokenType::EndOfFile){
        if(!parseStatement())
            return false;
    }

    return !m_cursor.failed();
}

bool StatementParser::finalizeExplicitDeclaration(MString& outAssetType, MString& outAssetVariable){
    if(m_declarations.empty()){
        m_cursor.error(1u, 1u, "expected declaration");
        return false;
    }

    const Document::Declaration& declaration = m_declarations.front();
    outAssetType.assign(declaration.type.data(), declaration.type.size());
    outAssetVariable.assign(declaration.variable.data(), declaration.variable.size());
    if(m_declaredAssetVariable.empty())
        m_declaredAssetVariable = MStringView(outAssetVariable.data(), outAssetVariable.size());
    return true;
}

bool StatementParser::parseDeclaration(const MStringView typeName, const u32 typeLine, const u32 typeColumn){
    const MStringView variableName = m_cursor.m_current.text;
    const u32 variableLine = m_cursor.m_current.line;
    const u32 variableColumn = m_cursor.m_current.column;
    m_cursor.advance();

    Value initialValue(m_arena);
    if(m_cursor.m_current.type == TokenType::Equal){
        m_cursor.advance();
        initialValue = m_expressions.parseExpression();
        if(m_cursor.failed())
            return false;
    }

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after declaration"))
        return false;

    return declareVariable(typeName, variableName, Move(initialValue), typeLine, typeColumn, variableLine, variableColumn);
}

bool StatementParser::declareVariable(const MStringView typeName, const MStringView variableName, Value&& initialValue, const u32 typeLine, const u32 typeColumn, const u32 variableLine, const u32 variableColumn){
    if(typeName.empty()){
        m_cursor.error(typeLine, typeColumn, "type name must not be empty");
        return false;
    }
    if(variableName.empty()){
        m_cursor.error(variableLine, variableColumn, "variable name must not be empty");
        return false;
    }
    if(m_variables.find(variableName) != m_variables.end()){
        m_cursor.error(variableLine, variableColumn, "duplicate variable declaration");
        return false;
    }

    MString key(variableName.data(), variableName.size(), m_arena);
    if(!m_variables.emplace(Move(key), Move(initialValue)).second){
        m_cursor.error(variableLine, variableColumn, "duplicate variable declaration");
        return false;
    }
    m_declarations.emplace_back(typeName, variableName, m_arena);
    if(m_declaredAssetVariable.empty())
        m_declaredAssetVariable = variableName;
    return true;
}

bool StatementParser::declareImplicitAsset(MString& outAssetType, MString& outAssetVariable, const MStringView assetType, const MStringView assetVariable){
    if(assetType.empty() || assetVariable.empty()){
        m_cursor.error(1u, 1u, "implicit asset declaration requires asset type and variable");
        return false;
    }

    outAssetType.assign(assetType.data(), assetType.size());
    outAssetVariable.assign(assetVariable.data(), assetVariable.size());
    m_declaredAssetVariable = MStringView(outAssetVariable.data(), outAssetVariable.size());

    MString key(outAssetVariable.data(), outAssetVariable.size(), m_arena);
    if(!m_variables.emplace(Move(key), Value(m_arena)).second){
        m_cursor.error(1u, 1u, "duplicate variable declaration");
        return false;
    }
    m_declarations.emplace_back(assetType, assetVariable, m_arena);
    return true;
}

bool StatementParser::parseStatement(){
    Value attributes = parseAttributeList();
    if(m_cursor.failed())
        return false;

    if(m_cursor.m_current.type != TokenType::Identifier){
        m_cursor.errorExpected("expected identifier at start of statement");
        return false;
    }

    const MStringView firstName = m_cursor.m_current.text;
    const u32 firstLine = m_cursor.m_current.line;
    const u32 firstColumn = m_cursor.m_current.column;
    m_cursor.advance();

    if(firstName == LiteralView("struct") && m_cursor.m_current.type == TokenType::Identifier)
        return parseStructDeclaration(Move(attributes), firstLine, firstColumn);

    if(!attributes.asList().empty()){
        m_cursor.error(firstLine, firstColumn, "attributes are only supported on struct declarations");
        return false;
    }

    if(m_cursor.m_current.type == TokenType::Identifier){
        if(__hidden_metascript_statement_parser::IsNameInList(m_declaredStructs, firstName))
            return parseStructInstanceDeclaration(firstName);

        return parseDeclaration(firstName, firstLine, firstColumn);
    }

    return parseAssignment(firstName, firstLine, firstColumn);
}

bool StatementParser::parseAssignment(const MStringView firstName, const u32 firstLine, const u32 firstColumn){
    ExpressionParser::ScratchPath path{m_scratchArena};
    path.reserve(__hidden_metascript_statement_parser::s_InitialReferencePathSegmentCount);
    path.push_back(firstName);
    if(!m_expressions.parseReferencePath(path))
        return false;

    const TokenType::Enum assignOp = m_cursor.m_current.type;
    const u32 assignLine = m_cursor.m_current.line;
    const u32 assignColumn = m_cursor.m_current.column;
    if(assignOp != TokenType::Equal && assignOp != TokenType::PlusEqual && assignOp != TokenType::MinusEqual && assignOp != TokenType::StarEqual && assignOp != TokenType::SlashEqual){
        m_cursor.errorExpected("expected assignment operator");
        return false;
    }
    m_cursor.advance();

    Value rhs = m_expressions.parseExpression();
    if(m_cursor.failed())
        return false;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after expression"))
        return false;

    const auto target = resolveTarget(path, firstLine, firstColumn);
    if(!target)
        return false;
    Value& targetValue = **target;

    if(!m_validator.validateAssignment(assignOp, targetValue, rhs, assignLine, assignColumn))
        return false;

    switch(assignOp){
    case TokenType::Equal:
        targetValue = Move(rhs);
        break;
    case TokenType::PlusEqual:
        targetValue += rhs;
        break;
    case TokenType::MinusEqual:
        targetValue -= rhs;
        break;
    case TokenType::StarEqual:
        targetValue *= rhs;
        break;
    case TokenType::SlashEqual:
        targetValue /= rhs;
        break;
    default:
        break;
    }

    return true;
}


Value StatementParser::parseAttributeList(){
    Value attributes(m_arena);
    attributes.makeList();

    while(m_cursor.m_current.type == TokenType::LeftBracket){
        Value attribute = parseAttribute();
        if(m_cursor.failed())
            return Value(m_arena);
        attributes.append(Move(attribute));
    }

    return attributes;
}

Value StatementParser::parseAttribute(){
    if(!m_cursor.expect(TokenType::LeftBracket, "expected '[' before attribute"))
        return Value(m_arena);

    if(m_cursor.m_current.type != TokenType::Identifier){
        m_cursor.errorExpected("expected attribute name");
        return Value(m_arena);
    }

    const MStringView attributeName = m_cursor.m_current.text;
    m_cursor.advance();

    Value attribute(m_arena);
    attribute.makeMap();
    attribute.field(LiteralView("name")).setString(attributeName);

    Value& arguments = attribute.field(LiteralView("arguments"));
    arguments.makeList();

    if(m_cursor.m_current.type == TokenType::LeftParen){
        m_cursor.advance();
        if(m_cursor.m_current.type != TokenType::RightParen){
            for(;;){
                Value argument = parseAttributeArgument();
                if(m_cursor.failed())
                    return Value(m_arena);
                arguments.append(Move(argument));

                if(m_cursor.m_current.type == TokenType::RightParen)
                    break;
                if(!m_cursor.expect(TokenType::Comma, "expected ',' or ')' in attribute arguments"))
                    return Value(m_arena);
            }
        }

        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after attribute arguments"))
            return Value(m_arena);
    }

    if(!m_cursor.expect(TokenType::RightBracket, "expected ']' after attribute"))
        return Value(m_arena);

    return attribute;
}

Value StatementParser::parseAttributeArgument(){
    if(m_cursor.m_current.type == TokenType::StringLiteral){
        const MStringView text = m_cursor.m_current.text;
        m_cursor.advance();
        return Value(text, m_arena);
    }

    m_cursor.errorExpected("expected string attribute argument");
    return Value(m_arena);
}


bool StatementParser::parseStructDeclaration(Value&& attributes, const u32 structLine, const u32 structColumn){
    const MStringView structName = m_cursor.m_current.text;
    m_cursor.advance();

    if(__hidden_metascript_statement_parser::IsNameInList(m_declaredStructs, structName)){
        m_cursor.error(structLine, structColumn, "duplicate struct declaration");
        return false;
    }

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' after struct name"))
        return false;

    Value fields(m_arena);
    fields.makeList();

    ScratchNameList fieldNames{m_scratchArena};
    fieldNames.reserve(__hidden_metascript_statement_parser::s_InitialStructFieldCount);

    while(m_cursor.m_current.type != TokenType::RightBrace){
        if(m_cursor.m_current.type == TokenType::EndOfFile){
            m_cursor.errorExpected("expected field declaration or '}' in struct");
            return false;
        }

        Value fieldAttributes = parseAttributeList();
        if(m_cursor.failed())
            return false;

        if(m_cursor.m_current.type == TokenType::RightBrace){
            if(!fieldAttributes.asList().empty()){
                m_cursor.errorExpected("expected field declaration after attributes");
                return false;
            }
            break;
        }

        Value field = parseStructField(Move(fieldAttributes), fieldNames);
        if(m_cursor.failed())
            return false;
        fields.append(Move(field));
    }

    if(!m_cursor.expect(TokenType::RightBrace, "expected '}' after struct fields"))
        return false;
    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after struct declaration"))
        return false;

    if(!m_bindDeclarations.addStruct(m_declaredAssetVariable, structName, Move(attributes), Move(fields), structLine, structColumn))
        return false;

    m_declaredStructs.push_back(structName);
    return true;
}

Value StatementParser::parseStructField(Value&& attributes, ScratchNameList& fieldNames){
    if(m_cursor.m_current.type != TokenType::Identifier){
        m_cursor.errorExpected("expected field type");
        return Value(m_arena);
    }
    const MStringView typeName = m_cursor.m_current.text;
    m_cursor.advance();

    if(m_cursor.m_current.type != TokenType::Identifier){
        m_cursor.errorExpected("expected field name after type");
        return Value(m_arena);
    }
    const MStringView fieldName = m_cursor.m_current.text;
    const u32 fieldLine = m_cursor.m_current.line;
    const u32 fieldColumn = m_cursor.m_current.column;
    m_cursor.advance();

    if(__hidden_metascript_statement_parser::IsNameInList(fieldNames, fieldName)){
        m_cursor.error(fieldLine, fieldColumn, "duplicate struct field declaration");
        return Value(m_arena);
    }

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after field declaration"))
        return Value(m_arena);

    fieldNames.push_back(fieldName);

    Value field(m_arena);
    field.makeMap();
    field.field(LiteralView("type")).setString(typeName);
    field.field(LiteralView("name")).setString(fieldName);
    field.field(LiteralView("attributes")) = Move(attributes);
    return field;
}

bool StatementParser::parseStructInstanceDeclaration(MStringView typeName){
    const MStringView instanceName = m_cursor.m_current.text;
    const u32 instanceLine = m_cursor.m_current.line;
    const u32 instanceColumn = m_cursor.m_current.column;
    m_cursor.advance();

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after instance declaration"))
        return false;

    return m_bindDeclarations.addInstance(m_declaredAssetVariable, typeName, instanceName, instanceLine, instanceColumn);
}


Expected<NotNull<Value*>> StatementParser::resolveTarget(const ExpressionParser::ScratchPath& path, const u32 errorLine, const u32 errorColumn){
    NWB_ASSERT(!path.empty());

    const auto rootName = path[0];
    auto rootIt = m_variables.find(rootName);
    if(rootIt == m_variables.end()){
        m_cursor.error(errorLine, errorColumn, "assignments must target a declared variable");
        return MakeUnexpected(Failure{});
    }

    auto current = MakeNotNull(&rootIt.value());
    for(usize i = 1; i < path.size(); ++i){
        if(current->isNull())
            current->makeMap();
        else if(!current->isMap()){
            m_cursor.error(errorLine, errorColumn, "cannot access field on non-map value");
            return MakeUnexpected(Failure{});
        }

        current = &current->field(path[i]);
    }

    return current;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

