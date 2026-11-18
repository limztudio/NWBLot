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

    const b8 hasExceptionProbe = m_cursor.matchIdentifier(KeywordSemantics::s_NoexceptLike);
    if(hasExceptionProbe){
        if(decl->isExtern || !decl->ownerClassName.empty() || decl->templateParams.empty() || decl->allowsCppUnwind){
            m_cursor.error("'noexcept_like' requires a generic free function definition with no other exception specification");
            return nullptr;
        }
        if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after 'noexcept_like'"))
            return nullptr;
        if(!m_cursor.check(TokenType::RightParen)){
            do{
                Expr* probe = m_exprParser.parseExpression();
                if(!probe)
                    return nullptr;
                decl->exceptionProbes.push_back(probe);
            }while(m_cursor.match(TokenType::Comma));
        }
        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after exception probe") || !m_cursor.check(TokenType::LeftBrace)){
            m_cursor.error("'noexcept_like' requires a function body");
            return nullptr;
        }
    }
    if(m_cursor.check(TokenType::LeftBrace)){
        if(decl->isExtern){
            m_cursor.error(span.line, span.column, "'extern' function declarations cannot define a body");
            return nullptr;
        }
        decl->hasBody = true;
        if(!m_stmtParser.parseBlockStatements(decl->body))
            return nullptr;
        if(hasExceptionProbe && decl->exceptionProbes.empty()){
            if(decl->body.size() != 1u || decl->body.front()->kind != StmtKind::Return || !decl->body.front()->expr){
                m_cursor.error("'noexcept_like()' requires a body containing one return expression");
                return nullptr;
            }
            decl->exceptionProbes.push_back(decl->body.front()->expr);
            decl->probesReturnValue = true;
        }
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
        case ExternCallableFlagKind::Unwind:       outMethod.allowsCppUnwind = true; break;
        case ExternCallableFlagKind::ScratchArena: outMethod.injectsCallerArena = true; break;
        case ExternCallableFlagKind::BorrowView:   outMethod.returnsBorrowedView = true; break;
        case ExternCallableFlagKind::PlatformLiteralArg0:
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
        if(attr.name == KeywordSemantics::s_CookSidecar){
            if(!decl.pairedSidecar.empty() || attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::Identifier){
                m_cursor.error(attr.line, attr.column, "'[cook_sidecar]' requires a StringView extension constant");
                return false;
            }
            decl.pairedSidecar = attr.arguments[0].text;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_CookEntry){
            const auto& args = attr.arguments;
            if(decl.assetCookRole != AssetCookRole::None || (args.size() != 5u && args.size() != 6u) || (args.size() == 6u && args[5].kind != TokenType::Identifier) || args[0].kind != TokenType::StringLiteral || args[0].text.empty()
                || args[1].kind != TokenType::Identifier || args[2].kind != TokenType::Identifier || args[3].kind != TokenType::Identifier || args[4].kind != TokenType::Identifier
                || (args[3].text != KeywordSemantics::s_CookDocumentOnly && args[3].text != KeywordSemantics::s_CookDocumentAndValue) || (args[4].text != KeywordSemantics::s_CookQuiet && args[4].text != KeywordSemantics::s_CookLogFailure)){
                m_cursor.error(attr.line, attr.column, "'[cook_entry]' requires a label, diagnostic prefix, value reader, document_only or document_and_value, and quiet or log_failure");
                return false;
            }
            decl.assetCookRole = AssetCookRole::Entry;
            decl.cookKindLabel = args[0].text;
            decl.cookDiagnosticPrefix = args[1].text;
            decl.cookValueReader = args[2].text;
            if(args.size() == 6u)
                decl.cookCodec = args[5].text;
            decl.cookAcceptsValue = args[3].text == KeywordSemantics::s_CookDocumentAndValue;
            decl.cookLogsFailure = args[4].text == KeywordSemantics::s_CookLogFailure;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_GatherMerge){
            if(decl.assetCookRole != AssetCookRole::None || !attr.arguments.empty()){
                m_cursor.error(attr.line, attr.column, "'[gather_merge]' takes no arguments and cannot be combined with another registry role");
                return false;
            }
            decl.assetCookRole = AssetCookRole::GatherMerge;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_CookDocument || attr.name == KeywordSemantics::s_VolumePrepare || attr.name == KeywordSemantics::s_CookSetup){
            if(decl.assetCookRole != AssetCookRole::None || !attr.arguments.empty()){
                m_cursor.error(attr.line, attr.column, "a cook callback role must appear once and takes no arguments");
                return false;
            }
            if(attr.name == KeywordSemantics::s_CookDocument)
                decl.assetCookRole = AssetCookRole::DocumentParser;
            else if(attr.name == KeywordSemantics::s_VolumePrepare)
                decl.assetCookRole = AssetCookRole::VolumePrepare;
            else
                decl.assetCookRole = AssetCookRole::Setup;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_AssetWriter || attr.name == KeywordSemantics::s_CookRegistrar){
            if(decl.assetCookRole != AssetCookRole::None || !attr.arguments.empty()){
                m_cursor.error(attr.line, attr.column, "an asset cook role must appear once and takes no arguments");
                return false;
            }
            decl.assetCookRole = attr.name == KeywordSemantics::s_AssetWriter ? AssetCookRole::Writer : AssetCookRole::Registrar;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_CppExplicitBool || attr.name == KeywordSemantics::s_CppTakeAssignment){
            b8& enabled = attr.name == KeywordSemantics::s_CppExplicitBool ? decl.exportsExplicitBool : decl.exportsTakingAssignment;
            if(enabled || !attr.arguments.empty() || decl.isExtern){
                m_cursor.error(attr.line, attr.column, "a C++ operator export takes no arguments and may appear once on an ADHD instance method");
                return false;
            }
            enabled = true;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_CppBase){
            if(!decl.initializedCppBase.empty() || attr.arguments.size() < 2u || attr.arguments[0].kind != TokenType::Identifier){
                m_cursor.error(attr.line, attr.column, "'[cpp_base]' requires one base name and at least one constructor parameter name");
                return false;
            }
            decl.initializedCppBase = attr.arguments[0].text;
            for(usize i = 1u; i < attr.arguments.size(); ++i){
                if(attr.arguments[i].kind != TokenType::Identifier){
                    m_cursor.error(attr.line, attr.column, "'[cpp_base]' arguments must name constructor parameters");
                    return false;
                }
                decl.cppBaseArguments.push_back(attr.arguments[i].text);
            }
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_CppRvalueOverload){
            if(decl.exportsRvalueOverload || !attr.arguments.empty() || decl.isExtern){
                m_cursor.error(attr.line, attr.column, "'[cpp_rvalue_overload]' takes no arguments and may appear once on an ADHD instance method");
                return false;
            }
            decl.exportsRvalueOverload = true;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_CppUnwind){
            if(decl.allowsCppUnwind || !attr.arguments.empty() || decl.isExtern){
                m_cursor.error(attr.line, attr.column, "'[cpp_unwind]' takes no arguments and may appear once on an ADHD function or method");
                return false;
            }
            decl.allowsCppUnwind = true;
            matchedAttr = true;
        }
        if(attr.name == KeywordSemantics::s_OwnerResult){
            if(decl.returnsFromOwnerArena || !attr.arguments.empty() || decl.isExtern){
                m_cursor.error(attr.line, attr.column, "'[owner_result]' takes no arguments and may appear once on a class method");
                return false;
            }
            decl.returnsFromOwnerArena = true;
            matchedAttr = true;
        }
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
                m_cursor.error(attr.line, attr.column, "unsupported attribute on callable");
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

