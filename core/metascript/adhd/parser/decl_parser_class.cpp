// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/literal_decoder.h>

#include <core/metascript/adhd/ast/keyword_semantics.h>
#include <core/metascript/adhd/ast/operator_semantics.h>

#include "decl_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClassDecl* DeclParser::parseClassDecl(const SourceSpan span, const Span<const ParsedAttribute> attributes, const b8 isExtern, const b8 isImported){
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected class name");
        return nullptr;
    }

    ClassDecl* decl = AllocateAstNode<ClassDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->span = span;
    decl->isImported = isImported || isExtern;
    decl->isExtern = isExtern;
    m_cursor.advance();

    b8 seenExternFlags = false;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
        if(!tryParseExternAttribute(attr, decl->cppExportName, decl->requiredHeader, isExtern, matchedAttr))
            return nullptr;
        if(!matchedAttr && !tryParseExternClassAttribute(attr, *decl, isExtern, seenExternFlags, matchedAttr))
            return nullptr;
#define NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE(attrName, declField) \
        if(!matchedAttr && attr.name == attrName && attr.arguments.empty()){ \
            decl->declField = true; \
            matchedAttr = true; \
        }
#define NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE(attrName, declField, impliesReflect, argErrorMessage) \
        if(!matchedAttr && attr.name == attrName){ \
            if(attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::StringLiteral){ \
                m_cursor.error(attr.line, attr.column, argErrorMessage); \
                return nullptr; \
            } \
            decl->declField = attr.arguments[0].text; \
            if constexpr(impliesReflect) \
                decl->isReflect = true; \
            matchedAttr = true; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_CLASS_STRING_ATTRIBUTE
#undef NWB_ADHD_EXPORT_CLASS_FLAG_ATTRIBUTE
        if(!matchedAttr && attr.name == KeywordSemantics::s_CppTypeAlias){
            if(isExtern || attr.arguments.size() != 2u || attr.arguments[0].kind != TokenType::Identifier || attr.arguments[1].kind != TokenType::Identifier){
                m_cursor.error(attr.line, attr.column, "'[cpp_type_alias(Name, Type)]' requires two names on an authored class");
                return nullptr;
            }
            TypeAliasDecl* alias = AllocateAstNode<TypeAliasDecl>(m_cursor.arena());
            alias->span = { attr.line, attr.column };
            alias->name = attr.arguments[0].text;
            alias->aliasedType = AllocateAstNode<TypeRef>(m_cursor.arena());
            alias->aliasedType->name = attr.arguments[1].text;
            decl->cppTypeAliases.push_back(alias);
            matchedAttr = true;
        }
        if(!matchedAttr && attr.name == KeywordSemantics::s_Friend){
            if(isExtern || !decl->friendClasses.empty() || attr.arguments.empty()){
                m_cursor.error(attr.line, attr.column, "'[friend(...)]' requires one list of peer class names on an authored class");
                return nullptr;
            }
            for(const ParsedAttributeArg& peer : attr.arguments){
                if(peer.kind != TokenType::Identifier || peer.text == decl->name || ContainsStringView(decl->friendClasses, peer.text)){
                    m_cursor.error(attr.line, attr.column, "'[friend(...)]' requires distinct peer class names");
                    return nullptr;
                }
                decl->friendClasses.push_back(peer.text);
            }
            matchedAttr = true;
        }
        if(!matchedAttr && attr.name == KeywordSemantics::s_RecordLayout){
            u64 alignment = 0u;
            if(isExtern || decl->recordAlignment || attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::IntegerLiteral
                || !LiteralDecoder::TryParseUnsignedIntegerLiteral(attr.arguments[0].text, alignment)
                || !alignment || (alignment & (alignment - 1u)) || alignment > Limit<usize>::s_Max){
                m_cursor.error(attr.line, attr.column, "layout requires one power-of-two alignment on an ordinary class");
                return nullptr;
            }
            decl->recordAlignment = static_cast<usize>(alignment);
            matchedAttr = true;
        }
        if(!matchedAttr){
            MStringView prohibitedErr;
#define NWB_ADHD_EXPORT_PROHIBITED_CLASS_ATTRIBUTE(attrName, errorMessage) \
            if(attr.name == attrName) prohibitedErr = errorMessage;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_CLASS_ATTRIBUTE
            if(!prohibitedErr.empty())
                m_cursor.error(attr.line, attr.column, prohibitedErr);
            else
                m_cursor.error(attr.line, attr.column, "unsupported attribute on class; expected 'layout(alignment)', 'friend(...)', 'move_only', 'serialize', 'reflect', 'arena', 'modular', 'extensible', 'asset(\"...\")', 'system(\"...\")', 'subsystem(\"...\")', or 'component(\"...\")'");
            return nullptr;
        }
    }

    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(decl->templateParams))
            return nullptr;
    }

    if(!parseColonTypeList(decl->implementedInterfaces, "expected implemented interface name after ':'", true))
        return nullptr;

    if(decl->isMoveOnly && (isExtern || decl->isArenaOwner || decl->isSerialize || decl->recordAlignment)){
        m_cursor.error(span.line, span.column, "'[move_only]' requires an ordinary value class");
        return nullptr;
    }
    if(decl->recordAlignment && (decl->isArenaOwner || !decl->implementedInterfaces.empty() || !decl->subsystemTypeName.empty() || !decl->assetTypeName.empty() || !decl->componentTypeName.empty() || !decl->systemTypeName.empty())){
        m_cursor.error(span.line, span.column, "layout is only supported on plain value records");
        return nullptr;
    }

    if(isExtern && m_cursor.match(TokenType::Semicolon))
        return decl;

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start class body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        if(!parseClassMember(*decl))
            return nullptr;
    }

    if(!m_cursor.expect(TokenType::RightBrace, "expected '}' to close class declaration"))
        return nullptr;
    while(m_cursor.match(TokenType::Semicolon)){}

    return decl;
}

