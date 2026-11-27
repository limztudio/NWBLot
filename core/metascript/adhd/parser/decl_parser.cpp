// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/keyword_semantics.h>

#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include <core/metascript/adhd/ast/operator_semantics.h>

#include "decl_parser.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclParser::parseTranslationUnit(TranslationUnit& outUnit){
    ScopedAstNodePool astPoolScope(outUnit.astNodePool);

    while(!m_cursor.check(TokenType::EndOfFile)){
        if(!parseTopLevelDeclaration(outUnit))
            return false;
    }

    return !m_cursor.failed();
}

b8 DeclParser::computeCurrentTokenOffset(usize& outOffset)const noexcept{
    if(!m_preprocessor || m_activeSource.empty() || m_cursor.current().text.empty())
        return false;
    const MChar* base = m_activeSource.data();
    const MChar* tokPtr = m_cursor.current().text.data();
    if(tokPtr < base || tokPtr >= base + m_activeSource.size())
        return false;
    outOffset = static_cast<usize>(tokPtr - base);
    return true;
}

b8 DeclParser::isCurrentTokenImported()const noexcept{
    usize offset = 0u;
    return computeCurrentTokenOffset(offset) && m_preprocessor->isOffsetInIncludedRange(offset);
}

MStringView DeclParser::currentTokenImportedNamespace()const noexcept{
    usize offset = 0u;
    if(!computeCurrentTokenOffset(offset))
        return {};
    return m_preprocessor->resolveIncludedNamespaceAtOffset(offset);
}

b8 DeclParser::tryParseExternAttribute(const ParsedAttribute& attr, MStringView& outCppExportName, MStringView& outRequiredHeader, const b8 isExtern, b8& outMatched){
    outMatched = false;
    MStringView duplicateError;
    MStringView argError;
    const ExternAttributeKind::Enum kind = KeywordSemantics::TryParseExternAttribute(attr.name, &duplicateError, &argError);
    if(kind == ExternAttributeKind::None)
        return true;

    if(!isExtern){
        m_cursor.error(attr.line, attr.column, "'[cpp_name(...)]', '[cpp_header(...)]', and '[cpp_flags(...)]' are only permitted on 'extern' declarations");
        return false;
    }
    if(kind != ExternAttributeKind::CppName && kind != ExternAttributeKind::CppHeader)
        return true;

    if(attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::StringLiteral || attr.arguments[0].text.empty()){
        m_cursor.error(attr.line, attr.column, argError);
        return false;
    }

    if(kind == ExternAttributeKind::CppName){
        if(!outCppExportName.empty()){
            m_cursor.error(attr.line, attr.column, duplicateError);
            return false;
        }
        outCppExportName = attr.arguments[0].text;
    }
    else if(kind == ExternAttributeKind::CppHeader){
        if(!outRequiredHeader.empty()){
            m_cursor.error(attr.line, attr.column, duplicateError);
            return false;
        }
        const MStringView rawHdr = attr.arguments[0].text;
        b8 hadAngles = false;
        b8 hadQuotes = false;
        const MStringView strippedHdr = StripHeaderDelimiters(rawHdr, &hadAngles, &hadQuotes);
        outRequiredHeader = (hadAngles || hadQuotes)
            ? rawHdr
            : InternAngleBracketHeader(m_cursor.arena(), strippedHdr);
    }

    outMatched = true;
    return true;
}

