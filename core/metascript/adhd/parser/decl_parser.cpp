// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decl_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_decl_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ResolveEnumUnderlyingType(const MStringView sizeTypeName, usize& outSize, usize& outAlign)noexcept{
    if(sizeTypeName == "u8" || sizeTypeName == "i8"){
        outSize = 1u;
        outAlign = 1u;
        return true;
    }
    if(sizeTypeName == "u16" || sizeTypeName == "i16"){
        outSize = 2u;
        outAlign = 2u;
        return true;
    }
    if(sizeTypeName == "u32" || sizeTypeName == "i32"){
        outSize = 4u;
        outAlign = 4u;
        return true;
    }
    if(sizeTypeName == "u64" || sizeTypeName == "i64"){
        outSize = 8u;
        outAlign = 8u;
        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DeclParser::parseTranslationUnit(TranslationUnit& outUnit){
    while(!m_cursor.check(TokenType::EndOfFile)){
        if(!parseTopLevelDeclaration(outUnit))
            return false;
    }
    return !m_cursor.failed();
}

bool DeclParser::parseTopLevelDeclaration(TranslationUnit& outUnit){
    MVector<ParsedAttribute> attributes(m_cursor.arena());
    if(m_cursor.check(TokenType::LeftBracket)){
        AttributeParser attrParser(m_cursor);
        if(!attrParser.parseSingleBracketList(attributes))
            return false;
    }

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};

    if(m_cursor.checkIdentifier("namespace")){
        m_cursor.error("explicit 'namespace' declarations are prohibited in adhd; namespaces are derived from file paths");
        return false;
    }
    if(m_cursor.checkIdentifier("struct")){
        m_cursor.error("'struct' is prohibited in adhd; use 'class' (or '[serialize] class' for 1-byte packed binary layout)");
        return false;
    }
    if(m_cursor.checkIdentifier("union")){
        m_cursor.error("'union' is prohibited in adhd");
        return false;
    }
    if(m_cursor.checkIdentifier("static")){
        m_cursor.error("'static' is prohibited in adhd");
        return false;
    }
    if(m_cursor.checkIdentifier("template")){
        m_cursor.error("C++ 'template' keyword is prohibited in adhd; write '<T>' directly on declarations");
        return false;
    }

    if(m_cursor.matchIdentifier("native")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on top-level 'native' blocks");
            return false;
        }
        MStringView code;
        if(!m_stmtParser.parseRawNativeBlock(code))
            return false;
        outUnit.topLevelNativeBlocks.push_back(code);
        return true;
    }

    if(m_cursor.matchIdentifier("const")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on file-level 'const' declarations");
            return false;
        }
        TypeRef* constType = m_typeParser.parseType(false, false);
        if(!constType)
            return false;
        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected("expected constant name");
            return false;
        }
        const MStringView name = m_cursor.current().text;
        m_cursor.advance();
        if(!m_cursor.expect(TokenType::Equal, "expected '=' in file-level 'const' declaration"))
            return false;
        Expr* expr = m_exprParser.parseExpression();
        if(!expr)
            return false;
        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after file-level 'const' declaration"))
            return false;

        Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
        stmt->kind = StmtKind::VarDecl;
        stmt->varType = constType;
        stmt->name = name;
        stmt->expr = expr;
        stmt->isConst = true;
        stmt->span = span;
        outUnit.globalConstants.push_back(stmt);
        return true;
    }

    if(m_cursor.matchIdentifier("type")){
        TypeAliasDecl* aliasDecl = parseTypeAlias(span, Move(attributes));
        if(!aliasDecl)
            return false;
        outUnit.typeAliases.push_back(aliasDecl);
        return true;
    }

    if(m_cursor.matchIdentifier("enum")){
        EnumDecl* enumDecl = parseEnumDecl(span, Move(attributes));
        if(!enumDecl)
            return false;
        outUnit.enums.push_back(enumDecl);
        return true;
    }

    if(m_cursor.matchIdentifier("interface")){
        InterfaceDecl* ifaceDecl = parseInterfaceDecl(span, Move(attributes));
        if(!ifaceDecl)
            return false;
        outUnit.interfaces.push_back(ifaceDecl);
        return true;
    }

    if(m_cursor.matchIdentifier("class")){
        ClassDecl* classDecl = parseClassDecl(span, Move(attributes));
        if(!classDecl)
            return false;
        outUnit.classes.push_back(classDecl);
        return true;
    }

    MethodDecl* callable = parseFreeOrOutOfLineCallable(span, Move(attributes));
    if(!callable)
        return false;

    if(callable->ownerClassName.empty())
        outUnit.freeFunctions.push_back(callable);
    else
        outUnit.outOfLineMethods.push_back(callable);

    return true;
}

