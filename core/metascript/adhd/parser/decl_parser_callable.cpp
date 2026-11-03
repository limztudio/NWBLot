// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decl_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclParser::parseCallableOwnerOrTemplateHeader(const b8 failOnInvalidTemplateParams, MVector<MStringView>& outOwnerTemplateParams, MVector<TemplateParamDecl>& outOwnerTemplateParamDecls, MVector<TemplateParamDecl>& outTemplateParams){
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
    MVector<TemplateParamDecl> parsedTemplateParams(m_cursor.arena());
    if(!m_typeParser.parseTemplateParamList(parsedTemplateParams))
        return !failOnInvalidTemplateParams;

    if(m_cursor.check(TokenType::ColonColon)){
        for(const TemplateParamDecl& tp : parsedTemplateParams)
            outOwnerTemplateParams.push_back(tp.name);
        outOwnerTemplateParamDecls = Move(parsedTemplateParams);
    }
    else{
        outTemplateParams = Move(parsedTemplateParams);
    }
    return true;
}

MethodDecl* DeclParser::parseFreeOrOutOfLineCallable(const SourceSpan span, const Span<const ParsedAttribute> attributes){
    MethodDecl* decl = AllocateAstNode<MethodDecl>(m_cursor.arena());
    decl->span = span;

    if(!applyCallableAttributes(attributes, *decl))
        return nullptr;

    if(m_cursor.check(TokenType::Identifier)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        const MStringView maybeClass = m_cursor.current().text;
        m_cursor.advance();

        MVector<MStringView> parsedOwnerTemplateParams(m_cursor.arena());
        MVector<TemplateParamDecl> parsedOwnerTemplateParamDecls(m_cursor.arena());
        MVector<TemplateParamDecl> ignoredLookaheadTemplateParams(m_cursor.arena());
        if(parseCallableOwnerOrTemplateHeader(false, parsedOwnerTemplateParams, parsedOwnerTemplateParamDecls, ignoredLookaheadTemplateParams) && m_cursor.match(TokenType::ColonColon)){
            if(decl->isReflect){
                m_cursor.error(span.line, span.column, "'reflect' attribute is not permitted on constructors or destructors");
                return nullptr;
            }
            if(m_cursor.match(TokenType::Tilde)){
                if(!m_cursor.checkIdentifier(maybeClass)){
                    m_cursor.errorExpected("expected class name after '~' in out-of-line destructor");
                    return nullptr;
                }
                m_cursor.advance();
                decl->ownerClassName = maybeClass;
                decl->ownerTemplateParams = Move(parsedOwnerTemplateParams);
                decl->ownerTemplateParamDecls = Move(parsedOwnerTemplateParamDecls);
                decl->name = maybeClass;
                decl->isDestructor = true;
                if(!parseDestructorEmptyParams("expected '(' in out-of-line destructor"))
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
                decl->ownerTemplateParamDecls = Move(parsedOwnerTemplateParamDecls);
                decl->name = maybeClass;
                decl->isConstructor = true;
                if(!parseConstructorParamsAndRejectInitList(decl->params))
                    return nullptr;
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
    MVector<TemplateParamDecl> parsedOwnerTemplateParamDecls(m_cursor.arena());
    if(!parseCallableOwnerOrTemplateHeader(true, parsedOwnerTemplateParams, parsedOwnerTemplateParamDecls, decl->templateParams))
        return nullptr;

    if(m_cursor.match(TokenType::ColonColon)){
        decl->ownerClassName = firstName;
        decl->ownerTemplateParams = Move(parsedOwnerTemplateParams);
        decl->ownerTemplateParamDecls = Move(parsedOwnerTemplateParamDecls);
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

    if(!parseMethodParamsAndValidateArity(*decl, span))
        return nullptr;

    if(decl->ownerClassName.empty()){
        if(decl->isReflect){
            m_cursor.error(span.line, span.column, "'reflect' attribute is only permitted on class or interface methods, not free functions");
            return nullptr;
        }
        if(m_cursor.check(TokenType::Identifier) && IsMethodQualifierKeyword(m_cursor.current().text)){
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

b8 DeclParser::applyCallableAttributes(const Span<const ParsedAttribute> attributes, MethodDecl& decl){
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
#define NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE(attrName, declField) \
        if(attr.name == attrName && attr.arguments.empty()){ \
            decl.declField = true; \
            matchedAttr = true; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE
        if(!matchedAttr){
            MStringView prohibitedErr;
#define NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE(attrName, errorMessage) \
            if(attr.name == attrName) prohibitedErr = errorMessage;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_CALLABLE_ATTRIBUTE
            if(!prohibitedErr.empty())
                m_cursor.error(attr.line, attr.column, prohibitedErr);
            else
                m_cursor.error(attr.line, attr.column, "unsupported attribute on callable; only '[reflect]' is permitted");
            return false;
        }
    }
    return true;
}

b8 DeclParser::parseOperatorName(MStringView& outOpName){
    const MStringView prohibitedOverloadErr = FindProhibitedOperatorOverloadError(m_cursor.current().type);
    if(!prohibitedOverloadErr.empty()){
        m_cursor.error(prohibitedOverloadErr);
        return false;
    }

    if(m_cursor.match(TokenType::LeftBracket)){
        if(!m_cursor.expect(TokenType::RightBracket, "expected ']' after '[' in 'operator[]'"))
            return false;
        outOpName = "[]";
        return true;
    }

    const Token tok = m_cursor.current();
    if(IsOverloadableOperatorToken(tok.type)){
        outOpName = tok.text;
        m_cursor.advance();
        return true;
    }
    m_cursor.error("unsupported operator overload in adhd");
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

