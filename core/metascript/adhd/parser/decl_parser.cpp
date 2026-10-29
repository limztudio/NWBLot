// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decl_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_decl_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ResolveEnumUnderlyingType(const MStringView sizeTypeName, usize& outSize, usize& outAlign)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat, defaultLiteral) \
    if constexpr(isFixedWidthInt){ \
        if(sizeTypeName == adhdName){ outSize = byteSize; outAlign = byteAlign; return true; } \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclParser::parseTranslationUnit(TranslationUnit& outUnit){
    while(!m_cursor.check(TokenType::EndOfFile)){
        if(!parseTopLevelDeclaration(outUnit))
            return false;
    }
    return !m_cursor.failed();
}

b8 DeclParser::isCurrentTokenImported()const noexcept{
    if(!m_preprocessor || m_activeSource.empty() || m_cursor.current().text.empty())
        return false;
    const MChar* base = m_activeSource.data();
    const MChar* tokPtr = m_cursor.current().text.data();
    if(tokPtr < base || tokPtr >= base + m_activeSource.size())
        return false;
    const usize offset = static_cast<usize>(tokPtr - base);
    return m_preprocessor->isOffsetInIncludedRange(offset);
}

b8 DeclParser::parseTopLevelDeclaration(TranslationUnit& outUnit){
    const b8 isImported = isCurrentTokenImported();

    MVector<ParsedAttribute> attributes(m_cursor.arena());
    if(m_cursor.check(TokenType::LeftBracket)){
        AttributeParser attrParser(m_cursor);
        if(!attrParser.parseSingleBracketList(attributes))
            return false;
    }

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};

#define NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD(keyword, errorMessage) \
    if(m_cursor.checkIdentifier(keyword)){ \
        m_cursor.error(errorMessage); \
        return false; \
    }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD

    if(m_cursor.matchIdentifier("native")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on top-level 'native' blocks");
            return false;
        }
        MStringView code;
        if(!m_stmtParser.parseRawNativeBlock(code))
            return false;
        if(!isImported)
            outUnit.topLevelNativeBlocks.push_back(code);
        return true;
    }

    if(m_cursor.matchIdentifier("const")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on file-level 'const' declarations");
            return false;
        }
        Stmt* stmt = m_stmtParser.parseConstDeclaration(span);
        if(!stmt)
            return false;
        stmt->isImported = isImported;
        outUnit.globalConstants.push_back(stmt);
        return true;
    }

    if(m_cursor.matchIdentifier("type")){
        TypeAliasDecl* aliasDecl = parseTypeAlias(span, attributes);
        if(!aliasDecl)
            return false;
        aliasDecl->isImported = isImported;
        outUnit.typeAliases.push_back(aliasDecl);
        return true;
    }

    if(m_cursor.matchIdentifier("enum")){
        EnumDecl* enumDecl = parseEnumDecl(span, attributes);
        if(!enumDecl)
            return false;
        enumDecl->isImported = isImported;
        outUnit.enums.push_back(enumDecl);
        return true;
    }

    if(m_cursor.matchIdentifier("interface")){
        InterfaceDecl* ifaceDecl = parseInterfaceDecl(span, attributes);
        if(!ifaceDecl)
            return false;
        ifaceDecl->isImported = isImported;
        outUnit.interfaces.push_back(ifaceDecl);
        return true;
    }

    if(m_cursor.matchIdentifier("class")){
        ClassDecl* classDecl = parseClassDecl(span, attributes);
        if(!classDecl)
            return false;
        classDecl->isImported = isImported;
        for(MethodDecl* m : classDecl->methods)
            m->isImported = isImported;
        outUnit.classes.push_back(classDecl);
        return true;
    }

    MethodDecl* callable = parseFreeOrOutOfLineCallable(span, attributes);
    if(!callable)
        return false;
    callable->isImported = isImported;

    if(callable->ownerClassName.empty())
        outUnit.freeFunctions.push_back(callable);
    else
        outUnit.outOfLineMethods.push_back(callable);

    return true;
}

TypeAliasDecl* DeclParser::parseTypeAlias(const SourceSpan span, const MVector<ParsedAttribute>& attributes){
    if(!attributes.empty()){
        m_cursor.error(span.line, span.column, "attributes are not permitted on 'type' alias declarations");
        return nullptr;
    }
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected alias name after 'type'");
        return nullptr;
    }

    TypeAliasDecl* decl = AllocateAstNode<TypeAliasDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->span = span;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(decl->templateParams))
            return nullptr;
    }

    if(!m_cursor.expect(TokenType::Equal, "expected '=' in 'type' alias declaration"))
        return nullptr;

    decl->aliasedType = m_typeParser.parseType(true, false);
    if(!decl->aliasedType)
        return nullptr;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'type' alias declaration"))
        return nullptr;

    return decl;
}

