// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parser.h"

#include "arena_names.h"

#include <global/core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BinaryI64Overflows(const TokenType::Enum op, const i64 lhs, const i64 rhs){
    switch(op){
    case TokenType::Plus:
    case TokenType::PlusEqual:
        return AddOverflows<i64>(lhs, rhs);
    case TokenType::Minus:
    case TokenType::MinusEqual:
        return SubtractOverflows<i64>(lhs, rhs);
    case TokenType::Star:
    case TokenType::StarEqual:
        return MultiplyOverflows<i64>(lhs, rhs);
    case TokenType::Slash:
    case TokenType::SlashEqual:
        return DivideOverflows<i64>(lhs, rhs);
    default:
        break;
    }

    return false;
}

template<usize N>
[[nodiscard]] constexpr MStringView LiteralView(const char (&text)[N]){
    return MStringView(text, N > 0u ? N - 1u : 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Parser{
public:
    Parser(
        MStringView source,
        MetaArena& arena,
        MVector<ParseError>& errors,
        MStringMap<Value>& variables,
        Document::DeclarationList& declarations
    )
        : m_lexer(source)
        , m_arena(arena)
        , m_scratchArena(MetascriptArenaScope::s_ParserScratch, 4096)
        , m_declaredStructs(m_scratchArena)
        , m_errors(errors)
        , m_variables(variables)
        , m_declarations(declarations)
    {
        advance();
    }


public:
    bool parseInto(MString& outAssetType, MString& outAssetVariable){
        try{
            if(!parseStatements())
                return false;
            return finalizeExplicitDeclaration(outAssetType, outAssetVariable);
        }
        catch(const GeneralException& e){
            error(
                m_current.line,
                m_current.column,
                MStringView(e.what(), MString::traits_type::length(e.what()))
            );
            return false;
        }
    }

    bool parseWithImplicitAsset(
        MString& outAssetType,
        MString& outAssetVariable,
        const MStringView assetType,
        const MStringView assetVariable
    ){
        try{
            if(!declareImplicitAsset(outAssetType, outAssetVariable, assetType, assetVariable))
                return false;
            return parseStatements();
        }
        catch(const GeneralException& e){
            error(
                m_current.line,
                m_current.column,
                MStringView(e.what(), MString::traits_type::length(e.what()))
            );
            return false;
        }
    }


private:
    using ScratchPath = Vector<MStringView, Alloc::ScratchArena>;
    using ScratchNameList = Vector<MStringView, Alloc::ScratchArena>;
    using ScratchString = BasicString<MChar, Alloc::ScratchArena>;


    bool parseStatements(){
        while(m_current.type != TokenType::EndOfFile){
            if(!parseStatement())
                return false;
        }

        return m_errors.empty();
    }

    bool finalizeExplicitDeclaration(MString& outAssetType, MString& outAssetVariable){
        if(m_declarations.empty()){
            error(1u, 1u, "expected declaration");
            return false;
        }

        const Document::Declaration& declaration = m_declarations.front();
        outAssetType.assign(declaration.type.data(), declaration.type.size());
        outAssetVariable.assign(declaration.variable.data(), declaration.variable.size());
        if(m_declaredAssetVariable.empty())
            m_declaredAssetVariable = MStringView(outAssetVariable.data(), outAssetVariable.size());
        return true;
    }

    bool parseDeclaration(const MStringView typeName, const u32 typeLine, const u32 typeColumn){
        const MStringView variableName = m_current.text;
        const u32 variableLine = m_current.line;
        const u32 variableColumn = m_current.column;
        advance();

        Value initialValue(m_arena);
        if(m_current.type == TokenType::Equal){
            advance();
            initialValue = parseExpression();
            if(!m_errors.empty())
                return false;
        }

        if(!expect(TokenType::Semicolon, "expected ';' after declaration"))
            return false;

        return declareVariable(typeName, variableName, Move(initialValue), typeLine, typeColumn, variableLine, variableColumn);
    }

    bool declareVariable(
        const MStringView typeName,
        const MStringView variableName,
        Value&& initialValue,
        const u32 typeLine,
        const u32 typeColumn,
        const u32 variableLine,
        const u32 variableColumn
    ){
        if(typeName.empty()){
            error(typeLine, typeColumn, "type name must not be empty");
            return false;
        }
        if(variableName.empty()){
            error(variableLine, variableColumn, "variable name must not be empty");
            return false;
        }
        if(m_variables.find(variableName) != m_variables.end()){
            error(variableLine, variableColumn, "duplicate variable declaration");
            return false;
        }

        MString key(variableName.data(), variableName.size(), m_arena);
        m_variables.emplace(Move(key), Move(initialValue));
        m_declarations.emplace_back(typeName, variableName, m_arena);
        if(m_declaredAssetVariable.empty()){
            const Document::Declaration& declaration = m_declarations.back();
            m_declaredAssetVariable = MStringView(declaration.variable.data(), declaration.variable.size());
        }
        return true;
    }

    bool declareImplicitAsset(
        MString& outAssetType,
        MString& outAssetVariable,
        const MStringView assetType,
        const MStringView assetVariable
    ){
        if(assetType.empty() || assetVariable.empty()){
            error(1u, 1u, "implicit asset declaration requires asset type and variable");
            return false;
        }

        outAssetType.assign(assetType.data(), assetType.size());
        outAssetVariable.assign(assetVariable.data(), assetVariable.size());
        m_declaredAssetVariable = MStringView(outAssetVariable.data(), outAssetVariable.size());

        MString key(outAssetVariable.data(), outAssetVariable.size(), m_arena);
        m_variables.emplace(Move(key), Value(m_arena));
        m_declarations.emplace_back(assetType, assetVariable, m_arena);
        return true;
    }

    bool parseStatement(){
        Value attributes = parseAttributeList();
        if(!m_errors.empty())
            return false;

        if(m_current.type != TokenType::Identifier){
            errorExpected("expected identifier at start of statement");
            return false;
        }

        const MStringView firstName = m_current.text;
        const u32 firstLine = m_current.line;
        const u32 firstColumn = m_current.column;
        advance();

        if(firstName == LiteralView("struct") && m_current.type == TokenType::Identifier)
            return parseStructDeclaration(Move(attributes), firstLine, firstColumn);

        if(!attributes.asList().empty()){
            error(firstLine, firstColumn, "attributes are only supported on struct declarations");
            return false;
        }

        if(m_current.type == TokenType::Identifier){
            if(isDeclaredStruct(firstName))
                return parseStructInstanceDeclaration(firstName);

            return parseDeclaration(firstName, firstLine, firstColumn);
        }

        ScratchPath path{m_scratchArena};
        path.reserve(4);
        path.push_back(firstName);

        while(m_current.type == TokenType::Dot){
            advance();
            if(m_current.type != TokenType::Identifier){
                errorExpected("expected identifier after '.'");
                return false;
            }
            path.push_back(m_current.text);
            advance();
        }

        const TokenType::Enum assignOp = m_current.type;
        const u32 assignLine = m_current.line;
        const u32 assignColumn = m_current.column;
        if(
            assignOp != TokenType::Equal
            && assignOp != TokenType::PlusEqual
            && assignOp != TokenType::MinusEqual
            && assignOp != TokenType::StarEqual
            && assignOp != TokenType::SlashEqual
        ){
            errorExpected("expected assignment operator");
            return false;
        }
        advance();

        Value rhs = parseExpression();
        if(!m_errors.empty())
            return false;

        if(!expect(TokenType::Semicolon, "expected ';' after expression"))
            return false;

        Value* target = resolveTarget(path, assignOp == TokenType::Equal, firstLine, firstColumn);
        if(!target)
            return false;

        if(!validateAssignment(assignOp, *target, rhs, assignLine, assignColumn))
            return false;

        switch(assignOp){
        case TokenType::Equal:
            *target = Move(rhs);
            break;
        case TokenType::PlusEqual:
            *target += rhs;
            break;
        case TokenType::MinusEqual:
            *target -= rhs;
            break;
        case TokenType::StarEqual:
            *target *= rhs;
            break;
        case TokenType::SlashEqual:
            *target /= rhs;
            break;
        default:
            break;
        }

        return true;
    }

    Value parseAttributeList(){
        Value attributes(m_arena);
        attributes.makeList();

        while(m_current.type == TokenType::LeftBracket){
            Value attribute = parseAttribute();
            if(!m_errors.empty())
                return Value(m_arena);
            attributes.append(Move(attribute));
        }

        return attributes;
    }

    Value parseAttribute(){
        if(!expect(TokenType::LeftBracket, "expected '[' before attribute"))
            return Value(m_arena);

        if(m_current.type != TokenType::Identifier){
            errorExpected("expected attribute name");
            return Value(m_arena);
        }

        const MStringView attributeName = m_current.text;
        advance();

        Value attribute(m_arena);
        attribute.makeMap();
        attribute.field(LiteralView("name")).setString(attributeName);

        Value& arguments = attribute.field(LiteralView("arguments"));
        arguments.makeList();

        if(m_current.type == TokenType::LeftParen){
            advance();
            if(m_current.type != TokenType::RightParen){
                for(;;){
                    Value argument = parseAttributeArgument();
                    if(!m_errors.empty())
                        return Value(m_arena);
                    arguments.append(Move(argument));

                    if(m_current.type == TokenType::RightParen)
                        break;
                    if(!expect(TokenType::Comma, "expected ',' or ')' in attribute arguments"))
                        return Value(m_arena);
                }
            }

            if(!expect(TokenType::RightParen, "expected ')' after attribute arguments"))
                return Value(m_arena);
        }

        if(!expect(TokenType::RightBracket, "expected ']' after attribute"))
            return Value(m_arena);

        return attribute;
    }

    Value parseAttributeArgument(){
        if(m_current.type == TokenType::StringLiteral){
            const MStringView text = m_current.text;
            advance();
            return Value(text, m_arena);
        }

        errorExpected("expected string attribute argument");
        return Value(m_arena);
    }

    bool parseStructDeclaration(Value&& attributes, const u32 structLine, const u32 structColumn){
        const MStringView structName = m_current.text;
        advance();

        if(isDeclaredStruct(structName)){
            error(structLine, structColumn, "duplicate struct declaration");
            return false;
        }

        if(!expect(TokenType::LeftBrace, "expected '{' after struct name"))
            return false;

        Value fields(m_arena);
        fields.makeList();

        ScratchNameList fieldNames{m_scratchArena};
        fieldNames.reserve(8);

        while(m_current.type != TokenType::RightBrace){
            if(m_current.type == TokenType::EndOfFile){
                errorExpected("expected field declaration or '}' in struct");
                return false;
            }

            Value fieldAttributes = parseAttributeList();
            if(!m_errors.empty())
                return false;

            if(m_current.type == TokenType::RightBrace){
                if(!fieldAttributes.asList().empty()){
                    errorExpected("expected field declaration after attributes");
                    return false;
                }
                break;
            }

            Value field = parseStructField(Move(fieldAttributes), fieldNames);
            if(!m_errors.empty())
                return false;
            fields.append(Move(field));
        }

        if(!expect(TokenType::RightBrace, "expected '}' after struct fields"))
            return false;
        if(!expect(TokenType::Semicolon, "expected ';' after struct declaration"))
            return false;

        if(!addBindStruct(structName, Move(attributes), Move(fields), structLine, structColumn))
            return false;

        m_declaredStructs.push_back(structName);
        return true;
    }

    Value parseStructField(Value&& attributes, ScratchNameList& fieldNames){
        if(m_current.type != TokenType::Identifier){
            errorExpected("expected field type");
            return Value(m_arena);
        }
        const MStringView typeName = m_current.text;
        advance();

        if(m_current.type != TokenType::Identifier){
            errorExpected("expected field name after type");
            return Value(m_arena);
        }
        const MStringView fieldName = m_current.text;
        const u32 fieldLine = m_current.line;
        const u32 fieldColumn = m_current.column;
        advance();

        if(isNameInList(fieldNames, fieldName)){
            error(fieldLine, fieldColumn, "duplicate struct field declaration");
            return Value(m_arena);
        }

        if(!expect(TokenType::Semicolon, "expected ';' after field declaration"))
            return Value(m_arena);

        fieldNames.push_back(fieldName);

        Value field(m_arena);
        field.makeMap();
        field.field(LiteralView("type")).setString(typeName);
        field.field(LiteralView("name")).setString(fieldName);
        field.field(LiteralView("attributes")) = Move(attributes);
        return field;
    }

    bool parseStructInstanceDeclaration(MStringView typeName){
        const MStringView instanceName = m_current.text;
        const u32 instanceLine = m_current.line;
        const u32 instanceColumn = m_current.column;
        advance();

        if(!expect(TokenType::Semicolon, "expected ';' after instance declaration"))
            return false;

        return addBindInstance(typeName, instanceName, instanceLine, instanceColumn);
    }


    Value parseExpression(){
        return parseAdditive();
    }

    template<typename ParseNext, typename ApplyOperation>
    Value parseBinaryExpression(ParseNext&& parseNext, const TokenType::Enum lhsOp, const TokenType::Enum rhsOp, ApplyOperation&& applyOperation){
        Value left = parseNext();
        if(!m_errors.empty())
            return left;

        while(m_current.type == lhsOp || m_current.type == rhsOp){
            const auto op = m_current.type;
            const u32 opLine = m_current.line;
            const u32 opColumn = m_current.column;
            advance();
            Value right = parseNext();
            if(!m_errors.empty())
                return Value(m_arena);

            if(!validateBinaryOperation(op, left, right, opLine, opColumn))
                return Value(m_arena);

            left = applyOperation(op, left, right);
        }

        return left;
    }

    Value parseAdditive(){
        return parseBinaryExpression(
            [&](){ return parseMultiplicative(); },
            TokenType::Plus,
            TokenType::Minus,
            [](const TokenType::Enum op, const Value& left, const Value& right){
                return op == TokenType::Plus ? left + right : left - right;
            }
        );
    }

    Value parseMultiplicative(){
        return parseBinaryExpression(
            [&](){ return parseUnary(); },
            TokenType::Star,
            TokenType::Slash,
            [](const TokenType::Enum op, const Value& left, const Value& right){
                return op == TokenType::Star ? left * right : left / right;
            }
        );
    }

    Value parseUnary(){
        if(m_current.type == TokenType::Minus){
            advance();
            Value val = parseUnary();
            if(!m_errors.empty())
                return Value(m_arena);

            if(val.isInteger()){
                if(NegateOverflows<i64>(val.asInteger())){
                    error("integer overflow");
                    return Value(m_arena);
                }
                return Value(-val.asInteger(), m_arena);
            }
            if(val.isDouble())
                return Value(-val.asDouble(), m_arena);

            error("unary '-' requires numeric operand");
            return Value(m_arena);
        }

        return parsePrimary();
    }

    Value parsePrimary(){
        switch(m_current.type){
        case TokenType::IntegerLiteral:{
            const auto text = m_current.text;
            advance();

            i64 result = 0;
            if(!ParseI64FromChars(text.data(), text.data() + text.size(), result)){
                error("invalid integer literal");
                return Value(m_arena);
            }
            return Value(result, m_arena);
        }
        case TokenType::DoubleLiteral:{
            const auto text = m_current.text;
            advance();

            f64 result = 0.0;
            if(!ParseF64FromChars(text.data(), text.data() + text.size(), result)){
                error("invalid double literal");
                return Value(m_arena);
            }
            return Value(result, m_arena);
        }
        case TokenType::StringLiteral:{
            const auto text = m_current.text;
            advance();
            return Value(text, m_arena);
        }
        case TokenType::Identifier:{
            const auto name = m_current.text;
            advance();

            ScratchPath path{m_scratchArena};
            path.reserve(4);
            path.push_back(name);

            while(m_current.type == TokenType::Dot){
                advance();
                if(m_current.type != TokenType::Identifier){
                    errorExpected("expected identifier after '.'");
                    return Value(m_arena);
                }
                path.push_back(m_current.text);
                advance();
            }

            return makeReference(path);
        }
        case TokenType::LeftBracket:
            return parseListLiteral(TokenType::RightBracket);
        case TokenType::LeftBrace:
            return parseBraceExpression();
        case TokenType::LeftParen:{
            advance();
            Value val = parseExpression();
            if(!m_errors.empty())
                return Value(m_arena);
            if(!expect(TokenType::RightParen, "expected ')' after expression"))
                return Value(m_arena);
            return val;
        }
        default:
            break;
        }

        errorExpected("expected expression");
        return Value(m_arena);
    }


    Value parseListLiteral(TokenType::Enum closeToken){
        advance();

        Value list(m_arena);
        list.makeList();

        if(m_current.type == closeToken){
            advance();
            return list;
        }

        for(;;){
            Value elem = parseExpression();
            if(!m_errors.empty())
                return Value(m_arena);
            list.append(Move(elem));

            if(m_current.type == closeToken){
                advance();
                return list;
            }

            if(!expect(TokenType::Comma, "expected ',' or closing delimiter in list"))
                return Value(m_arena);
            if(m_current.type == closeToken){
                advance();
                return list;
            }
        }
    }

    Value parseBraceExpression(){
        advance();

        if(m_current.type == TokenType::RightBrace){
            advance();
            Value list(m_arena);
            list.makeList();
            return list;
        }

        Value first = parseExpression();
        if(!m_errors.empty())
            return Value(m_arena);

        if(m_current.type == TokenType::Colon)
            return parseMapLiteralContinuation(Move(first));

        Value list(m_arena);
        list.makeList();
        list.append(Move(first));

        if(m_current.type == TokenType::RightBrace){
            advance();
            return list;
        }

        if(!expect(TokenType::Comma, "expected ',' or '}' in list"))
            return Value(m_arena);
        if(m_current.type == TokenType::RightBrace){
            advance();
            return list;
        }

        while(m_current.type != TokenType::RightBrace){
            Value elem = parseExpression();
            if(!m_errors.empty())
                return Value(m_arena);
            list.append(Move(elem));

            if(m_current.type == TokenType::RightBrace)
                break;

            if(!expect(TokenType::Comma, "expected ',' or '}' in list"))
                return Value(m_arena);
            if(m_current.type == TokenType::RightBrace)
                break;
        }

        advance();
        return list;
    }

    Value parseMapLiteralContinuation(Value&& firstKey){
        advance();

        if(!firstKey.isString()){
            error("map keys must be strings");
            return Value(m_arena);
        }

        Value map(m_arena);
        map.makeMap();

        Value firstValue = parseExpression();
        if(!m_errors.empty())
            return Value(m_arena);

        map.field(firstKey.asString()) = Move(firstValue);

        if(m_current.type == TokenType::RightBrace){
            advance();
            return map;
        }

        if(!expect(TokenType::Comma, "expected ',' or '}' in map"))
            return Value(m_arena);
        if(m_current.type == TokenType::RightBrace){
            advance();
            return map;
        }

        while(m_current.type != TokenType::RightBrace){
            Value key = parseExpression();
            if(!m_errors.empty())
                return Value(m_arena);

            if(!key.isString()){
                error("map keys must be strings");
                return Value(m_arena);
            }

            if(!expect(TokenType::Colon, "expected ':' after map key"))
                return Value(m_arena);

            Value val = parseExpression();
            if(!m_errors.empty())
                return Value(m_arena);

            map.field(key.asString()) = Move(val);

            if(m_current.type == TokenType::RightBrace)
                break;

            if(!expect(TokenType::Comma, "expected ',' or '}' in map"))
                return Value(m_arena);
            if(m_current.type == TokenType::RightBrace)
                break;
        }

        advance();
        return map;
    }


    Value* resolveTarget(const ScratchPath& path, const bool allowCreateRoot, const u32 errorLine, const u32 errorColumn){
        NWB_ASSERT(!path.empty());

        const auto rootName = path[0];
        if(!isDeclaredVariable(rootName)){
            error(errorLine, errorColumn, "assignments must target a declared variable");
            return nullptr;
        }

        auto rootIt = m_variables.find(rootName);
        if(rootIt == m_variables.end()){
            if(!allowCreateRoot){
                error(errorLine, errorColumn, "undefined variable");
                return nullptr;
            }

            MString rootKey(rootName.data(), rootName.size(), m_arena);
            auto result = m_variables.emplace(Move(rootKey), Value(m_arena));
            rootIt = result.first;
        }

        Value* current = &rootIt.value();
        for(usize i = 1; i < path.size(); ++i){
            if(current->isNull())
                current->makeMap();
            else if(!current->isMap()){
                error(errorLine, errorColumn, "cannot access field on non-map value");
                return nullptr;
            }

            current = &current->field(path[i]);
        }

        return current;
    }

    Value resolveRead(const ScratchPath& path){
        NWB_ASSERT(!path.empty());

        if(!isDeclaredVariable(path[0])){
            error("references must target a declared variable");
            return Value(m_arena);
        }

        auto rootIt = m_variables.find(path[0]);
        if(rootIt == m_variables.end()){
            error("undefined variable");
            return Value(m_arena);
        }

        const Value* current = &rootIt.value();
        for(usize i = 1; i < path.size(); ++i){
            if(!current->isMap()){
                error("cannot access field on non-map value");
                return Value(m_arena);
            }
            current = current->findField(path[i]);
            if(!current){
                error("undefined field");
                return Value(m_arena);
            }
        }

        return *current;
    }

    Value makeReference(const ScratchPath& path){
        NWB_ASSERT(!path.empty());

        if(!isDeclaredVariable(path[0])){
            error("references must target a declared variable");
            return Value(m_arena);
        }

        MString text(m_arena);
        for(usize i = 0u; i < path.size(); ++i){
            if(i != 0u)
                text.push_back('.');
            text.append(path[i].data(), path[i].size());
        }

        return Value::Reference(MStringView(text.data(), text.size()), m_arena);
    }

    [[nodiscard]] bool isNameInList(const ScratchNameList& names, MStringView name)const{
        for(const MStringView currentName : names){
            if(currentName == name)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool isDeclaredStruct(MStringView name)const{
        return isNameInList(m_declaredStructs, name);
    }

    [[nodiscard]] bool isDeclaredVariable(MStringView name)const{
        for(const Document::Declaration& declaration : m_declarations){
            if(MStringView(declaration.variable.data(), declaration.variable.size()) == name)
                return true;
        }
        return false;
    }

    [[nodiscard]] Value* declaredAssetRoot(const u32 line, const u32 column){
        auto rootIt = m_variables.find(m_declaredAssetVariable);
        if(rootIt == m_variables.end()){
            error(line, column, "missing declared asset variable");
            return nullptr;
        }
        return &rootIt.value();
    }

    bool ensureMapValue(Value& value, const u32 line, const u32 column, MStringView message){
        if(value.isNull())
            value.makeMap();
        else if(!value.isMap()){
            error(line, column, message);
            return false;
        }
        return true;
    }

    bool ensureListValue(Value& value, const u32 line, const u32 column, MStringView message){
        if(value.isNull())
            value.makeList();
        else if(!value.isList()){
            error(line, column, message);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool containsBindInstanceName(const Value& instances, MStringView instanceName)const{
        NWB_ASSERT(instances.isList());

        for(const Value& instance : instances.asList()){
            if(!instance.isMap())
                continue;

            const Value* name = instance.findField(LiteralView("name"));
            if(name && name->isString() && name->asString() == instanceName)
                return true;
        }

        return false;
    }

    bool addBindStruct(MStringView structName, Value&& attributes, Value&& fields, const u32 line, const u32 column){
        Value* assetRoot = declaredAssetRoot(line, column);
        if(!assetRoot)
            return false;
        if(!ensureMapValue(*assetRoot, line, column, "bind declarations require asset root to be a map"))
            return false;

        Value& structs = assetRoot->field(LiteralView("structs"));
        if(!ensureMapValue(structs, line, column, "asset.structs must be a map"))
            return false;
        if(structs.findField(structName)){
            error(line, column, "duplicate struct declaration");
            return false;
        }

        Value& outStruct = structs.field(structName);
        outStruct.makeMap();
        outStruct.field(LiteralView("attributes")) = Move(attributes);
        outStruct.field(LiteralView("fields")) = Move(fields);
        return true;
    }

    bool addBindInstance(MStringView typeName, MStringView instanceName, const u32 line, const u32 column){
        Value* assetRoot = declaredAssetRoot(line, column);
        if(!assetRoot)
            return false;
        if(!ensureMapValue(*assetRoot, line, column, "bind declarations require asset root to be a map"))
            return false;

        Value& instances = assetRoot->field(LiteralView("instances"));
        if(!ensureListValue(instances, line, column, "asset.instances must be a list"))
            return false;
        if(containsBindInstanceName(instances, instanceName)){
            error(line, column, "duplicate struct instance declaration");
            return false;
        }

        Value instance(m_arena);
        instance.makeMap();
        instance.field(LiteralView("type")).setString(typeName);
        instance.field(LiteralView("name")).setString(instanceName);
        instances.append(Move(instance));
        return true;
    }

    [[nodiscard]] bool isZero(const Value& value)const{
        if(value.isInteger())
            return value.asInteger() == 0;
        if(value.isDouble())
            return value.asDouble() == 0.0;
        return false;
    }

    bool validateNumericOperands(
        const TokenType::Enum op,
        const Value& lhs,
        const Value& rhs,
        const u32 line,
        const u32 column,
        const char* operandError,
        const bool rejectZeroDivisor = false){
        if(!(lhs.isNumeric() && rhs.isNumeric())){
            if(operandError)
                error(line, column, operandError);
            return false;
        }
        if(rejectZeroDivisor && isZero(rhs)){
            error(line, column, "division by zero");
            return false;
        }
        if(lhs.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, lhs.asInteger(), rhs.asInteger())){
            error(line, column, "integer overflow");
            return false;
        }
        return true;
    }

    bool validateBinaryOperation(const TokenType::Enum op, const Value& lhs, const Value& rhs, const u32 line, const u32 column){
        switch(op){
        case TokenType::Plus:
            if(lhs.isNumeric() && rhs.isNumeric())
                return validateNumericOperands(op, lhs, rhs, line, column, nullptr);
            if(lhs.isString() && rhs.isString()){
                if(AddOverflows<usize>(lhs.asString().size(), rhs.asString().size())){
                    error(line, column, "string concatenation size overflow");
                    return false;
                }
                return true;
            }
            if(lhs.isList() && rhs.isList()){
                if(AddOverflows<usize>(lhs.asList().size(), rhs.asList().size())){
                    error(line, column, "list concatenation size overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '+' requires numeric, string, or list operands of matching types");
            return false;
        case TokenType::Minus:
            return validateNumericOperands(op, lhs, rhs, line, column, "operator '-' requires numeric operands");
        case TokenType::Star:
            return validateNumericOperands(op, lhs, rhs, line, column, "operator '*' requires numeric operands");
        case TokenType::Slash:
            return validateNumericOperands(op, lhs, rhs, line, column, "operator '/' requires numeric operands", true);
        default:
            break;
        }

        error(line, column, "unsupported binary operator");
        return false;
    }

    bool validateAssignment(const TokenType::Enum op, const Value& target, const Value& rhs, const u32 line, const u32 column){
        switch(op){
        case TokenType::Equal:
            return true;
        case TokenType::PlusEqual:
            if(target.isNumeric() && rhs.isNumeric())
                return validateNumericOperands(op, target, rhs, line, column, nullptr);
            if(target.isString() && rhs.isString()){
                if(AddOverflows<usize>(target.asString().size(), rhs.asString().size())){
                    error(line, column, "string append size overflow");
                    return false;
                }
                return true;
            }
            if(target.isList()){
                const usize addedElements = rhs.isList() ? rhs.asList().size() : 1u;
                if(AddOverflows<usize>(target.asList().size(), addedElements)){
                    error(line, column, "list append size overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '+=' requires numeric operands, string += string, or a list target");
            return false;
        case TokenType::MinusEqual:
            return validateNumericOperands(op, target, rhs, line, column, "operator '-=' requires numeric operands");
        case TokenType::StarEqual:
            return validateNumericOperands(op, target, rhs, line, column, "operator '*=' requires numeric operands");
        case TokenType::SlashEqual:
            return validateNumericOperands(op, target, rhs, line, column, "operator '/=' requires numeric operands", true);
        default:
            break;
        }

        error(line, column, "unsupported assignment operator");
        return false;
    }


    void advance(){
        m_previous = m_current;
        m_current = m_lexer.next();

        if(m_current.type == TokenType::Error)
            error(m_current.text);
    }

    bool expect(TokenType::Enum type, MStringView expected){
        if(m_current.type == type){
            advance();
            return true;
        }
        errorExpected(expected);
        return false;
    }

    void error(MStringView message){
        error(m_current.line, m_current.column, message);
    }

    void error(u32 line, u32 column, MStringView message){
        m_errors.push_back(ParseError{line, column, MString(message.data(), message.size(), m_arena)});
    }

    void errorExpected(MStringView expected){
        const auto desc = tokenDescription();
        ScratchString msg{m_scratchArena};
        if(
            expected.size() <= Limit<usize>::s_Max - 8u
            && desc.size() <= Limit<usize>::s_Max - expected.size() - 8u
        )
            msg.reserve(expected.size() + 8u + desc.size());
        msg.append(expected.data(), expected.size());
        msg.append(", found ", 8);
        msg.append(desc.data(), desc.size());
        error(m_current.line, m_current.column, MStringView(msg.data(), msg.size()));
    }

    [[nodiscard]] ScratchString tokenDescription(){
        switch(m_current.type){
        case TokenType::Identifier:{
            ScratchString d("identifier '", m_scratchArena);
            d.append(m_current.text.data(), m_current.text.size());
            d.push_back('\'');
            return d;
        }
        case TokenType::IntegerLiteral:
        case TokenType::DoubleLiteral:{
            ScratchString d("number '", m_scratchArena);
            d.append(m_current.text.data(), m_current.text.size());
            d.push_back('\'');
            return d;
        }
        case TokenType::StringLiteral: return ScratchString("string literal", m_scratchArena);
        case TokenType::Plus: return ScratchString("'+'", m_scratchArena);
        case TokenType::Minus: return ScratchString("'-'", m_scratchArena);
        case TokenType::Star: return ScratchString("'*'", m_scratchArena);
        case TokenType::Slash: return ScratchString("'/'", m_scratchArena);
        case TokenType::PlusEqual: return ScratchString("'+='", m_scratchArena);
        case TokenType::MinusEqual: return ScratchString("'-='", m_scratchArena);
        case TokenType::StarEqual: return ScratchString("'*='", m_scratchArena);
        case TokenType::SlashEqual: return ScratchString("'/='", m_scratchArena);
        case TokenType::Equal: return ScratchString("'='", m_scratchArena);
        case TokenType::Semicolon: return ScratchString("';'", m_scratchArena);
        case TokenType::Dot: return ScratchString("'.'", m_scratchArena);
        case TokenType::Comma: return ScratchString("','", m_scratchArena);
        case TokenType::Colon: return ScratchString("':'", m_scratchArena);
        case TokenType::LeftBracket: return ScratchString("'['", m_scratchArena);
        case TokenType::RightBracket: return ScratchString("']'", m_scratchArena);
        case TokenType::LeftBrace: return ScratchString("'{'", m_scratchArena);
        case TokenType::RightBrace: return ScratchString("'}'", m_scratchArena);
        case TokenType::LeftParen: return ScratchString("'('", m_scratchArena);
        case TokenType::RightParen: return ScratchString("')'", m_scratchArena);
        case TokenType::EndOfFile: return ScratchString("end of file", m_scratchArena);
        case TokenType::Error: return ScratchString("error", m_scratchArena);
        default: return ScratchString("unknown token", m_scratchArena);
        }
    }


private:
    Lexer m_lexer;
    MetaArena& m_arena;
    Alloc::ScratchArena m_scratchArena;
    ScratchNameList m_declaredStructs;
    MVector<ParseError>& m_errors;
    MStringMap<Value>& m_variables;
    Document::DeclarationList& m_declarations;
    MStringView m_declaredAssetVariable;

    Token m_current;
    Token m_previous;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Document::Document(MetaArena& arena)
    : m_arena(arena)
    , m_assetType(arena)
    , m_assetVariable(arena)
    , m_variables(0, MStringHash(), MStringEqual(), arena)
    , m_declarations(arena)
    , m_errors(arena)
{}


bool Document::parse(MStringView source){
    m_errors.clear();
    m_assetType.clear();
    m_assetVariable.clear();
    m_variables.clear();
    m_declarations.clear();

    __hidden_metascript_parser::Parser parser(source, m_arena, m_errors, m_variables, m_declarations);
    return parser.parseInto(m_assetType, m_assetVariable);
}

bool Document::parseWithImplicitAsset(MStringView source, MStringView assetType, MStringView assetVariable){
    m_errors.clear();
    m_assetType.clear();
    m_assetVariable.clear();
    m_variables.clear();
    m_declarations.clear();

    __hidden_metascript_parser::Parser parser(source, m_arena, m_errors, m_variables, m_declarations);
    return parser.parseWithImplicitAsset(m_assetType, m_assetVariable, assetType, assetVariable);
}

bool Document::parse(IMetaReader& reader){
    m_errors.clear();
    m_assetType.clear();
    m_assetVariable.clear();
    m_variables.clear();
    m_declarations.clear();

    try{
        constexpr usize chunkSize = 4096;

        Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_DocumentReaderScratch, chunkSize);
        BasicString<MChar, Alloc::ScratchArena> buffer{scratchArena};
        buffer.reserve(chunkSize);
        MChar chunk[chunkSize];

        for(;;){
            const isize bytesRead = reader.read(chunk, chunkSize);
            if(bytesRead < 0){
                m_errors.push_back(ParseError{0, 0, MString("read error", m_arena)});
                return false;
            }
            if(bytesRead == 0)
                break;
            if(static_cast<usize>(bytesRead) > chunkSize){
                m_errors.push_back(ParseError{0, 0, MString("reader returned more bytes than requested", m_arena)});
                return false;
            }
            buffer.append(chunk, static_cast<usize>(bytesRead));
        }

        return parse(MStringView(buffer.data(), buffer.size()));
    }
    catch(const GeneralException& e){
        m_errors.push_back(ParseError{0, 0, MString(e.what(), m_arena)});
        return false;
    }
}

const Value& Document::asset()const{
    const MStringView key(m_assetVariable.data(), m_assetVariable.size());
    auto it = m_variables.find(key);
    NWB_ASSERT(it != m_variables.end());
    return it.value();
}

Value& Document::asset(){
    const MStringView key(m_assetVariable.data(), m_assetVariable.size());
    auto it = m_variables.find(key);
    NWB_ASSERT(it != m_variables.end());
    return it.value();
}

const Value* Document::findVariable(MStringView name)const{
    auto it = m_variables.find(name);
    if(it == m_variables.end())
        return nullptr;
    return &it.value();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