b8 DeclParser::parseRegistration(TranslationUnit& unit, const MVector<ParsedAttribute>& attributes, const b8 isExtern, const b8 isImported){
    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};
    if(!m_cursor.check(TokenType::Identifier) || !IsAsciiIdentifier(m_cursor.current().text)){
        m_cursor.error("expected a registration name");
        return false;
    }
    const MStringView name = m_cursor.current().text;
    m_cursor.advance();
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after registration name"))
        return false;

    MVector<MStringView> arguments(m_cursor.arena());
    if(!m_cursor.check(TokenType::RightParen)){
        do{
            if(!m_cursor.check(TokenType::Identifier) || !IsAsciiIdentifier(m_cursor.current().text)){
                m_cursor.error("registration arguments must be identifiers");
                return false;
            }
            arguments.push_back(m_cursor.current().text);
            m_cursor.advance();
        }while(m_cursor.match(TokenType::Comma));
    }
    if(!m_cursor.expect(TokenType::RightParen, "expected ')' after registration arguments")
        || !m_cursor.expect(TokenType::Semicolon, "expected ';' after registration"))
        return false;

    const RegistrationBinding* const existing = FindIfPointer(unit.registrationBindings, [name](const RegistrationBinding& binding){ return binding.name == name; });
    if(isExtern){
        RegistrationBinding binding{ name, {}, {}, arguments.size() };
        for(const ParsedAttribute& attr : attributes){
            b8 matched = false;
            if(!tryParseExternAttribute(attr, binding.cppName, binding.header, true, matched))
                return false;
            if(!matched){
                m_cursor.error(attr.line, attr.column, "unsupported registration attribute");
                return false;
            }
        }
        if(binding.header.empty() || !IsAsciiIdentifier(binding.cppName)){
            m_cursor.error(span.line, span.column, "extern registration requires cpp_header and an identifier in cpp_name");
            return false;
        }
        if(existing){
            if(existing->cppName == binding.cppName && existing->header == binding.header && existing->parameterCount == binding.parameterCount)
                return true;
            m_cursor.error(span.line, span.column, "conflicting extern registration declarations");
            return false;
        }
        unit.registrationBindings.push_back(binding);
        return true;
    }
    if(!attributes.empty() || !existing || arguments.size() != existing->parameterCount){
        m_cursor.error(span.line, span.column, "registration must match a declared registrar and its identifier count");
        return false;
    }
    if(!isImported){
        if(!FindIfPointer(unit.registrationHeaders, [existing](const MStringView header){ return header == existing->header; }))
            unit.registrationHeaders.push_back(existing->header);
        unit.registrationCalls.push_back(RegistrationCall{ existing->cppName, Move(arguments) });
    }
    return true;
}

