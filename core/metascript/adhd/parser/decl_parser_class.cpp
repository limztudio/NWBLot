// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decl_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClassDecl* DeclParser::parseClassDecl(const SourceSpan span, const MVector<ParsedAttribute>& attributes){
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected class name");
        return nullptr;
    }

    ClassDecl* decl = AllocateAstNode<ClassDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->span = span;
    m_cursor.advance();

    for(const ParsedAttribute& attr : attributes){
        if(attr.name == "serialize" && attr.arguments.empty()){
            decl->isSerialize = true;
        }
        else if(attr.name == "reflect" && attr.arguments.empty()){
            decl->isReflect = true;
        }
        else if(attr.name == "asset"){
            if(attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::StringLiteral){
                m_cursor.error(attr.line, attr.column, "'[asset(\"...\")]' requires a single string literal argument");
                return nullptr;
            }
            decl->assetTypeName = attr.arguments[0].text;
            decl->isReflect = true;
        }
        else if(attr.name == "subsystem"){
            if(attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::StringLiteral){
                m_cursor.error(attr.line, attr.column, "'[subsystem(\"...\")]' requires a single string literal argument");
                return nullptr;
            }
            decl->subsystemTypeName = attr.arguments[0].text;
        }
        else if(attr.name == "component_asset"){
            if(attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::StringLiteral){
                m_cursor.error(attr.line, attr.column, "'[component_asset(\"...\")]' requires a single string literal argument");
                return nullptr;
            }
            decl->componentAssetTypeName = attr.arguments[0].text;
        }
        else{
            m_cursor.error(attr.line, attr.column, "unsupported attribute on class; expected 'serialize', 'reflect', 'asset(\"...\")', 'subsystem(\"...\")', or 'component_asset(\"...\")'");
            return nullptr;
        }
    }

    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(decl->templateParams))
            return nullptr;
    }

    if(m_cursor.match(TokenType::Colon)){
        for(;;){
            if(m_cursor.checkIdentifier("public") || m_cursor.checkIdentifier("private") || m_cursor.checkIdentifier("protected")){
                m_cursor.error("do not write 'public'/'private'/'protected' before implemented interface names in adhd");
                return nullptr;
            }
            if(!m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected implemented interface name after ':'");
                return nullptr;
            }
            TypeRef* ifaceType = m_typeParser.parseType(false, false);
            if(!ifaceType)
                return nullptr;
            decl->implementedInterfaces.push_back(ifaceType);
            if(!m_cursor.match(TokenType::Comma))
                break;
        }
    }

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start class body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        if(!parseClassMember(*decl))
            return nullptr;
    }

    if(!m_cursor.expect(TokenType::RightBrace, "expected '}' to close class declaration"))
        return nullptr;

    return decl;
}

void DeclParser::parseClassMethodQualifiers(MethodDecl& method)noexcept{
    while(m_cursor.checkIdentifier("const") || m_cursor.checkIdentifier("override")){
        if(m_cursor.matchIdentifier("const"))
            method.isConst = true;
        else if(m_cursor.matchIdentifier("override"))
            method.isOverride = true;
    }
}

b8 DeclParser::parseClassMemberCallableBody(MethodDecl& method, const MStringView missingTerminatorMessage){
    if(m_cursor.check(TokenType::LeftBrace)){
        method.hasBody = true;
        return m_stmtParser.parseBlockStatements(method.body);
    }
    return m_cursor.expect(TokenType::Semicolon, missingTerminatorMessage);
}

