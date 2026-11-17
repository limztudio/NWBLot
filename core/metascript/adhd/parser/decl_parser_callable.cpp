// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/keyword_semantics.h>

#include <core/metascript/adhd/ast/operator_semantics.h>

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

MethodDecl* DeclParser::parseFreeOrOutOfLineCallable(const SourceSpan span, const Span<const ParsedAttribute> attributes, const b8 isExtern){
    MethodDecl* decl = AllocateAstNode<MethodDecl>(m_cursor.arena());
    decl->span = span;
    decl->isImported = isExtern;
    decl->isExtern = isExtern;

    if(!applyCallableAttributes(attributes, *decl))
        return nullptr;

    if(!isExtern && m_cursor.check(TokenType::Identifier)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        const MStringView maybeClass = m_cursor.current().text;
        m_cursor.advance();

        MVector<MStringView> parsedOwnerTemplateParams(m_cursor.arena());
        MVector<TemplateParamDecl> parsedOwnerTemplateParamDecls(m_cursor.arena());
        MVector<TemplateParamDecl> ignoredLookaheadTemplateParams(m_cursor.arena());
        if(parseCallableOwnerOrTemplateHeader(false, parsedOwnerTemplateParams, parsedOwnerTemplateParamDecls, ignoredLookaheadTemplateParams) && m_cursor.match(TokenType::ColonColon)){
            if(m_cursor.match(TokenType::Tilde)){
                if(!m_cursor.checkIdentifier(maybeClass)){
                    m_cursor.errorExpected("expected class name after '~' in out-of-line destructor");
                    return nullptr;
                }
                m_cursor.advance();
                if(!initializeDestructorDecl(*decl, maybeClass, "expected '(' in out-of-line destructor", &parsedOwnerTemplateParams, &parsedOwnerTemplateParamDecls))
                    return nullptr;
                decl->hasBody = true;
                if(!m_stmtParser.parseBlockStatements(decl->body))
                    return nullptr;
                return decl;
            }

            if(m_cursor.checkIdentifier(maybeClass)){
                m_cursor.advance();
                if(!initializeConstructorDecl(*decl, maybeClass, &parsedOwnerTemplateParams, &parsedOwnerTemplateParamDecls))
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

    decl->returnType = m_typeParser.parseType(true, isExtern);
    if(!decl->returnType)
        return nullptr;
    decl->returnsReference = (decl->returnType->refMode != ParamPassingMode::None);

    if(m_cursor.checkIdentifier(KeywordSemantics::s_Operator)){
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
        if(!parseMethodOrOperatorName(decl->name, decl->isOperator, "expected method name after '::'"))
            return nullptr;
        if(!decl->isOperator && m_cursor.check(TokenType::Less)){
            if(!m_typeParser.parseTemplateParamList(decl->templateParams))
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
        if(m_cursor.check(TokenType::Identifier) && KeywordSemantics::IsMethodQualifierKeyword(m_cursor.current().text)){
            m_cursor.error(span.line, span.column, "'const' and 'override' qualifiers are only permitted on class methods, not free functions");
            return nullptr;
        }
    }
    else{
        parseClassMethodQualifiers(*decl);
    }

    if(m_cursor.check(TokenType::LeftBrace)){
        if(decl->isExtern){
            m_cursor.error(span.line, span.column, "'extern' function declarations cannot define a body");
            return nullptr;
        }
        decl->hasBody = true;
        if(!m_stmtParser.parseBlockStatements(decl->body))
            return nullptr;
    }
    else if(!decl->ownerClassName.empty()){
        m_cursor.error(span.line, span.column, isExtern
            ? "out-of-line method declarations cannot be marked 'extern'"
            : "out-of-line method definition must have a '{ ... }' body");
        return nullptr;
    }
    else if(!m_cursor.expect(TokenType::Semicolon, "expected '{' or ';' after function declaration"))
        return nullptr;

    return decl;
}

b8 DeclParser::tryParseExternCallableAttribute(const ParsedAttribute& attr, MethodDecl& outMethod, b8& inOutSeenFlags, b8& outMatched){
    outMatched = false;
    if(!outMethod.isExtern)
        return true;

    MStringView duplicateError;
    MStringView argError;
    const ExternAttributeKind::Enum kind = KeywordSemantics::TryParseExternAttribute(attr.name, &duplicateError, &argError);
    if(kind != ExternAttributeKind::CppFlags)
        return true;

    if(inOutSeenFlags){
        m_cursor.error(attr.line, attr.column, duplicateError);
        return false;
    }
    if(attr.arguments.empty()){
        m_cursor.error(attr.line, attr.column, argError);
        return false;
    }
    for(const ParsedAttributeArg& arg : attr.arguments){
        ExternCallableFlagKind::Enum flagKind = ExternCallableFlagKind::None;
        if(arg.kind != TokenType::Identifier || !KeywordSemantics::TryParseExternCallableFlag(arg.text, flagKind)){
            m_cursor.error(attr.line, attr.column, argError);
            return false;
        }
        switch(flagKind){
        case ExternCallableFlagKind::ScratchArena: outMethod.injectsCallerArena = true; break;
        case ExternCallableFlagKind::BorrowView:   outMethod.returnsBorrowedView = true; break;
        case ExternCallableFlagKind::ByteViewArg0:
        case ExternCallableFlagKind::ByteViewArg1:
            outMethod.callLoweringKind = static_cast<u8>(flagKind);
            break;
        default: break;
        }
    }
    inOutSeenFlags = true;
    outMatched = true;
    return true;
}

b8 DeclParser::applyCallableAttributes(const Span<const ParsedAttribute> attributes, MethodDecl& decl){
    b8 seenExternFlags = false;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
        if(!tryParseExternAttribute(attr, decl.cppExportName, decl.requiredHeader, decl.isExtern, matchedAttr))
            return false;
        if(!matchedAttr && !tryParseExternCallableAttribute(attr, decl, seenExternFlags, matchedAttr))
            return false;
#define NWB_ADHD_EXPORT_CALLABLE_ATTRIBUTE(attrName, declField) \
        if(!matchedAttr && attr.name == attrName && attr.arguments.empty()){ \
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
    const MStringView prohibitedOverloadErr = OperatorSemantics::FindProhibitedOperatorOverloadError(m_cursor.current().type);
    if(!prohibitedOverloadErr.empty()){
        m_cursor.error(prohibitedOverloadErr);
        return false;
    }

    TokenType::Enum closeBracket = TokenType::EndOfFile;
    MStringView bracketSym;
    MStringView missingCloseErr;
    if(OperatorSemantics::TryGetBracketOperator(m_cursor.current().type, closeBracket, bracketSym, missingCloseErr)){
        m_cursor.advance();
        if(!m_cursor.expect(closeBracket, missingCloseErr))
            return false;
        outOpName = bracketSym;
        return true;
    }

    const Token tok = m_cursor.current();
    if(OperatorSemantics::IsOverloadableOperatorToken(tok.type)){
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

