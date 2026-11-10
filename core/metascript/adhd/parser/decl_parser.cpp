// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/keyword_semantics.h>

#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include <core/metascript/adhd/ast/operator_semantics.h>

#include "decl_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclParser::ValidateMemberOperatorArity(const MStringView opName, const usize paramCount, MStringView& outErrorMsg)noexcept{
    usize minParams = 1u;
    usize maxParams = 1u;
    b8 prohibitsUnary = false;
    if(!OperatorSemantics::TryGetMemberOperatorArityRules(opName, minParams, maxParams, prohibitsUnary))
        return false;
    if(prohibitsUnary && paramCount == 0u){
        outErrorMsg = "unary 'operator*' (dereference) and unary 'operator&' (address-of) overloads are prohibited in adhd";
        return false;
    }
    if(paramCount < minParams || paramCount > maxParams){
        if(maxParams == 0u)
            outErrorMsg = "member 'operator~' cannot take parameters";
        else if(minParams == 0u)
            outErrorMsg = "member 'operator+' and 'operator-' may take at most 1 parameter";
        else
            outErrorMsg = "binary or indexing member operator overload must take exactly 1 parameter";
        return false;
    }
    return true;
}

b8 DeclParser::parseTranslationUnit(TranslationUnit& outUnit){
    m_pendingImplicitSourceNativeBlocks.clear();
    while(!m_cursor.check(TokenType::EndOfFile)){
        if(!parseTopLevelDeclaration(outUnit))
            return false;
    }
    for(const MStringView& pending : m_pendingImplicitSourceNativeBlocks)
        outUnit.sourceNativeBlocks.push_back(pending);
    m_pendingImplicitSourceNativeBlocks.clear();
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
    if(!AttributeParser::ParseOptionalList(m_cursor, attributes))
        return false;

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};

    if(m_cursor.check(TokenType::Identifier)){
        const MStringView prohibitedErr = KeywordSemantics::FindProhibitedTopLevelKeywordError(m_cursor.current().text);
        if(!prohibitedErr.empty()){
            m_cursor.error(prohibitedErr);
            return false;
        }
    }

    if(m_cursor.matchIdentifier("native")){
        b8 hasExplicitTarget = false;
        b8 explicitIsSource = false;
        for(const ParsedAttribute& attr : attributes){
            b8 attrIsSource = false;
            if(!KeywordSemantics::TryParseTopLevelNativeAttribute(attr.name, attrIsSource)){
                m_cursor.error(attr.line, attr.column, "unsupported attribute on top-level 'native' block; only '[header]' or '[source]' is permitted");
                return false;
            }
            if(!attr.arguments.empty()){
                m_cursor.error(attr.line, attr.column, "top-level 'native' target attribute does not accept arguments");
                return false;
            }
            if(hasExplicitTarget){
                m_cursor.error(attr.line, attr.column, "duplicate or conflicting target attribute on top-level 'native' block");
                return false;
            }
            hasExplicitTarget = true;
            explicitIsSource = attrIsSource;
        }
        MStringView code;
        if(!m_cursor.consumeBalancedBracesRaw(code))
            return false;
        if(!isImported){
            if(hasExplicitTarget){
                if(explicitIsSource){
                    for(const MStringView& pending : m_pendingImplicitSourceNativeBlocks)
                        outUnit.sourceNativeBlocks.push_back(pending);
                    m_pendingImplicitSourceNativeBlocks.clear();
                    outUnit.sourceNativeBlocks.push_back(code);
                }
                else{
                    outUnit.topLevelNativeBlocks.push_back(code);
                }
            }
            else{
                b8 hasPriorLocalDecl = !outUnit.sourceNativeBlocks.empty();
                if(!hasPriorLocalDecl){ for(const Stmt* s : outUnit.globalConstants){ if(s && !s->isImported){ hasPriorLocalDecl = true; break; } } }
                if(!hasPriorLocalDecl){ for(const TypeAliasDecl* a : outUnit.typeAliases){ if(a && !a->isImported){ hasPriorLocalDecl = true; break; } } }
                if(!hasPriorLocalDecl){ for(const EnumDecl* e : outUnit.enums){ if(e && !e->isImported){ hasPriorLocalDecl = true; break; } } }
                if(!hasPriorLocalDecl){ for(const InterfaceDecl* i : outUnit.interfaces){ if(i && !i->isImported){ hasPriorLocalDecl = true; break; } } }
                if(!hasPriorLocalDecl){ for(const ClassDecl* c : outUnit.classes){ if(c && !c->isImported){ hasPriorLocalDecl = true; break; } } }
                if(!hasPriorLocalDecl){ for(const MethodDecl* f : outUnit.freeFunctions){ if(f && !f->isImported){ hasPriorLocalDecl = true; break; } } }
                if(!hasPriorLocalDecl){ for(const MethodDecl* m : outUnit.outOfLineMethods){ if(m && !m->isImported){ hasPriorLocalDecl = true; break; } } }
                if(hasPriorLocalDecl)
                    m_pendingImplicitSourceNativeBlocks.push_back(code);
                else
                    outUnit.topLevelNativeBlocks.push_back(code);
            }
        }
        else if(!(hasExplicitTarget && explicitIsSource)){
            outUnit.hasImportedNativeBlocks = true;
        }
        return true;
    }

    if(!isImported && !m_pendingImplicitSourceNativeBlocks.empty()){
        for(const MStringView& pending : m_pendingImplicitSourceNativeBlocks)
            outUnit.topLevelNativeBlocks.push_back(pending);
        m_pendingImplicitSourceNativeBlocks.clear();
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

    b8 isFlagDecl = false;
    if(m_cursor.check(TokenType::Identifier) && KeywordSemantics::TryParseEnumDeclKeyword(m_cursor.current().text, &isFlagDecl)){
        m_cursor.advance();
        EnumDecl* enumDecl = parseEnumDecl(span, attributes, isFlagDecl);
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

TypeAliasDecl* DeclParser::parseTypeAlias(const SourceSpan span, const Span<const ParsedAttribute> attributes){
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

EnumDecl* DeclParser::parseEnumDecl(const SourceSpan span, const Span<const ParsedAttribute> attributes, const b8 isFlag){
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
#define NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE(attrName) \
        if(attr.name == attrName && attr.arguments.empty()) \
            matchedAttr = true;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ENUM_FLAG_ATTRIBUTE
        if(!matchedAttr){
            m_cursor.error(attr.line, attr.column, isFlag ? "unsupported attribute on 'flag'; only '[reflect]' is permitted" : "unsupported attribute on 'enum'; only '[reflect]' is permitted");
            return nullptr;
        }
    }
    if(!m_cursor.match(TokenType::Less)){
        m_cursor.error(isFlag
            ? "'flag' must explicitly specify an integer size type: 'flag<u8|u16|u32|u64|i8|i16|i32|i64> Name'"
            : "'enum' must explicitly specify an integer size type: 'enum<u8|u16|u32|u64|i8|i16|i32|i64> Name'");
        return nullptr;
    }

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected(isFlag ? "expected integer size type inside 'flag<...>'" : "expected integer size type inside 'enum<...>'");
        return nullptr;
    }

    const MStringView sizeTypeName = m_cursor.current().text;
    usize underlyingSize = 1u;
    usize underlyingAlign = 1u;
    if(!BuiltinTypeSemantics::ResolveFixedWidthIntScalar(sizeTypeName, underlyingSize, underlyingAlign)){
        m_cursor.error("invalid enum/flag underlying type; expected u8, u16, u32, u64, i8, i16, i32, or i64");
        return nullptr;
    }
    m_cursor.advance();

    if(!m_cursor.expect(TokenType::Greater, "expected '>' after underlying size type"))
        return nullptr;

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected(isFlag ? "expected flag name" : "expected enum name");
        return nullptr;
    }
    const MStringView enumName = m_cursor.current().text;
    m_cursor.advance();

    EnumDecl* decl = AllocateAstNode<EnumDecl>(m_cursor.arena());
    decl->name = enumName;
    decl->sizeTypeName = sizeTypeName;
    decl->underlyingSize = underlyingSize;
    decl->underlyingAlign = underlyingAlign;
    decl->span = span;
    decl->isFlag = isFlag;

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

InterfaceDecl* DeclParser::parseInterfaceDecl(const SourceSpan span, const Span<const ParsedAttribute> attributes){
    InterfaceDecl* decl = AllocateAstNode<InterfaceDecl>(m_cursor.arena());
    decl->span = span;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
#define NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE(attrName) \
        if(attr.name == attrName && attr.arguments.empty()) \
            matchedAttr = true;
#define NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE(attrName, declField) \
        if(!matchedAttr && attr.name == attrName && attr.arguments.size() == 1u && attr.arguments[0].kind == TokenType::StringLiteral){ \
            decl->declField = attr.arguments[0].text; \
            matchedAttr = true; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_INTERFACE_STRING_ATTRIBUTE
#undef NWB_ADHD_EXPORT_INTERFACE_FLAG_ATTRIBUTE
        if(!matchedAttr){
            m_cursor.error(attr.line, attr.column, "unsupported attribute on 'interface'; expected 'reflect' or 'asset(\"...\")'");
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
        if(!applyCallableAttributes(methodAttrs, *method))
            return nullptr;

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