TypeAliasDecl* DeclParser::parseTypeAlias(const SourceSpan span, MVector<ParsedAttribute>&& attributes){
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected alias name after 'type'");
        return nullptr;
    }

    TypeAliasDecl* decl = AllocateAstNode<TypeAliasDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->attributes = Move(attributes);
    decl->span = span;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Less)){
        if(!m_typeParser.parseTemplateParamList(decl->templateParams))
            return nullptr;
    }

    if(!m_cursor.expect(TokenType::Equal, "expected '=' in 'type' alias declaration"))
        return nullptr;

    decl->aliasedType = m_typeParser.parseType(false, false);
    if(!decl->aliasedType)
        return nullptr;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'type' alias declaration"))
        return nullptr;

    return decl;
}

EnumDecl* DeclParser::parseEnumDecl(const SourceSpan span, MVector<ParsedAttribute>&& attributes){
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
    decl->attributes = Move(attributes);
    decl->span = span;
    m_cursor.advance();

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start enum body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        bool isFlag = false;
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

    return decl;
}

InterfaceDecl* DeclParser::parseInterfaceDecl(const SourceSpan span, MVector<ParsedAttribute>&& attributes){
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected interface name");
        return nullptr;
    }

    InterfaceDecl* decl = AllocateAstNode<InterfaceDecl>(m_cursor.arena());
    decl->name = m_cursor.current().text;
    decl->attributes = Move(attributes);
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
            decl->baseInterfaces.push_back(m_cursor.current().text);
            m_cursor.advance();
            if(!m_cursor.match(TokenType::Comma))
                break;
        }
    }

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start interface body"))
        return nullptr;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        if(m_cursor.checkIdentifier("public") || m_cursor.checkIdentifier("private") || m_cursor.checkIdentifier("protected")){
            m_cursor.error("interface methods are implicitly public; do not write access specifiers inside 'interface'");
            return nullptr;
        }
        if(m_cursor.checkIdentifier("virtual")){
            m_cursor.error("'virtual' keyword is prohibited in adhd; interface methods are implicitly abstract contracts");
            return nullptr;
        }

        const SourceSpan methodSpan{m_cursor.current().line, m_cursor.current().column};
        TypeRef* retType = m_typeParser.parseType(false, false);
        if(!retType)
            return nullptr;

        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected("expected interface method name");
            return nullptr;
        }

        MethodDecl* method = AllocateAstNode<MethodDecl>(m_cursor.arena());
        method->access = AccessSpecifier::Public;
        method->returnType = retType;
        method->name = m_cursor.current().text;
        method->span = methodSpan;
        m_cursor.advance();

        if(! m_cursor.check(TokenType::LeftParen)){
            m_cursor.error(methodSpan.line, methodSpan.column, "data members are prohibited inside 'interface'; only method contracts are allowed");
            return nullptr;
        }

        if(!parseParamList(method->params))
            return nullptr;

        if(m_cursor.matchIdentifier("const"))
            method->isConst = true;

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

    return decl;
}

ClassDecl* DeclParser::parseClassDecl(const SourceSpan span, MVector<ParsedAttribute>&& attributes){
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
        else{
            m_cursor.error(attr.line, attr.column, "unsupported attribute on class; expected 'serialize', 'reflect', or 'asset(\"...\")'");
            return nullptr;
        }
    }
    decl->attributes = Move(attributes);

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
            decl->implementedInterfaces.push_back(m_cursor.current().text);
            m_cursor.advance();
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