b8 DeclParser::parseClassMember(ClassDecl& classDecl){
    MVector<ParsedAttribute> attributes(m_cursor.arena());
    if(m_cursor.check(TokenType::LeftBracket)){
        AttributeParser attrParser(m_cursor);
        if(!attrParser.parseSingleBracketList(attributes))
            return false;
    }

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};

    AccessSpecifier::Enum access = AccessSpecifier::None;
    if(m_cursor.matchIdentifier("public"))
        access = AccessSpecifier::Public;
    else if(m_cursor.matchIdentifier("private"))
        access = AccessSpecifier::Private;
    else if(m_cursor.matchIdentifier("protected"))
        access = AccessSpecifier::Protected;

    if(access != AccessSpecifier::None && m_cursor.check(TokenType::Colon)){
        m_cursor.error("C++-style 'public:' section labels are prohibited in adhd; write the access specifier on each member");
        return false;
    }
    if(m_cursor.checkIdentifier("static")){
        m_cursor.error("'static' members are prohibited in adhd");
        return false;
    }
    if(m_cursor.checkIdentifier("virtual")){
        m_cursor.error("'virtual' is prohibited in adhd; declare contracts in an 'interface' and mark implementations with 'override'");
        return false;
    }
    if(m_cursor.checkIdentifier("class") || m_cursor.checkIdentifier("struct") || m_cursor.checkIdentifier("interface") || m_cursor.checkIdentifier("enum") || m_cursor.checkIdentifier("union") || m_cursor.checkIdentifier("type")){
        m_cursor.error("nested type declarations inside a class are prohibited in adhd");
        return false;
    }
    if(access == AccessSpecifier::None){
        m_cursor.error("every class member in adhd must explicitly specify 'public', 'private', or 'protected'");
        return false;
    }

    if(m_cursor.match(TokenType::Tilde)){
        if(!m_cursor.checkIdentifier(classDecl.name)){
            m_cursor.errorExpected("expected class name after '~' for destructor");
            return false;
        }
        m_cursor.advance();

        MethodDecl* dtor = AllocateAstNode<MethodDecl>(m_cursor.arena());
        dtor->access = access;
        dtor->ownerClassName = classDecl.name;
        dtor->name = classDecl.name;
        dtor->isDestructor = true;
        dtor->span = span;

        if(!applyCallableAttributes(attributes, *dtor))
            return false;
        if(dtor->isReflect)
            classDecl.isReflect = true;

        if(!m_cursor.expect(TokenType::LeftParen, "expected '(' for destructor"))
            return false;
        if(!m_cursor.expect(TokenType::RightParen, "destructor cannot take parameters"))
            return false;
        if(!parseClassMemberCallableBody(*dtor, "expected '{' or ';' after destructor declaration"))
            return false;

        classDecl.methods.push_back(dtor);
        return true;
    }

    if(m_cursor.checkIdentifier(classDecl.name)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        m_cursor.advance();
        if(m_cursor.check(TokenType::LeftParen)){
            MethodDecl* ctor = AllocateAstNode<MethodDecl>(m_cursor.arena());
            ctor->access = access;
            ctor->ownerClassName = classDecl.name;
            ctor->name = classDecl.name;
            ctor->isConstructor = true;
            ctor->span = span;

            if(!applyCallableAttributes(attributes, *ctor))
                return false;
            if(ctor->isReflect)
                classDecl.isReflect = true;

            if(!m_typeParser.parseParamList(ctor->params))
                return false;

            if(m_cursor.check(TokenType::Colon)){
                m_cursor.error("C++ constructor initializer lists (': member(...)') are prohibited in adhd; initialize 'this.member' inside the constructor body");
                return false;
            }
            if(!parseClassMemberCallableBody(*ctor, "expected '{' or ';' after constructor declaration"))
                return false;

            classDecl.methods.push_back(ctor);
            return true;
        }
        m_cursor.restore(cp);
    }

    if(m_cursor.checkIdentifier("operator")){
        m_cursor.error("implicit type-conversion operators are prohibited in adhd; use 'cast<T>(x)' or an explicit named method");
        return false;
    }

    TypeRef* memberType = m_typeParser.parseType(true, false);
    if(!memberType)
        return false;

    if(m_cursor.matchIdentifier("operator")){
        MStringView opName;
        if(!parseOperatorName(opName))
            return false;

        MethodDecl* method = AllocateAstNode<MethodDecl>(m_cursor.arena());
        method->access = access;
        method->ownerClassName = classDecl.name;
        method->returnType = memberType;
        method->name = opName;
        method->isOperator = true;
        method->span = span;

        if(!applyCallableAttributes(attributes, *method))
            return false;
        if(method->isReflect)
            classDecl.isReflect = true;

        if(!m_typeParser.parseParamList(method->params))
            return false;
        MStringView opErr;
        if(!validateMemberOperatorArity(opName, method->params.size(), opErr)){
            m_cursor.error(span.line, span.column, opErr);
            return false;
        }

        parseClassMethodQualifiers(*method);
        if(!parseClassMemberCallableBody(*method, "expected '{' or ';' after operator declaration"))
            return false;

        classDecl.methods.push_back(method);
        return true;
    }

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected member name");
        return false;
    }

    const MStringView memberName = m_cursor.current().text;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Colon)){
        m_cursor.error("C-style bitfields are prohibited in adhd; use 'enum<SizeType>' with 'flag'");
        return false;
    }
    if(m_cursor.check(TokenType::LeftBracket)){
        m_cursor.error("C-style arrays ('T name[N]') are prohibited in adhd; use 'Array<T, N>' or 'Vector<T>'");
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
        const b8 isMethodSig = m_cursor.check(TokenType::RightParen)
            || m_cursor.checkIdentifier("in")
            || m_cursor.checkIdentifier("mut");
        m_cursor.restore(cp);

        if(isMethodSig){
            MethodDecl* method = AllocateAstNode<MethodDecl>(m_cursor.arena());
            method->access = access;
            method->ownerClassName = classDecl.name;
            method->templateParams = Move(methodTemplateParams);
            method->returnType = memberType;
            method->name = memberName;
            method->span = span;

            if(!applyCallableAttributes(attributes, *method))
                return false;
            if(method->isReflect)
                classDecl.isReflect = true;

            if(!m_typeParser.parseParamList(method->params))
                return false;

            parseClassMethodQualifiers(*method);
            if(!parseClassMemberCallableBody(*method, "expected '{' or ';' after method declaration"))
                return false;

            classDecl.methods.push_back(method);
            return true;
        }
    }

    FieldDecl* field = AllocateAstNode<FieldDecl>(m_cursor.arena());
    field->access = access;
    field->type = memberType;
    field->name = memberName;
    field->span = span;
    field->originalIndex = classDecl.fields.size();

    for(const ParsedAttribute& attr : attributes){
        if(attr.name == "transient" && attr.arguments.empty()){
            field->isTransient = true;
        }
        else if(attr.name == "reflect" && attr.arguments.empty()){
            field->isReflect = true;
            classDecl.isReflect = true;
        }
        else{
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
