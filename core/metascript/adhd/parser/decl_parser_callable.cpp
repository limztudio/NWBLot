// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decl_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclParser::validateMemberOperatorArity(
    const MStringView opName,
    const usize paramCount,
    MStringView& outErrorMsg
)noexcept{
    if((opName == "*" || opName == "&") && paramCount == 0u){
        outErrorMsg = "unary 'operator*' (dereference) and unary 'operator&' (address-of) overloads are prohibited in adhd";
        return false;
    }
    if(opName == "~"){
        if(paramCount != 0u){
            outErrorMsg = "member 'operator~' cannot take parameters";
            return false;
        }
        return true;
    }
    if(opName == "+" || opName == "-"){
        if(paramCount > 1u){
            outErrorMsg = "member 'operator+' and 'operator-' may take at most 1 parameter";
            return false;
        }
        return true;
    }
    if(paramCount != 1u){
        outErrorMsg = "binary or indexing member operator overload must take exactly 1 parameter";
        return false;
    }
    return true;
}

b8 DeclParser::parseCallableOwnerOrTemplateHeader(
    MVector<MStringView>& outOwnerTemplateParams,
    MVector<TemplateParamDecl>& outTemplateParams,
    const b8 failOnInvalidTemplateParams
){
    if(!m_cursor.check(TokenType::Less))
        return true;

    const ParserCursor::Checkpoint cp = m_cursor.save();
    MVector<TypeArg> ownerTypeArgs(m_cursor.arena());
    if(m_typeParser.parseTypeArgList(ownerTypeArgs) && !m_typeParser.hasPendingRightAngles() && m_cursor.check(TokenType::ColonColon)){
        for(const TypeArg& arg : ownerTypeArgs){
            if(arg.type && arg.type->refMode == ParamPassingMode::None && arg.type->templateArgs.empty() && !arg.type->isFuncType)
                outOwnerTemplateParams.push_back(arg.type->name);
            else
                outOwnerTemplateParams.push_back(arg.constantText);
        }
        return true;
    }

    m_cursor.restore(cp);
    m_typeParser.resetPendingRightAngles();
    if(!m_typeParser.parseTemplateParamList(outTemplateParams))
        return !failOnInvalidTemplateParams;

    for(const TemplateParamDecl& tp : outTemplateParams)
        outOwnerTemplateParams.push_back(tp.name);
    return true;
}

