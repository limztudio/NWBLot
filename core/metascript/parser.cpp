// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parser.h"
#include "integer_overflow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace __hidden_metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BinaryI64Overflows(const TokenType::Enum op, const i64 lhs, const i64 rhs){
    switch(op){
    case TokenType::Plus:
    case TokenType::PlusEqual:
        return AddI64Overflows(lhs, rhs);
    case TokenType::Minus:
    case TokenType::MinusEqual:
        return SubtractI64Overflows(lhs, rhs);
    case TokenType::Star:
    case TokenType::StarEqual:
        return MultiplyI64Overflows(lhs, rhs);
    case TokenType::Slash:
    case TokenType::SlashEqual:
        return DivideI64Overflows(lhs, rhs);
    default:
        break;
    }

    return false;
}

[[nodiscard]] bool AddUsizeOverflows(const usize lhs, const usize rhs){
    return lhs > Limit<usize>::s_Max - rhs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Parser{
public:
    Parser(MStringView source, Alloc::CustomArena& arena, MVector<ParseError>& errors, MStringMap<Value>& variables)
        : m_lexer(source)
        , m_arena(arena)
        , m_errors(errors)
        , m_variables(variables)
    {
        advance();
    }


public:
    bool parseInto(MString& outAssetType, MString& outAssetVariable){
        try{
            if(!parseDeclaration(outAssetType, outAssetVariable))
                return false;
            m_declaredAssetVariable = MStringView(outAssetVariable.data(), outAssetVariable.size());

            while(m_current.type != TokenType::EndOfFile){
                if(!parseStatement())
                    return false;
            }

            return m_errors.empty();
        }
        catch(const GeneralException& e){
            error(m_current.line, m_current.column,
                MStringView(e.what(), MString::traits_type::length(e.what())));
            return false;
        }
    }


private:
    bool parseDeclaration(MString& outAssetType, MString& outAssetVariable){
        if(m_current.type != TokenType::Identifier){
            errorExpected("expected asset type name");
            return false;
        }
        outAssetType.assign(m_current.text.data(), m_current.text.size());
        advance();

        if(m_current.type != TokenType::Identifier){
            errorExpected("expected variable name after type name");
            return false;
        }
        outAssetVariable.assign(m_current.text.data(), m_current.text.size());
        advance();

        if(!expect(TokenType::Semicolon, "expected ';' after declaration"))
            return false;

        MString key(outAssetVariable.data(), outAssetVariable.size(), MAllocator<MChar>(m_arena));
        m_variables.emplace(Move(key), Value(m_arena));
        return true;
    }

    bool parseStatement(){
        if(m_current.type != TokenType::Identifier){
            errorExpected("expected identifier at start of statement");
            return false;
        }

        const MStringView firstName = m_current.text;
        const u32 firstLine = m_current.line;
        const u32 firstColumn = m_current.column;
        advance();

        if(m_current.type == TokenType::Identifier){
            error(firstLine, firstColumn, "re-declaration is not allowed; only one asset declaration permitted");
            return false;
        }

        MVector<MStringView> path{MAllocator<MStringView>(m_arena)};
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
        if(assignOp != TokenType::Equal
            && assignOp != TokenType::PlusEqual
            && assignOp != TokenType::MinusEqual
            && assignOp != TokenType::StarEqual
            && assignOp != TokenType::SlashEqual)
        {
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


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
                if(NegateI64Overflows(val.asInteger())){
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

            MVector<MStringView> path{MAllocator<MStringView>(m_arena)};
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

            return resolveRead(path);
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


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


    Value* resolveTarget(const MVector<MStringView>& path, const bool allowCreateRoot, const u32 errorLine, const u32 errorColumn){
        NWB_ASSERT(!path.empty());

        const auto rootName = path[0];
        if(rootName != m_declaredAssetVariable){
            error(errorLine, errorColumn, "assignments must target the declared asset variable");
            return nullptr;
        }

        auto rootIt = m_variables.find(rootName);
        if(rootIt == m_variables.end()){
            if(!allowCreateRoot){
                error(errorLine, errorColumn, "undefined variable");
                return nullptr;
            }

            MString rootKey(rootName.data(), rootName.size(), MAllocator<MChar>(m_arena));
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

    Value resolveRead(const MVector<MStringView>& path){
        NWB_ASSERT(!path.empty());

        if(path[0] != m_declaredAssetVariable){
            error("references must target the declared asset variable");
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

    [[nodiscard]] bool isZero(const Value& value)const{
        if(value.isInteger())
            return value.asInteger() == 0;
        if(value.isDouble())
            return value.asDouble() == 0.0;
        return false;
    }

    bool validateBinaryOperation(const TokenType::Enum op, const Value& lhs, const Value& rhs, const u32 line, const u32 column){
        switch(op){
        case TokenType::Plus:
            if(lhs.isNumeric() && rhs.isNumeric()){
                if(lhs.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, lhs.asInteger(), rhs.asInteger())){
                    error(line, column, "integer overflow");
                    return false;
                }
                return true;
            }
            if(lhs.isString() && rhs.isString()){
                if(AddUsizeOverflows(lhs.asString().size(), rhs.asString().size())){
                    error(line, column, "string concatenation size overflow");
                    return false;
                }
                return true;
            }
            if(lhs.isList() && rhs.isList()){
                if(AddUsizeOverflows(lhs.asList().size(), rhs.asList().size())){
                    error(line, column, "list concatenation size overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '+' requires numeric, string, or list operands of matching types");
            return false;
        case TokenType::Minus:
            if(lhs.isNumeric() && rhs.isNumeric()){
                if(lhs.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, lhs.asInteger(), rhs.asInteger())){
                    error(line, column, "integer overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '-' requires numeric operands");
            return false;
        case TokenType::Star:
            if(lhs.isNumeric() && rhs.isNumeric()){
                if(lhs.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, lhs.asInteger(), rhs.asInteger())){
                    error(line, column, "integer overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '*' requires numeric operands");
            return false;
        case TokenType::Slash:
            if(!(lhs.isNumeric() && rhs.isNumeric())){
                error(line, column, "operator '/' requires numeric operands");
                return false;
            }
            if(isZero(rhs)){
                error(line, column, "division by zero");
                return false;
            }
            if(lhs.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, lhs.asInteger(), rhs.asInteger())){
                error(line, column, "integer overflow");
                return false;
            }
            return true;
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
            if(target.isNumeric() && rhs.isNumeric()){
                if(target.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, target.asInteger(), rhs.asInteger())){
                    error(line, column, "integer overflow");
                    return false;
                }
                return true;
            }
            if(target.isString() && rhs.isString()){
                if(AddUsizeOverflows(target.asString().size(), rhs.asString().size())){
                    error(line, column, "string append size overflow");
                    return false;
                }
                return true;
            }
            if(target.isList()){
                const usize addedElements = rhs.isList() ? rhs.asList().size() : 1u;
                if(AddUsizeOverflows(target.asList().size(), addedElements)){
                    error(line, column, "list append size overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '+=' requires numeric operands, string += string, or a list target");
            return false;
        case TokenType::MinusEqual:
            if(target.isNumeric() && rhs.isNumeric()){
                if(target.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, target.asInteger(), rhs.asInteger())){
                    error(line, column, "integer overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '-=' requires numeric operands");
            return false;
        case TokenType::StarEqual:
            if(target.isNumeric() && rhs.isNumeric()){
                if(target.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, target.asInteger(), rhs.asInteger())){
                    error(line, column, "integer overflow");
                    return false;
                }
                return true;
            }
            error(line, column, "operator '*=' requires numeric operands");
            return false;
        case TokenType::SlashEqual:
            if(!(target.isNumeric() && rhs.isNumeric())){
                error(line, column, "operator '/=' requires numeric operands");
                return false;
            }
            if(isZero(rhs)){
                error(line, column, "division by zero");
                return false;
            }
            if(target.isInteger() && rhs.isInteger() && BinaryI64Overflows(op, target.asInteger(), rhs.asInteger())){
                error(line, column, "integer overflow");
                return false;
            }
            return true;
        default:
            break;
        }

        error(line, column, "unsupported assignment operator");
        return false;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        m_errors.push_back(ParseError{line, column, MString(message.data(), message.size(), MAllocator<MChar>(m_arena))});
    }

    void errorExpected(MStringView expected){
        const auto desc = tokenDescription();
        MString msg{MAllocator<MChar>(m_arena)};
        msg.append(expected.data(), expected.size());
        msg.append(", found ", 8);
        msg.append(desc.data(), desc.size());
        error(m_current.line, m_current.column, MStringView(msg.data(), msg.size()));
    }

    [[nodiscard]] AString tokenDescription()const{
        switch(m_current.type){
        case TokenType::Identifier:{
            AString d = "identifier '";
            d.append(m_current.text.data(), m_current.text.size());
            d.push_back('\'');
            return d;
        }
        case TokenType::IntegerLiteral:
        case TokenType::DoubleLiteral:{
            AString d = "number '";
            d.append(m_current.text.data(), m_current.text.size());
            d.push_back('\'');
            return d;
        }
        case TokenType::StringLiteral: return AString("string literal");
        case TokenType::Plus: return AString("'+'");
        case TokenType::Minus: return AString("'-'");
        case TokenType::Star: return AString("'*'");
        case TokenType::Slash: return AString("'/'");
        case TokenType::PlusEqual: return AString("'+='");
        case TokenType::MinusEqual: return AString("'-='");
        case TokenType::StarEqual: return AString("'*='");
        case TokenType::SlashEqual: return AString("'/='");
        case TokenType::Equal: return AString("'='");
        case TokenType::Semicolon: return AString("';'");
        case TokenType::Dot: return AString("'.'");
        case TokenType::Comma: return AString("','");
        case TokenType::Colon: return AString("':'");
        case TokenType::LeftBracket: return AString("'['");
        case TokenType::RightBracket: return AString("']'");
        case TokenType::LeftBrace: return AString("'{'");
        case TokenType::RightBrace: return AString("'}'");
        case TokenType::LeftParen: return AString("'('");
        case TokenType::RightParen: return AString("')'");
        case TokenType::EndOfFile: return AString("end of file");
        case TokenType::Error: return AString("error");
        default: return AString("unknown token");
        }
    }


private:
    Lexer m_lexer;
    Alloc::CustomArena& m_arena;
    MVector<ParseError>& m_errors;
    MStringMap<Value>& m_variables;
    MStringView m_declaredAssetVariable;

    Token m_current;
    Token m_previous;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Document::Document(Alloc::CustomArena& arena)
    : m_arena(arena)
    , m_assetType(MAllocator<MChar>(arena))
    , m_assetVariable(MAllocator<MChar>(arena))
    , m_variables(0, MStringHash(), MStringEqual(), MAllocator<Pair<MString, Value>>(arena))
    , m_errors(MAllocator<ParseError>(arena))
{}


bool Document::parse(MStringView source){
    m_errors.clear();
    m_assetType.clear();
    m_assetVariable.clear();
    m_variables.clear();

    __hidden_metascript_parser::Parser parser(source, m_arena, m_errors, m_variables);
    return parser.parseInto(m_assetType, m_assetVariable);
}

bool Document::parse(IMetaReader& reader){
    m_errors.clear();
    m_assetType.clear();
    m_assetVariable.clear();
    m_variables.clear();

    try{
        constexpr usize chunkSize = 4096;

        MString buffer{MAllocator<MChar>(m_arena)};
        MChar chunk[chunkSize];

        for(;;){
            const isize bytesRead = reader.read(chunk, chunkSize);
            if(bytesRead < 0){
                m_errors.push_back(ParseError{0, 0, MString("read error", MAllocator<MChar>(m_arena))});
                return false;
            }
            if(bytesRead == 0)
                break;
            if(static_cast<usize>(bytesRead) > chunkSize){
                m_errors.push_back(ParseError{0, 0, MString("reader returned more bytes than requested", MAllocator<MChar>(m_arena))});
                return false;
            }
            buffer.append(chunk, static_cast<usize>(bytesRead));
        }

        return parse(MStringView(buffer.data(), buffer.size()));
    }
    catch(const GeneralException& e){
        m_errors.push_back(ParseError{0, 0, MString(e.what(), MAllocator<MChar>(m_arena))});
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