b8 DeclParser::parseTopLevelDeclaration(TranslationUnit& outUnit){
    const b8 isImported = isCurrentTokenImported();
    const MStringView importedNamespace = isImported ? currentTokenImportedNamespace() : MStringView{};

    MVector<ParsedAttribute> attributes(m_cursor.arena());
    if(!AttributeParser::ParseOptionalList(m_cursor, attributes))
        return false;

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};
    const b8 isExtern = m_cursor.matchIdentifier(KeywordSemantics::s_Extern);

    if(m_cursor.matchIdentifier(KeywordSemantics::s_Registration))
        return parseRegistration(outUnit, attributes, isExtern, isImported);

    if(m_cursor.matchIdentifier(KeywordSemantics::s_RegisterAsset)){
        if(isExtern || !attributes.empty()){
            m_cursor.error(span.line, span.column, "register_asset does not accept attributes or extern");
            return false;
        }
        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.error("expected a concrete asset type after register_asset");
            return false;
        }
        const MStringView name = m_cursor.current().text;
        m_cursor.advance();
        if(!m_cursor.matchIdentifier(KeywordSemantics::s_RegistrationUsing)){
            m_cursor.error("expected 'using' and a registrar type after the asset type");
            return false;
        }
        TypeRef* registrar = m_typeParser.parseType();
        if(!registrar)
            return false;
        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after register_asset"))
            return false;
        if(!isImported){
            for(const AssetRegistration& registration : outUnit.assetRegistrations){
                if(registration.className == name){
                    m_cursor.error(span.line, span.column, "duplicate asset registration in this module");
                    return false;
                }
            }
            outUnit.assetRegistrations.push_back(AssetRegistration{ name, registrar, span });
        }
        return true;
    }

    if(m_cursor.matchIdentifier(KeywordSemantics::s_StaticAssert)){
        if(isExtern || !attributes.empty()){
            m_cursor.error(span.line, span.column, "static_assert does not accept attributes or extern");
            return false;
        }
        if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after static_assert"))
            return false;
        Expr* condition = m_exprParser.parseExpression();
        if(!condition)
            return false;
        Expr* message = nullptr;
        if(m_cursor.match(TokenType::Comma)){
            if(!m_cursor.check(TokenType::StringLiteral)){
                m_cursor.error("static_assert message must be a string literal");
                return false;
            }
            message = m_exprParser.parseExpression();
            if(!message)
                return false;
            if(message->kind != ExprKind::StringLiteral){
                m_cursor.error(span.line, span.column, "static_assert message must be a string literal");
                return false;
            }
        }
        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after static_assert") || !m_cursor.expect(TokenType::Semicolon, "expected ';' after static_assert"))
            return false;
        if(!isImported)
            outUnit.staticAssertions.push_back(StaticAssertion{ condition, message });
        return true;
    }

    if(m_cursor.matchIdentifier(KeywordSemantics::s_NamespaceWrapper)){
        if(isExtern || !attributes.empty()){
            m_cursor.error(span.line, span.column, "namespace_wrapper does not accept attributes or extern");
            return false;
        }
        if(!m_cursor.check(TokenType::Identifier) || !IsAsciiIdentifier(m_cursor.current().text)){
            m_cursor.error("expected a macro prefix after namespace_wrapper");
            return false;
        }
        if(m_cursor.current().text == ContractSemantics::s_DefaultRootNamespace){
            m_cursor.error("namespace_wrapper prefix is reserved for the engine namespace");
            return false;
        }
        if(!isImported){
            if(!outUnit.namespaceWrapper.empty()){
                m_cursor.error(span.line, span.column, "only one namespace_wrapper is allowed per file");
                return false;
            }
            outUnit.namespaceWrapper = m_cursor.current().text;
        }
        m_cursor.advance();
        return m_cursor.expect(TokenType::Semicolon, "expected ';' after namespace_wrapper");
    }

    if(isExtern && m_cursor.check(TokenType::StringLiteral)){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'extern \"<header>\"' import directives");
            return false;
        }
        const MStringView rawHeader = m_cursor.current().text;
        m_cursor.advance();
        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'extern \"...\"' header import"))
            return false;

        b8 hasAngles = false;
        MStringView inner = StripHeaderDelimiters(rawHeader, nullptr, nullptr);
        inner = StripHeaderDelimiters(inner, &hasAngles, nullptr);
        if(inner.empty()){
            m_cursor.error(span.line, span.column, "C++ header path in 'extern \"...\"' cannot be empty");
            return false;
        }

        ImportedCppHeader hdr;
        hdr.headerPath = rawHeader;
        hdr.span = span;
        hdr.isSystemHeader = hasAngles;
        outUnit.importedCppHeaders.push_back(hdr);
        return true;
    }

    if(m_cursor.check(TokenType::Identifier)){
        const MStringView prohibitedErr = KeywordSemantics::FindProhibitedTopLevelKeywordError(m_cursor.current().text);
        if(!prohibitedErr.empty()){
            m_cursor.error(prohibitedErr);
            return false;
        }
    }

    if(m_cursor.matchIdentifier(KeywordSemantics::s_Const)){
        MStringView extCppExportName;
        MStringView extRequiredHeader;
        for(const ParsedAttribute& attr : attributes){
            b8 matchedExtern = false;
            if(!tryParseExternAttribute(attr, extCppExportName, extRequiredHeader, isExtern, matchedExtern))
                return false;
            if(!matchedExtern){
                m_cursor.error(span.line, span.column, isExtern
                    ? "unsupported attribute on 'extern const' declaration; only '[cpp_name(\"...\")]' and '[cpp_header(\"...\")]' are permitted"
                    : "attributes are not permitted on file-level 'const' declarations");
                return false;
            }
        }
        Stmt* stmt = m_stmtParser.parseConstDeclaration(span, isExtern);
        if(!stmt)
            return false;
        stmt->cppNamespace = importedNamespace;
        stmt->cppExportName = extCppExportName;
        stmt->requiredHeader = extRequiredHeader;
        stmt->isImported = isImported || isExtern;
        stmt->isExtern = isExtern;
        outUnit.globalConstants.push_back(stmt);
        return true;
    }

    if(m_cursor.matchIdentifier(KeywordSemantics::s_Type)){
        TypeAliasDecl* aliasDecl = parseTypeAlias(span, attributes, isExtern);
        if(!aliasDecl)
            return false;
        aliasDecl->cppNamespace = importedNamespace;
        aliasDecl->isImported = isImported || isExtern;
        aliasDecl->isExtern = isExtern;
        outUnit.typeAliases.push_back(aliasDecl);
        return true;
    }

    b8 isFlagDecl = false;
    if(m_cursor.check(TokenType::Identifier) && KeywordSemantics::TryParseEnumDeclKeyword(m_cursor.current().text, &isFlagDecl)){
        m_cursor.advance();
        EnumDecl* enumDecl = parseEnumDecl(span, attributes, isFlagDecl, isExtern);
        if(!enumDecl)
            return false;
        enumDecl->cppNamespace = importedNamespace;
        enumDecl->isImported = isImported || isExtern;
        enumDecl->isExtern = isExtern;
        outUnit.enums.push_back(enumDecl);
        return true;
    }

    if(m_cursor.matchIdentifier(KeywordSemantics::s_Interface)){
        InterfaceDecl* ifaceDecl = parseInterfaceDecl(span, attributes, isExtern);
        if(!ifaceDecl)
            return false;
        ifaceDecl->cppNamespace = importedNamespace;
        ifaceDecl->isImported = isImported || isExtern;
        ifaceDecl->isExtern = isExtern;
        outUnit.interfaces.push_back(ifaceDecl);
        return true;
    }

    if(m_cursor.matchIdentifier(KeywordSemantics::s_Class)){
        ClassDecl* classDecl = parseClassDecl(span, attributes, isExtern, isImported);
        if(!classDecl)
            return false;
        classDecl->cppNamespace = importedNamespace;
        classDecl->isImported = isImported || isExtern;
        classDecl->isExtern = isExtern;
        for(MethodDecl* m : classDecl->methods){
            m->cppNamespace = importedNamespace;
            m->isImported = isImported || isExtern;
            m->isExtern = isExtern;
            if(m->requiredHeader.empty())
                m->requiredHeader = classDecl->requiredHeader;
        }
        outUnit.classes.push_back(classDecl);
        return true;
    }

    MethodDecl* callable = parseFreeOrOutOfLineCallable(span, attributes, isExtern, isImported);
    if(!callable)
        return false;
    callable->cppNamespace = importedNamespace;
    callable->isImported = isImported || isExtern;
    callable->isExtern = isExtern;

    if(callable->ownerClassName.empty())
        outUnit.freeFunctions.push_back(callable);
    else
        outUnit.outOfLineMethods.push_back(callable);

    return true;
}