b8 DeclParser::tryParseExternClassAttribute(const ParsedAttribute& attr, ClassDecl& outClass, const b8 isExtern, b8& inOutSeenFlags, b8& outMatched){
    outMatched = false;
    if(!isExtern)
        return true;

    MStringView duplicateError;
    MStringView argError;
    const ExternAttributeKind::Enum kind = KeywordSemantics::TryParseExternAttribute(attr.name, &duplicateError, &argError);
    if(kind == ExternAttributeKind::CppFlags){
        if(inOutSeenFlags){
            m_cursor.error(attr.line, attr.column, duplicateError);
            return false;
        }
        if(attr.arguments.empty()){
            m_cursor.error(attr.line, attr.column, argError);
            return false;
        }
        for(const ParsedAttributeArg& arg : attr.arguments){
            ExternTypeFlagKind::Enum flagKind = ExternTypeFlagKind::None;
            if(arg.kind != TokenType::Identifier || !KeywordSemantics::TryParseExternTypeFlag(arg.text, flagKind)){
                m_cursor.error(attr.line, attr.column, argError);
                return false;
            }
            switch(flagKind){
            case ExternTypeFlagKind::SelfDefaulted:      outClass.isSelfDefaulted = true; break;
            case ExternTypeFlagKind::SmallValue:         outClass.isSmallValue = true; break;
            case ExternTypeFlagKind::BorrowView:         outClass.isBorrowView = true; break;
            case ExternTypeFlagKind::BinarySerializable: outClass.isBinarySerializable = true; break;
            case ExternTypeFlagKind::BraceInit:          outClass.emitsBraceFieldInit = true; break;
            case ExternTypeFlagKind::EqualitySwitch:     outClass.isEqualitySwitch = true; break;
            case ExternTypeFlagKind::NonThreadSafe:      outClass.isNonThreadSafeContainer = true; break;
            case ExternTypeFlagKind::MapKey:             outClass.isNwbMapKey = true; break;
            case ExternTypeFlagKind::ArenaFirstCtor:     outClass.arenaCtorMode = 1u; break;
            case ExternTypeFlagKind::ArenaOmitCtor:      outClass.arenaCtorMode = 2u; break;
            case ExternTypeFlagKind::NoCopy:
                outClass.isCopyConstructible = false;
                outClass.isMoveConstructible = false;
                break;
            case ExternTypeFlagKind::NoAssign:
                outClass.isCopyAssignable = false;
                outClass.isMoveAssignable = false;
                break;
            case ExternTypeFlagKind::Abstract:           outClass.isAbstract = true; break;
            case ExternTypeFlagKind::PolymorphicBase:    outClass.isPolymorphicBase = true; break;
            default: break;
            }
        }
        inOutSeenFlags = true;
        outMatched = true;
        return true;
    }

    return true;
}