EnumDecl* DeclParser::parseEnumDecl(const SourceSpan span, const MVector<ParsedAttribute>& attributes){
    for(const ParsedAttribute& attr : attributes){
        if(attr.name != "reflect" || !attr.arguments.empty()){
            m_cursor.error(attr.line, attr.column, "unsupported attribute on 'enum'; only '[reflect]' is permitted");
            return nullptr;
        }
    }
    if(!m_cursor.match(TokenType::Less)){
        m_cursor.error("'enum' must explicitly specify an integer size type: 'enum<u8|u16|u32|u64|i8|i16|i32|i64>'");
        return nullptr;
    }

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected integer size type inside 'enum<...>'");
        return nullptr;
    }

    const MStringView sizeTypeName = m_cursor.current().text;
    usize underlyingSize = 1u;
    usize underlyingAlign = 1u;
    if(!__hidden_adhd_decl_parser::ResolveEnumUnderlyingType(sizeTypeName, underlyingSize, underlyingAlign)){
        m_cursor.error("invalid enum underlying type; expected u8, u16, u32, u64, i8, i16, i32, or i64");
        return nullptr;
    }
    m_cursor.advance();

    if(!m_cursor.expect(TokenType::Greater, "expected '>' after enum size type"))
        return nullptr;

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected enum name");
        return nullptr;
    }

    EnumDecl* decl = AllocateAstNode<EnumDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->sizeTypeName = sizeTypeName;
    decl->underlyingSize = underlyingSize;
    decl->underlyingAlign = underlyingAlign;
    decl->isReflect = !attributes.empty();
    decl->span = span;
    m_cursor.advance();

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start enum body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        b8 isFlag = false;
        if(m_cursor.matchIdentifier("flag")){
            isFlag = true;
        }
        else if(!m_cursor.matchIdentifier("enum")){
            m_cursor.error("enum entry must explicitly begin with 'enum' or 'flag'");
            return nullptr;
        }

        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected("expected enum enumerator name");
            return nullptr;
        }

        EnumEntry entry;
        entry.name = m_cursor.current().text;
        entry.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};
        entry.isFlag = isFlag;
        m_cursor.advance();

        if(m_cursor.match(TokenType::Equal)){
            entry.valueExpr = m_exprParser.parseExpression();
            if(!entry.valueExpr)
                return nullptr;
        }

        decl->entries.push_back(entry);

        if(m_cursor.check(TokenType::RightBrace))
            break;
        if(!m_cursor.expect(TokenType::Comma, "expected ',' or '}' in enum body"))
            return nullptr;
    }

    if(!m_cursor.expect(TokenType::RightBrace, "expected '}' to close enum declaration"))
        return nullptr;
    while(m_cursor.match(TokenType::Semicolon)){}

    return decl;
}

InterfaceDecl* DeclParser::parseInterfaceDecl(const SourceSpan span, const MVector<ParsedAttribute>& attributes){
    MStringView assetTypeName;
    b8 isReflect = false;
    for(const ParsedAttribute& attr : attributes){
        if(attr.name == "reflect" && attr.arguments.empty()){
            isReflect = true;
            continue;
        }
        if(attr.name == "asset" && attr.arguments.size() == 1u && attr.arguments[0].kind == TokenType::StringLiteral){
            assetTypeName = attr.arguments[0].text;
            continue;
        }
        m_cursor.error(attr.line, attr.column, "unsupported attribute on 'interface'; expected 'reflect' or 'asset(\"...\")'");
        return nullptr;
    }
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected interface name");
        return nullptr;
    }

    InterfaceDecl* decl = AllocateAstNode<InterfaceDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->assetTypeName = assetTypeName;
    decl->isReflect = isReflect;
    decl->span = span;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(decl->templateParams))
            return nullptr;
    }

    if(m_cursor.match(TokenType::Colon)){
        for(;;){
            if(!m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected base interface name after ':'");
                return nullptr;
            }
            TypeRef* baseType = m_typeParser.parseType(false, false);
            if(!baseType)
                return nullptr;
            decl->baseInterfaces.push_back(baseType);
            if(!m_cursor.match(TokenType::Comma))
                break;
        }
    }

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start interface body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        MVector<ParsedAttribute> methodAttrs(m_cursor.arena());
        if(m_cursor.check(TokenType::LeftBracket)){
            AttributeParser attrParser(m_cursor);
            if(!attrParser.parseSingleBracketList(methodAttrs))
                return nullptr;
        }

#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, astEnum) \
        if(m_cursor.checkIdentifier(adhdKeyword)){ \
            m_cursor.error("interface methods are implicitly public; do not write access specifiers inside 'interface'"); \
            return nullptr; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
        if(m_cursor.checkIdentifier("virtual")){
            m_cursor.error("'virtual' keyword is prohibited in adhd; interface methods are implicitly abstract contracts");
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
        if(!applyCallableAttributes(methodAttrs, *method))
            return nullptr;
        if(method->isForceInline || method->isNoInline){
            m_cursor.error(methodSpan.line, methodSpan.column, "'inline' and 'noinline' attributes are not permitted on abstract interface method declarations");
            return nullptr;
        }
        if(method->isReflect)
            decl->isReflect = true;

        if(m_cursor.matchIdentifier("operator")){
            MStringView opName;
            if(!parseOperatorName(opName))
                return nullptr;
            method->name = opName;
            method->isOperator = true;
        }
        else if(m_cursor.check(TokenType::Identifier)){
            method->name = m_cursor.current().text;
            m_cursor.advance();
        }
        else{
            m_cursor.errorExpected("expected interface method name");
            return nullptr;
        }

        if(m_cursor.check(TokenType::Less)){
            m_cursor.error(methodSpan.line, methodSpan.column, "interface methods cannot be templated; parameterize the enclosing 'interface' instead");
            return nullptr;
        }
        if(!m_cursor.check(TokenType::LeftParen)){
            m_cursor.error(methodSpan.line, methodSpan.column, "data members are prohibited inside 'interface'; only method contracts are allowed");
            return nullptr;
        }

        if(!m_typeParser.parseParamList(method->params))
            return nullptr;
        if(method->isOperator){
            MStringView opErr;
            if(!validateMemberOperatorArity(method->name, method->params.size(), opErr)){
                m_cursor.error(methodSpan.line, methodSpan.column, opErr);
                return nullptr;
            }
        }

        if(m_cursor.matchIdentifier("const"))
            method->isConst = true;
        if(m_cursor.checkIdentifier("override")){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