TypeAliasDecl* DeclParser::parseTypeAlias(const SourceSpan span, const Span<const ParsedAttribute> attributes, const b8 isExtern){
    MStringView extCppExportName;
    MStringView extRequiredHeader;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedExtern = false;
        if(!tryParseExternAttribute(attr, extCppExportName, extRequiredHeader, isExtern, matchedExtern))
            return nullptr;
        if(!matchedExtern){
            m_cursor.error(span.line, span.column, isExtern
                ? "unsupported attribute on 'extern type' declaration; only '[cpp_name(\"...\")]' and '[cpp_header(\"...\")]' are permitted"
                : "attributes are not permitted on 'type' alias declarations");
            return nullptr;
        }
    }
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected alias name after 'type'");
        return nullptr;
    }

    TypeAliasDecl* decl = AllocateAstNode<TypeAliasDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->span = span;
    decl->cppExportName = extCppExportName;
    decl->requiredHeader = extRequiredHeader;
    decl->isImported = isExtern;
    decl->isExtern = isExtern;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(decl->templateParams))
            return nullptr;
    }

    if(isExtern && m_cursor.match(TokenType::Semicolon))
        return decl;

    if(!m_cursor.expect(TokenType::Equal, "expected '=' in 'type' alias declaration"))
        return nullptr;

    decl->aliasedType = m_typeParser.parseType(true, false);
    if(!decl->aliasedType)
        return nullptr;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'type' alias declaration"))
        return nullptr;

    return decl;
}