bool DeclParser::parseClassMember(ClassDecl& classDecl){
    MVector<ParsedAttribute> attributes(m_cursor.arena());
    if(m_cursor.check(TokenType::LeftBracket)){
        AttributeParser attrParser(m_cursor);
        if(!attrParser.parseSingleBracketList(attributes))
            return false;
    }

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};

    if(m_cursor.checkIdentifier("class") || m_cursor.checkIdentifier("struct") || m_cursor.checkIdentifier("interface") || m_cursor.checkIdentifier("enum") || m_cursor.checkIdentifier("union")){
        m_cursor.error("nested type declarations inside a class are prohibited in adhd");
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

    AccessSpecifier::Enum access = AccessSpecifier::None;
    if(m_cursor.matchIdentifier("public"))
        access = AccessSpecifier::Public;
    else if(m_cursor.matchIdentifier("private"))
        access = AccessSpecifier::Private;
    else if(m_cursor.matchIdentifier("protected"))
        access = AccessSpecifier::Protected;
    else{
        m_cursor.error("every class member in adhd must explicitly specify 'public', 'private', or 'protected'");
        return false;
    }

    if(m_cursor.check(TokenType::Colon)){
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
    if(m_cursor.checkIdentifier("class") || m_cursor.checkIdentifier("struct") || m_cursor.checkIdentifier("interface") || m_cursor.checkIdentifier("enum") || m_cursor.checkIdentifier("union")){
        m_cursor.error("nested type declarations inside a class are prohibited in adhd");
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
        dtor->attributes = Move(attributes);
        dtor->span = span;

        if(!m_cursor.expect(TokenType::LeftParen, "expected '(' for destructor"))
            return false;
        if(!m_cursor.expect(TokenType::RightParen, "destructor cannot take parameters"))
            return false;

        if(m_cursor.check(TokenType::LeftBrace)){
            dtor->hasBody = true;
            if(!m_stmtParser.parseBlockStatements(dtor->body))
                return false;
        }
        else if(!m_cursor.expect(TokenType::Semicolon, "expected '{' or ';' after destructor declaration"))
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
            ctor->attributes = Move(attributes);
            ctor->span = span;

            if(!parseParamList(ctor->params))
                return false;

            if(m_cursor.check(TokenType::Colon)){
                m_cursor.error("C++ constructor initializer lists (': member(...)') are prohibited in adhd; initialize 'this.member' inside the constructor body");
                return false;
            }

            if(m_cursor.check(TokenType::LeftBrace)){
                ctor->hasBody = true;
                if(!m_stmtParser.parseBlockStatements(ctor->body))
                    return false;
            }
            else if(!m_cursor.expect(TokenType::Semicolon, "expected '{' or ';' after constructor declaration"))
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
        method->attributes = Move(attributes);
        method->span = span;

        if(!parseParamList(method->params))
            return false;
        if((opName == "*" || opName == "&") && method->params.empty()){
            m_cursor.error("unary 'operator*' (dereference) and unary 'operator&' (address-of) overloads are prohibited in adhd");
            return false;
        }

        if(m_cursor.matchIdentifier("const"))
            method->isConst = true;
        if(m_cursor.matchIdentifier("override"))
            method->isOverride = true;

        if(m_cursor.check(TokenType::LeftBrace)){
            method->hasBody = true;
            if(!m_stmtParser.parseBlockStatements(method->body))
                return false;
        }
        else if(!m_cursor.expect(TokenType::Semicolon, "expected '{' or ';' after operator declaration"))
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
        const bool isMethodSig = m_cursor.check(TokenType::RightParen)
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

            for(const ParsedAttribute& attr : attributes){
                if(attr.name == "inline" && attr.arguments.empty())
                    method->isInline = true;
                else if(attr.name == "noinline" && attr.arguments.empty())
                    method->isNoInline = true;
            }
            method->attributes = Move(attributes);

            if(!parseParamList(method->params))
                return false;

            while(m_cursor.checkIdentifier("const") || m_cursor.checkIdentifier("override")){
                if(m_cursor.matchIdentifier("const"))
                    method->isConst = true;
                else if(m_cursor.matchIdentifier("override"))
                    method->isOverride = true;
            }

            if(m_cursor.check(TokenType::LeftBrace)){
                method->hasBody = true;
                if(!m_stmtParser.parseBlockStatements(method->body))
                    return false;
            }
            else if(!m_cursor.expect(TokenType::Semicolon, "expected '{' or ';' after method declaration"))
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
        if(attr.name == "transient" && attr.arguments.empty())
            field->isTransient = true;
    }
    field->attributes = Move(attributes);

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

MethodDecl* DeclParser::parseFreeOrOutOfLineCallable(
    const SourceSpan span,
    MVector<ParsedAttribute>&& attributes
){
    MethodDecl* decl = AllocateAstNode<MethodDecl>(m_cursor.arena());
    decl->span = span;

    for(const ParsedAttribute& attr : attributes){
        if(attr.name == "inline" && attr.arguments.empty())
            decl->isInline = true;
        else if(attr.name == "noinline" && attr.arguments.empty())
            decl->isNoInline = true;
    }
    decl->attributes = Move(attributes);

    if(m_cursor.check(TokenType::Identifier)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        const MStringView maybeClass = m_cursor.current().text;
        m_cursor.advance();

        MVector<TypeArg> ownerTypeArgs(m_cursor.arena());
        if(m_cursor.check(TokenType::Less)){
            (void)m_typeParser.parseTypeArgList(ownerTypeArgs);
        }

        if(m_cursor.match(TokenType::ColonColon)){
            if(m_cursor.match(TokenType::Tilde)){
                if(!m_cursor.checkIdentifier(maybeClass)){
                    m_cursor.errorExpected("expected class name after '~' in out-of-line destructor");
                    return nullptr;
                }
                m_cursor.advance();
                decl->ownerClassName = maybeClass;
                decl->ownerTemplateArgs = Move(ownerTypeArgs);
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
                decl->ownerTemplateArgs = Move(ownerTypeArgs);
                decl->name = maybeClass;
                decl->isConstructor = true;
                if(!parseParamList(decl->params))
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
    }

    decl->returnType = m_typeParser.parseType(false, false);
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

    if(m_cursor.check(TokenType::Less)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        MVector<TypeArg> maybeOwnerArgs(m_cursor.arena());
        if(m_typeParser.parseTypeArgList(maybeOwnerArgs) && m_cursor.check(TokenType::ColonColon)){
            decl->ownerTemplateArgs = Move(maybeOwnerArgs);
        }
        else{
            m_cursor.restore(cp);
            if(!m_typeParser.parseTemplateParamList(decl->templateParams))
                return nullptr;
        }
    }

    if(m_cursor.match(TokenType::ColonColon)){
        decl->ownerClassName = firstName;
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

    if(!parseParamList(decl->params))
        return nullptr;
    if(decl->isOperator && (decl->name == "*" || decl->name == "&") && decl->params.empty()){
        m_cursor.error("unary 'operator*' (dereference) and unary 'operator&' (address-of) overloads are prohibited in adhd");
        return nullptr;
    }

    while(m_cursor.checkIdentifier("const") || m_cursor.checkIdentifier("override")){
        if(m_cursor.matchIdentifier("const"))
            decl->isConst = true;
        else if(m_cursor.matchIdentifier("override"))
            decl->isOverride = true;
    }

    if(m_cursor.check(TokenType::LeftBrace)){
        decl->hasBody = true;
        if(!m_stmtParser.parseBlockStatements(decl->body))
            return nullptr;
    }
    else if(!m_cursor.expect(TokenType::Semicolon, "expected '{' or ';' after function declaration"))
        return nullptr;

    return decl;
}

bool DeclParser::parseParamList(MVector<ParamDecl>& outParams){
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' for parameter list"))
        return false;

    if(!m_cursor.check(TokenType::RightParen)){
        for(;;){
            ParamDecl param;
            param.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};

            if(m_cursor.matchIdentifier("in"))
                param.mode = ParamPassingMode::In;
            else if(m_cursor.matchIdentifier("mut"))
                param.mode = ParamPassingMode::Mut;
            else{
                m_cursor.error("every parameter in adhd must explicitly specify 'in' or 'mut'");
                return false;
            }

            param.type = m_typeParser.parseType(false, false);
            if(!param.type)
                return false;

            if(!m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected parameter name");
                return false;
            }
            param.name = m_cursor.current().text;
            m_cursor.advance();

            if(m_cursor.check(TokenType::LeftBracket)){
                m_cursor.error("C-style arrays ('T name[N]') are prohibited in adhd; use 'Array<T, N>' or 'Span<T>'");
                return false;
            }
            if(m_cursor.check(TokenType::Equal)){
                m_cursor.error("default parameter arguments are prohibited in adhd");
                return false;
            }

            outParams.push_back(param);

            if(m_cursor.check(TokenType::RightParen))
                break;
            if(!m_cursor.expect(TokenType::Comma, "expected ',' or ')' in parameter list"))
                return false;
        }
    }

    return m_cursor.expect(TokenType::RightParen, "expected ')' after parameter list");
}

bool DeclParser::parseOperatorName(MStringView& outOpName){
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