MethodDecl* DeclParser::parseFreeOrOutOfLineCallable(
    const SourceSpan span,
    const MVector<ParsedAttribute>& attributes
){
    MethodDecl* decl = AllocateAstNode<MethodDecl>(m_cursor.arena());
    decl->span = span;

    if(!applyCallableAttributes(attributes, *decl))
        return nullptr;

    if(m_cursor.check(TokenType::Identifier)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        const MStringView maybeClass = m_cursor.current().text;
        m_cursor.advance();

        MVector<MStringView> parsedOwnerTemplateParams(m_cursor.arena());
        MVector<TemplateParamDecl> ownerTemplateParams(m_cursor.arena());
        (void)parseCallableOwnerOrTemplateHeader(parsedOwnerTemplateParams, ownerTemplateParams, false);

        if(m_cursor.match(TokenType::ColonColon)){
            if(m_cursor.match(TokenType::Tilde)){
                if(!m_cursor.checkIdentifier(maybeClass)){
                    m_cursor.errorExpected("expected class name after '~' in out-of-line destructor");
                    return nullptr;
                }
                m_cursor.advance();
                decl->ownerClassName = maybeClass;
                decl->ownerTemplateParams = Move(parsedOwnerTemplateParams);
                decl->name = maybeClass;
                decl->isDestructor = true;
                if(!m_cursor.expect(TokenType::LeftParen, "expected '(' in out-of-line destructor"))
                    return nullptr;
                if(!m_cursor.expect(TokenType::RightParen, "destructor cannot take parameters"))
                    return nullptr;
                decl->hasBody = true;
                if(!m_stmtParser.parseBlockStatements(decl->body))
                    return nullptr;
                return decl;
            }

            if(m_cursor.checkIdentifier(maybeClass)){
                m_cursor.advance();
                decl->ownerClassName = maybeClass;
                decl->ownerTemplateParams = Move(parsedOwnerTemplateParams);
                decl->name = maybeClass;
                decl->isConstructor = true;
                if(!m_typeParser.parseParamList(decl->params))
                    return nullptr;
                if(m_cursor.check(TokenType::Colon)){
                    m_cursor.error("C++ constructor initializer lists are prohibited in adhd; initialize 'this.member' inside the constructor body");
                    return nullptr;
                }
                decl->hasBody = true;
                if(!m_stmtParser.parseBlockStatements(decl->body))
                    return nullptr;
                return decl;
            }
        }
        m_cursor.restore(cp);
        m_typeParser.resetPendingRightAngles();
    }

    decl->returnType = m_typeParser.parseType(true, false);
    if(!decl->returnType)
        return nullptr;

    if(m_cursor.checkIdentifier("operator")){
        m_cursor.error("free operator overloads are prohibited in adhd; overload operators as instance member methods on a class");
        return nullptr;
    }

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected function or class name");
        return nullptr;
    }

    const MStringView firstName = m_cursor.current().text;
    m_cursor.advance();

    MVector<MStringView> parsedOwnerTemplateParams(m_cursor.arena());
    if(!parseCallableOwnerOrTemplateHeader(parsedOwnerTemplateParams, decl->templateParams, true))
        return nullptr;

    if(m_cursor.match(TokenType::ColonColon)){
        decl->templateParams.clear();
        decl->ownerClassName = firstName;
        decl->ownerTemplateParams = Move(parsedOwnerTemplateParams);
        if(m_cursor.matchIdentifier("operator")){
            MStringView opName;
            if(!parseOperatorName(opName))
                return nullptr;
            decl->name = opName;
            decl->isOperator = true;
        }
        else if(m_cursor.check(TokenType::Identifier)){
            decl->name = m_cursor.current().text;
            m_cursor.advance();
            if(m_cursor.check(TokenType::Less)){
                if(!m_typeParser.parseTemplateParamList(decl->templateParams))
                    return nullptr;
            }
        }
        else{
            m_cursor.errorExpected("expected method name after '::'");
            return nullptr;
        }
    }
    else{
        decl->name = firstName;
    }

    if(!m_cursor.check(TokenType::LeftParen)){
        m_cursor.error(span.line, span.column, "mutable global variables are prohibited in adhd; use 'const' for compile-time constants");
        return nullptr;
    }

    if(!m_typeParser.parseParamList(decl->params))
        return nullptr;
    if(decl->isOperator){
        MStringView opErr;
        if(!validateMemberOperatorArity(decl->name, decl->params.size(), opErr)){
            m_cursor.error(span.line, span.column, opErr);
            return nullptr;
        }
    }

    if(decl->ownerClassName.empty()){
        if(m_cursor.checkIdentifier("const") || m_cursor.checkIdentifier("override")){
            m_cursor.error(span.line, span.column, "'const' and 'override' qualifiers are only permitted on class methods, not free functions");
            return nullptr;
        }
    }
    else{
        parseClassMethodQualifiers(*decl);
    }

    if(m_cursor.check(TokenType::LeftBrace)){
        decl->hasBody = true;
        if(!m_stmtParser.parseBlockStatements(decl->body))
            return nullptr;
    }
    else if(!decl->ownerClassName.empty()){
        m_cursor.error(span.line, span.column, "out-of-line method definition must have a '{ ... }' body");
        return nullptr;
    }
    else if(!m_cursor.expect(TokenType::Semicolon, "expected '{' or ';' after function declaration"))
        return nullptr;

    return decl;
}

b8 DeclParser::applyCallableAttributes(
    const MVector<ParsedAttribute>& attributes,
    MethodDecl& decl
){
    for(const ParsedAttribute& attr : attributes){
        if(attr.name == "reflect" && attr.arguments.empty())
            decl.isReflect = true;
        else{
            m_cursor.error(attr.line, attr.column, "unsupported attribute on callable; expected 'reflect'");
            return false;
        }
    }
    return true;
}

b8 DeclParser::parseOperatorName(MStringView& outOpName){
    if(m_cursor.check(TokenType::AmpersandAmpersand) || m_cursor.check(TokenType::PipePipe)){
        m_cursor.error("overloading 'operator&&' and 'operator||' is prohibited in adhd");
        return false;
    }

    if(m_cursor.match(TokenType::LeftBracket)){
        if(!m_cursor.expect(TokenType::RightBracket, "expected ']' after '[' in 'operator[]'"))
            return false;
        outOpName = "[]";
        return true;
    }

    const Token tok = m_cursor.current();
    switch(tok.type){
    case TokenType::Plus:
    case TokenType::Minus:
    case TokenType::Star:
    case TokenType::Slash:
    case TokenType::Percent:
    case TokenType::PlusEqual:
    case TokenType::MinusEqual:
    case TokenType::StarEqual:
    case TokenType::SlashEqual:
    case TokenType::PercentEqual:
    case TokenType::Ampersand:
    case TokenType::Pipe:
    case TokenType::Caret:
    case TokenType::Tilde:
    case TokenType::LessLess:
    case TokenType::GreaterGreater:
    case TokenType::AmpersandEqual:
    case TokenType::PipeEqual:
    case TokenType::CaretEqual:
    case TokenType::LessLessEqual:
    case TokenType::GreaterGreaterEqual:
    case TokenType::EqualEqual:
    case TokenType::BangEqual:
    case TokenType::Less:
    case TokenType::LessEqual:
    case TokenType::Greater:
    case TokenType::GreaterEqual:
        outOpName = tok.text;
        m_cursor.advance();
        return true;
    default:
        m_cursor.error("unsupported operator overload in adhd");
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