EnumDecl* DeclParser::parseEnumDecl(const SourceSpan span, const Span<const ParsedAttribute> attributes, const b8 isFlag, const b8 isExtern){
    MStringView extCppExportName;
    MStringView extRequiredHeader;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
        if(!tryParseExternAttribute(attr, extCppExportName, extRequiredHeader, isExtern, matchedAttr))
            return nullptr;
        if(!matchedAttr){
            if(attr.name == "reflect")
                m_cursor.error(attr.line, attr.column, "'reflect' attribute is prohibited on 'enum' and 'flag' in adhd; enum and flag reflection is decided automatically by the adhd compiler");
            else if(isExtern)
                m_cursor.error(attr.line, attr.column, isFlag ? "unsupported attribute on 'extern flag'; only '[cpp_name(\"...\")]' and '[cpp_header(\"...\")]' are permitted" : "unsupported attribute on 'extern enum'; only '[cpp_name(\"...\")]' and '[cpp_header(\"...\")]' are permitted");
            else
                m_cursor.error(attr.line, attr.column, isFlag ? "attributes are not permitted on 'flag'" : "attributes are not permitted on 'enum'");
            return nullptr;
        }
    }

    MStringView sizeTypeName;
    usize underlyingSize = 4u;
    usize underlyingAlign = 4u;
    MStringView enumName;

    const auto parseFixedWidthUnderlyingType = [&](const MStringView expectedIdentMsg) -> b8{
        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected(expectedIdentMsg);
            return false;
        }
        sizeTypeName = m_cursor.current().text;
        if(!BuiltinTypeSemantics::ResolveFixedWidthIntScalar(sizeTypeName, underlyingSize, underlyingAlign)){
            m_cursor.error("invalid enum/flag underlying type; expected u8, u16, u32, u64, i8, i16, i32, or i64");
            return false;
        }
        m_cursor.advance();
        return true;
    };

    if(m_cursor.match(TokenType::Less)){
        if(!parseFixedWidthUnderlyingType(isFlag ? "expected integer size type inside 'flag<...>'" : "expected integer size type inside 'enum<...>'"))
            return nullptr;

        if(!m_cursor.expect(TokenType::Greater, "expected '>' after underlying size type"))
            return nullptr;

        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected(isFlag ? "expected flag name" : "expected enum name");
            return nullptr;
        }
        enumName = m_cursor.current().text;
        m_cursor.advance();
    }
    else if(isExtern){
        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected(isFlag ? "expected flag name" : "expected enum name");
            return nullptr;
        }
        enumName = m_cursor.current().text;
        m_cursor.advance();

        if(m_cursor.match(TokenType::Colon)){
            if(!parseFixedWidthUnderlyingType("expected integer underlying type after ':'"))
                return nullptr;
        }
        else{
            sizeTypeName = BuiltinTypeSemantics::UInt32ScalarTypeName();
            if(!BuiltinTypeSemantics::ResolveFixedWidthIntScalar(sizeTypeName, underlyingSize, underlyingAlign))
                return nullptr;
        }
    }
    else{
        m_cursor.error(isFlag
            ? "'flag' must explicitly specify an integer size type: 'flag<u8|u16|u32|u64|i8|i16|i32|i64> Name'"
            : "'enum' must explicitly specify an integer size type: 'enum<u8|u16|u32|u64|i8|i16|i32|i64> Name'");
        return nullptr;
    }

    EnumDecl* decl = AllocateAstNode<EnumDecl>(m_cursor.arena());
    decl->name = enumName;
    decl->sizeTypeName = sizeTypeName;
    decl->underlyingSize = underlyingSize;
    decl->underlyingAlign = underlyingAlign;
    decl->span = span;
    decl->isFlag = isFlag;
    decl->cppExportName = extCppExportName;
    decl->requiredHeader = extRequiredHeader;
    decl->isImported = isExtern;
    decl->isExtern = isExtern;
    if(isExtern && !extCppExportName.empty()){
        const b8 hasWrappedSuffix = ContractSemantics::HasWrappedNwbEnumSuffix(extCppExportName);
        decl->isDirectCppEnum = !hasWrappedSuffix;
    }

    if(!m_cursor.expect(TokenType::LeftBrace, isFlag ? "expected '{' to start flag body" : "expected '{' to start enum body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected(isFlag ? "expected flag enumerator name" : "expected enum enumerator name");
            return nullptr;
        }

        EnumEntry entry;
        entry.name = m_cursor.current().text;
        entry.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};
        m_cursor.advance();

        if(KeywordSemantics::TryParseEnumDeclKeyword(entry.name) && m_cursor.check(TokenType::Identifier)){
            m_cursor.error("enumerator entries must not be prefixed with 'enum' or 'flag'");
            return nullptr;
        }

        if(m_cursor.match(TokenType::Equal)){
            entry.valueExpr = m_exprParser.parseExpression();
            if(!entry.valueExpr)
                return nullptr;
        }

        decl->entries.push_back(entry);

        if(m_cursor.check(TokenType::RightBrace))
            break;
        if(!m_cursor.expect(TokenType::Comma, isFlag ? "expected ',' or '}' in flag body" : "expected ',' or '}' in enum body"))
            return nullptr;
    }

    if(!m_cursor.expect(TokenType::RightBrace, isFlag ? "expected '}' to close flag declaration" : "expected '}' to close enum declaration"))
        return nullptr;
    while(m_cursor.match(TokenType::Semicolon)){}

    return decl;
}