void DeclParser::parseClassMethodQualifiers(MethodDecl& method){
    while(m_cursor.check(TokenType::Identifier) && KeywordSemantics::IsMethodQualifierKeyword(m_cursor.current().text)){
#define NWB_ADHD_EXPORT_METHOD_QUALIFIER(adhdKeyword, declField, duplicateErrorMessage) \
        if(m_cursor.matchIdentifier(adhdKeyword)){ \
            if(method.declField) \
                m_cursor.error(duplicateErrorMessage); \
            method.declField = true; \
            continue; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_METHOD_QUALIFIER
    }
}

b8 DeclParser::parseClassMemberCallableBody(const ClassDecl& ownerClass, MethodDecl& method, const MStringView missingTerminatorMessage){
    if(m_cursor.check(TokenType::LeftBrace)){
        if(method.isExtern){
            m_cursor.error(method.span.line, method.span.column, "'extern' methods cannot define a body");
            return false;
        }
        method.hasBody = true;
        const b8 allowSkip = ownerClass.templateParams.empty() && method.templateParams.empty() && !method.isConstructor;
        b8 skipped = false;
        if(!trySkipImportedCallableBody(method, allowSkip, skipped))
            return false;
        return skipped || m_stmtParser.parseBlockStatements(method.body);
    }
    return m_cursor.expect(TokenType::Semicolon, missingTerminatorMessage);
}

b8 DeclParser::parseDestructorEmptyParams(const MStringView missingLeftParenMessage){
    if(!m_cursor.expect(TokenType::LeftParen, missingLeftParenMessage))
        return false;
    return m_cursor.expect(TokenType::RightParen, "destructor cannot take parameters");
}

b8 DeclParser::parseConstructorParamsAndRejectInitList(MethodDecl& method){
    if(!m_typeParser.parseParamList(method.params, method.isExtern, method.isExtern, &method.minArgCount, &method.hasDefaultArgs, &method.isVariadic))
        return false;
    if(m_cursor.check(TokenType::Colon)){
        m_cursor.error("C++ constructor initializer lists (': member(...)') are prohibited in adhd; initialize 'this.member' inside the constructor body");
        return false;
    }
    return true;
}

b8 DeclParser::parseMethodParamsAndValidateArity(MethodDecl& method, const SourceSpan span){
    if(!m_typeParser.parseParamList(method.params, method.isExtern, method.isExtern, &method.minArgCount, &method.hasDefaultArgs, &method.isVariadic))
        return false;
    if(method.isOperator){
        MStringView opErr;
        if(!OperatorSemantics::ValidateMemberOperatorArity(method.name, method.params.size(), opErr)){
            m_cursor.error(span.line, span.column, opErr);
            return false;
        }
    }
    return true;
}

b8 DeclParser::parseClassMethodOrOperatorTail(
    ClassDecl& classDecl,
    const AccessSpecifier::Enum access,
    const b8 isStatic,
    const b8 isExtern,
    TypeRef* returnType,
    const MStringView name,
    const b8 isOperator,
    MVector<TemplateParamDecl>&& templateParams,
    const Span<const ParsedAttribute> attributes,
    const SourceSpan span,
    const MStringView missingTerminatorMessage
){
    MethodDecl* method = AllocateAstNode<MethodDecl>(m_cursor.arena());
    method->access = access;
    method->ownerClassName = classDecl.name;
    method->templateParams = Move(templateParams);
    method->returnType = returnType;
    method->returnsReference = (returnType && returnType->refMode != ParamPassingMode::None);
    method->name = name;
    method->isOperator = isOperator;
    method->isStatic = isStatic;
    method->isImported = classDecl.isImported || isExtern;
    method->isExtern = isExtern;
    method->requiredHeader = classDecl.requiredHeader;
    method->span = span;

    if(!applyCallableAttributes(attributes, *method))
        return false;
    if(method->isReflect)
        classDecl.isReflect = true;
    if(!parseMethodParamsAndValidateArity(*method, span))
        return false;

    parseClassMethodQualifiers(*method);
    if(!parseClassMemberCallableBody(classDecl, *method, missingTerminatorMessage))
        return false;

    classDecl.methods.push_back(method);
    return true;
}

b8 DeclParser::parseClassMember(ClassDecl& classDecl){
    MVector<ParsedAttribute> attributes(m_cursor.arena());
    if(!AttributeParser::ParseOptionalList(m_cursor, attributes))
        return false;

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};

    AccessSpecifier::Enum access = AccessSpecifier::None;
    if(m_cursor.check(TokenType::Identifier)){
        access = KeywordSemantics::ParseAccessSpecifierKeyword(m_cursor.current().text);
        if(access != AccessSpecifier::None)
            m_cursor.advance();
    }

    if(access != AccessSpecifier::None && m_cursor.check(TokenType::Colon)){
        m_cursor.error("C++-style 'public:' section labels are prohibited in adhd; write the access specifier on each member");
        return false;
    }

    b8 isMemberStatic = false;
    b8 isMemberExtern = classDecl.isExtern;
    if(classDecl.isExtern){
        b8 seenExternKeyword = false;
        for(;;){
            if(!isMemberStatic && m_cursor.matchIdentifier(KeywordSemantics::s_Static)){
                isMemberStatic = true;
                continue;
            }
            if(!seenExternKeyword && m_cursor.matchIdentifier(KeywordSemantics::s_Extern)){
                seenExternKeyword = true;
                isMemberExtern = true;
                continue;
            }
            break;
        }
    }

    if(m_cursor.check(TokenType::Identifier)){
        const MStringView prohibitedErr = KeywordSemantics::FindProhibitedMemberKeywordError(m_cursor.current().text, false);
        if(!prohibitedErr.empty()){
            m_cursor.error(prohibitedErr);
            return false;
        }
    }
    if(m_cursor.check(TokenType::Identifier) && KeywordSemantics::IsNestedTypeKeyword(m_cursor.current().text, true)){
        m_cursor.error("nested type declarations inside a class are prohibited in adhd");
        return false;
    }
    if(access == AccessSpecifier::None){
        if(classDecl.isExtern)
            access = AccessSpecifier::Public;
        else{
            m_cursor.error("every class member in adhd must explicitly specify 'public', 'private', or 'protected'");
            return false;
        }
    }

    if(!isMemberStatic && m_cursor.match(TokenType::Tilde)){
        if(!m_cursor.checkIdentifier(classDecl.name)){
            m_cursor.errorExpected("expected class name after '~' for destructor");
            return false;
        }
        m_cursor.advance();

        MethodDecl* dtor = AllocateAstNode<MethodDecl>(m_cursor.arena());
        dtor->access = access;
        dtor->isImported = classDecl.isImported || isMemberExtern;
        dtor->isExtern = isMemberExtern;
        dtor->requiredHeader = classDecl.requiredHeader;
        dtor->span = span;

        if(!applyCallableAttributes(attributes, *dtor))
            return false;
        if(!initializeDestructorDecl(*dtor, classDecl.name, "expected '(' for destructor"))
            return false;
        if(!parseClassMemberCallableBody(classDecl, *dtor, "expected '{' or ';' after destructor declaration"))
            return false;

        classDecl.methods.push_back(dtor);
        return true;
    }

    if(!isMemberStatic && m_cursor.checkIdentifier(classDecl.name)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        m_cursor.advance();
        if(m_cursor.check(TokenType::LeftParen)){
            MethodDecl* ctor = AllocateAstNode<MethodDecl>(m_cursor.arena());
            ctor->access = access;
            ctor->isImported = classDecl.isImported || isMemberExtern;
            ctor->isExtern = isMemberExtern;
            ctor->requiredHeader = classDecl.requiredHeader;
            ctor->span = span;

            if(!applyCallableAttributes(attributes, *ctor))
                return false;
            if(!initializeConstructorDecl(*ctor, classDecl.name))
                return false;
            if(!parseClassMemberCallableBody(classDecl, *ctor, "expected '{' or ';' after constructor declaration"))
                return false;

            classDecl.methods.push_back(ctor);
            return true;
        }
        m_cursor.restore(cp);
    }

    if(m_cursor.checkIdentifier(KeywordSemantics::s_Operator)){
        m_cursor.error("implicit type-conversion operators are prohibited in adhd; use 'cast<T>(x)' or an explicit named method");
        return false;
    }

    TypeRef* memberType = m_typeParser.parseType(true, isMemberExtern);
    if(!memberType)
        return false;

    if(m_cursor.matchIdentifier(KeywordSemantics::s_Operator)){
        MStringView opName;
        if(!parseOperatorName(opName))
            return false;

        MVector<TemplateParamDecl> emptyTemplateParams(m_cursor.arena());
        return parseClassMethodOrOperatorTail(classDecl, access, isMemberStatic, isMemberExtern, memberType, opName, true, Move(emptyTemplateParams), attributes, span, "expected '{' or ';' after operator declaration");
    }

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected member name");
        return false;
    }

    const MStringView memberName = m_cursor.current().text;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Colon)){
        m_cursor.error("C-style bitfields are prohibited in adhd; use 'flag<SizeType> Name'");
        return false;
    }
    if(m_cursor.check(TokenType::LeftBracket)){
        m_cursor.error(s_ProhibitedCStyleArrayError);
        return false;
    }

    MVector<TemplateParamDecl> methodTemplateParams(m_cursor.arena());
    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(methodTemplateParams))
            return false;
        if(!m_cursor.check(TokenType::LeftParen)){
            m_cursor.error("data members cannot be templated");
            return false;
        }
    }

    if(m_cursor.check(TokenType::LeftParen)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        m_cursor.advance();
        b8 isMethodSig = m_cursor.check(TokenType::RightParen)
            || MatchParamPassingMode(m_cursor) != ParamPassingMode::None
            || (isMemberExtern && m_cursor.check(TokenType::Dot));
        if(!isMethodSig && m_cursor.check(TokenType::Identifier)){
            if(!KeywordSemantics::FindProhibitedTypeKeywordError(m_cursor.current().text).empty()){
                isMethodSig = true;
            }
            else{
                TypeRef* maybeParamType = m_typeParser.parseType(false, isMemberExtern);
                if(maybeParamType && !m_typeParser.hasPendingRightAngles() && m_cursor.check(TokenType::Identifier))
                    isMethodSig = true;
                else if(!maybeParamType && (m_cursor.check(TokenType::Star) || m_cursor.check(TokenType::Ampersand))){
                    while(m_cursor.check(TokenType::Star) || m_cursor.check(TokenType::Ampersand))
                        m_cursor.advance();
                    if(m_cursor.check(TokenType::Identifier))
                        isMethodSig = true;
                }
                m_typeParser.resetPendingRightAngles();
            }
        }
        m_cursor.restore(cp);

        if(isMethodSig)
            return parseClassMethodOrOperatorTail(classDecl, access, isMemberStatic, isMemberExtern, memberType, memberName, false, Move(methodTemplateParams), attributes, span, "expected '{' or ';' after method declaration");
    }

    FieldDecl* field = AllocateAstNode<FieldDecl>(m_cursor.arena());
    field->access = access;
    field->type = memberType;
    field->name = memberName;
    field->span = span;
    field->originalIndex = classDecl.fields.size();
    field->isStatic = isMemberStatic;

    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
#define NWB_ADHD_EXPORT_FIELD_ATTRIBUTE(attrName, declField, impliesClassReflect) \
        if(attr.name == attrName && attr.arguments.empty()){ \
            field->declField = true; \
            if constexpr(impliesClassReflect) \
                classDecl.isReflect = true; \
            matchedAttr = true; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_FIELD_ATTRIBUTE
        if(!matchedAttr){
            m_cursor.error(attr.line, attr.column, "unsupported attribute on field; expected 'transient' or 'reflect'");
            return false;
        }
    }

    if(m_cursor.match(TokenType::Equal)){
        field->defaultExpr = m_exprParser.parseExpression();
        if(!field->defaultExpr)
            return false;
    }
    else if(m_cursor.check(TokenType::LeftParen)){
        field->hasCtorInit = true;
        if(!m_exprParser.parseArgumentList(field->ctorArgs))
            return false;
    }

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after field declaration"))
        return false;

    classDecl.fields.push_back(field);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