InterfaceDecl* DeclParser::parseInterfaceDecl(const SourceSpan span, const Span<const ParsedAttribute> attributes, const b8 isExtern){
    InterfaceDecl* decl = AllocateAstNode<InterfaceDecl>(m_cursor.arena());
    decl->span = span;
    decl->isImported = isExtern;
    decl->isExtern = isExtern;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
        if(!tryParseExternAttribute(attr, decl->cppExportName, decl->requiredHeader, isExtern, matchedAttr))
            return nullptr;
#define NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE(attrName, declField) \
        if(!matchedAttr && attr.name == attrName && attr.arguments.size() == 1u && attr.arguments[0].kind == TokenType::StringLiteral){ \
            decl->declField = attr.arguments[0].text; \
            matchedAttr = true; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE
        if(!matchedAttr){
            if(attr.name == "reflect")
                m_cursor.error(attr.line, attr.column, "'reflect' attribute is prohibited on 'interface' in adhd; interface reflection is decided automatically by the adhd compiler");
            else
                m_cursor.error(attr.line, attr.column, "unsupported attribute on 'interface'; expected 'asset(\"...\")'");
            return nullptr;
        }
    }
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected interface name");
        return nullptr;
    }

    decl->name = m_cursor.current().text;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(decl->templateParams))
            return nullptr;
    }

    if(!parseColonTypeList(decl->baseInterfaces, "expected base interface name after ':'", false))
        return nullptr;

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start interface body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        MVector<ParsedAttribute> methodAttrs(m_cursor.arena());
        if(!AttributeParser::ParseOptionalList(m_cursor, methodAttrs))
            return nullptr;

        if(m_cursor.check(TokenType::Identifier) && KeywordSemantics::ParseAccessSpecifierKeyword(m_cursor.current().text) != AccessSpecifier::None){
            m_cursor.error("interface methods are implicitly public; do not write access specifiers inside 'interface'");
            return nullptr;
        }
        if(m_cursor.check(TokenType::Identifier)){
            const MStringView prohibitedErr = KeywordSemantics::FindProhibitedMemberKeywordError(m_cursor.current().text, true);
            if(!prohibitedErr.empty()){
                m_cursor.error(prohibitedErr);
                return nullptr;
            }
        }
        if(m_cursor.check(TokenType::Identifier) && KeywordSemantics::IsNestedTypeKeyword(m_cursor.current().text, true)){
            m_cursor.error("nested type declarations inside 'interface' are prohibited in adhd; declare types at file scope");
            return nullptr;
        }

        const SourceSpan methodSpan{m_cursor.current().line, m_cursor.current().column};
        TypeRef* retType = m_typeParser.parseType(true, false);
        if(!retType)
            return nullptr;

        MethodDecl* method = AllocateAstNode<MethodDecl>(m_cursor.arena());
        method->access = AccessSpecifier::Public;
        method->returnType = retType;
        method->span = methodSpan;
        method->isImported = isExtern;
        method->isExtern = isExtern;
        method->requiredHeader = decl->requiredHeader;
        if(!applyCallableAttributes(methodAttrs, *method))
            return nullptr;
        if(method->isReflect){
            m_cursor.error(methodSpan.line, methodSpan.column, "'reflect' attribute is prohibited on 'interface' methods; interface reflection is decided automatically by the adhd compiler");
            return nullptr;
        }

        if(!parseMethodOrOperatorName(method->name, method->isOperator, "expected interface method name"))
            return nullptr;

        if(m_cursor.check(TokenType::Less)){
            m_cursor.error(methodSpan.line, methodSpan.column, "interface methods cannot be templated; parameterize the enclosing 'interface' instead");
            return nullptr;
        }
        if(!m_cursor.check(TokenType::LeftParen)){
            m_cursor.error(methodSpan.line, methodSpan.column, "data members are prohibited inside 'interface'; only method contracts are allowed");
            return nullptr;
        }

        if(!parseMethodParamsAndValidateArity(*method, methodSpan))
            return nullptr;

        parseClassMethodQualifiers(*method);
        if(method->isOverride){
            m_cursor.error(methodSpan.line, methodSpan.column, "'override' is only permitted on 'class' methods implementing an 'interface', not inside 'interface' itself");
            return nullptr;
        }

        if(m_cursor.check(TokenType::LeftBrace)){
            m_cursor.error(methodSpan.line, methodSpan.column, "interface methods cannot have inline bodies");
            return nullptr;
        }

        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after interface method declaration"))
            return nullptr;

        decl->methods.push_back(method);
    }

    if(!m_cursor.expect(TokenType::RightBrace, "expected '}' to close interface declaration"))
        return nullptr;
    while(m_cursor.match(TokenType::Semicolon)){}

    return decl;
}

b8 DeclParser::parseColonTypeList(MVector<TypeRef*>& outTypes, const MStringView expectedTypeMsg, const b8 rejectAccessSpecifiers){
    if(!m_cursor.match(TokenType::Colon))
        return true;

    for(;;){
        if(rejectAccessSpecifiers && m_cursor.check(TokenType::Identifier) && KeywordSemantics::ParseAccessSpecifierKeyword(m_cursor.current().text) != AccessSpecifier::None){
            m_cursor.error("do not write 'public'/'private'/'protected' before implemented interface names in adhd");
            return false;
        }
        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected(expectedTypeMsg);
            return false;
        }
        TypeRef* baseType = m_typeParser.parseType(false, false);
        if(!baseType)
            return false;
        outTypes.push_back(baseType);
        if(!m_cursor.match(TokenType::Comma))
            break;
    }
    return true;
}

b8 DeclParser::parseMethodOrOperatorName(MStringView& outName, b8& outIsOperator, const MStringView expectedNameMsg){
    outIsOperator = false;
    if(m_cursor.matchIdentifier(KeywordSemantics::s_Operator)){
        if(!parseOperatorName(outName))
            return false;
        outIsOperator = true;
        return true;
    }
    if(m_cursor.check(TokenType::Identifier)){
        outName = m_cursor.current().text;
        m_cursor.advance();
        return true;
    }
    m_cursor.errorExpected(expectedNameMsg);
    return false;
}

b8 DeclParser::initializeDestructorDecl(
    MethodDecl& method,
    const MStringView ownerName,
    const MStringView missingLeftParenMsg,
    MVector<MStringView>* const ownerTemplateParams,
    MVector<TemplateParamDecl>* const ownerTemplateParamDecls
){
    method.ownerClassName = ownerName;
    if(ownerTemplateParams)
        method.ownerTemplateParams = Move(*ownerTemplateParams);
    if(ownerTemplateParamDecls)
        method.ownerTemplateParamDecls = Move(*ownerTemplateParamDecls);
    method.name = ownerName;
    method.isDestructor = true;
    if(method.isReflect){
        m_cursor.error(method.span.line, method.span.column, "'reflect' attribute is not permitted on constructors or destructors");
        return false;
    }
    return parseDestructorEmptyParams(missingLeftParenMsg);
}

b8 DeclParser::initializeConstructorDecl(
    MethodDecl& method,
    const MStringView ownerName,
    MVector<MStringView>* const ownerTemplateParams,
    MVector<TemplateParamDecl>* const ownerTemplateParamDecls
){
    method.ownerClassName = ownerName;
    if(ownerTemplateParams)
        method.ownerTemplateParams = Move(*ownerTemplateParams);
    if(ownerTemplateParamDecls)
        method.ownerTemplateParamDecls = Move(*ownerTemplateParamDecls);
    method.name = ownerName;
    method.isConstructor = true;
    if(method.isReflect){
        m_cursor.error(method.span.line, method.span.column, "'reflect' attribute is not permitted on constructors or destructors");
        return false;
    }
    return parseConstructorParamsAndRejectInitList(method);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

