// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stmt_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool StmtParser::parseBlockStatements(MVector<Stmt*>& outStatements){
    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to begin statement block"))
        return false;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        Stmt* stmt = parseStatement();
        if(!stmt)
            return false;
        outStatements.push_back(stmt);
    }

    return m_cursor.expect(TokenType::RightBrace, "expected '}' to close statement block");
}

Stmt* StmtParser::parseStatement(){
    MVector<ParsedAttribute> attributes(m_cursor.arena());
    if(m_cursor.check(TokenType::LeftBracket)){
        AttributeParser attrParser(m_cursor);
        if(!attrParser.parseSingleBracketList(attributes))
            return nullptr;
    }

    const SourceSpan span{m_cursor.current().line, m_cursor.current().column};

    if(m_cursor.check(TokenType::LeftBrace)){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on a plain block statement");
            return nullptr;
        }
        Stmt* block = AllocateAstNode<Stmt>(m_cursor.arena());
        block->kind = StmtKind::Block;
        block->span = span;
        if(!parseBlockStatements(block->statements))
            return nullptr;
        return block;
    }

    if(m_cursor.checkIdentifier("try") || m_cursor.checkIdentifier("catch") || m_cursor.checkIdentifier("throw")){
        m_cursor.error("C++ exceptions ('try'/'catch'/'throw') are prohibited in adhd; use Optional<T> or Expected<T, E>");
        return nullptr;
    }
    if(m_cursor.checkIdentifier("goto")){
        m_cursor.error("'goto' is prohibited in adhd");
        return nullptr;
    }
    if(m_cursor.checkIdentifier("static")){
        m_cursor.error("'static' variables are prohibited in adhd");
        return nullptr;
    }
    if(m_cursor.checkIdentifier("class") || m_cursor.checkIdentifier("struct") || m_cursor.checkIdentifier("interface") || m_cursor.checkIdentifier("enum") || m_cursor.checkIdentifier("union")){
        m_cursor.error("local or nested type declarations are prohibited in adhd; declare types at top-level file scope");
        return nullptr;
    }

    if(m_cursor.matchIdentifier("if")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'if' statements");
            return nullptr;
        }
        return parseIfStatement(span);
    }

    if(m_cursor.matchIdentifier("switch")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'switch' statements");
            return nullptr;
        }
        return parseSwitchStatement(span);
    }

    if(m_cursor.matchIdentifier("for"))
        return parseForOrParallelFor(span, false, Move(attributes));

    if(m_cursor.matchIdentifier("parallel_for"))
        return parseForOrParallelFor(span, true, Move(attributes));

    if(m_cursor.matchIdentifier("while")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'while' statements");
            return nullptr;
        }
        return parseWhileStatement(span);
    }

    if(m_cursor.matchIdentifier("do")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'do-while' statements");
            return nullptr;
        }
        return parseDoWhileStatement(span);
    }

    if(m_cursor.matchIdentifier("break")){
        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'break'"))
            return nullptr;
        Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
        stmt->kind = StmtKind::Break;
        stmt->span = span;
        return stmt;
    }

    if(m_cursor.matchIdentifier("continue")){
        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'continue'"))
            return nullptr;
        Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
        stmt->kind = StmtKind::Continue;
        stmt->span = span;
        return stmt;
    }

    if(m_cursor.matchIdentifier("return")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'return' statements");
            return nullptr;
        }
        return parseReturnStatement(span);
    }

    if(m_cursor.matchIdentifier("parallel"))
        return parseParallelStatement(span, Move(attributes));

    if(m_cursor.matchIdentifier("native")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'native' blocks");
            return nullptr;
        }
        return parseNativeStatement(span);
    }

    if(m_cursor.matchIdentifier("assert"))
        return parseDiagnosticStatement(span, DiagnosticKind::Assert, Move(attributes));
    if(m_cursor.matchIdentifier("info"))
        return parseDiagnosticStatement(span, DiagnosticKind::Info, Move(attributes));
    if(m_cursor.matchIdentifier("warning"))
        return parseDiagnosticStatement(span, DiagnosticKind::Warning, Move(attributes));
    if(m_cursor.matchIdentifier("error"))
        return parseDiagnosticStatement(span, DiagnosticKind::Error, Move(attributes));

    if(m_cursor.matchIdentifier("const")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on local 'const' declarations");
            return nullptr;
        }
        return parseConstDeclaration(span);
    }

    if(!attributes.empty()){
        m_cursor.error(span.line, span.column, "unexpected attributes before statement");
        return nullptr;
    }

    if(m_cursor.check(TokenType::Identifier)){
        const ParserCursor::Checkpoint cp = m_cursor.save();
        TypeRef* maybeType = m_typeParser.parseType(true, true);
        if(maybeType && m_cursor.check(TokenType::Identifier) && !m_cursor.checkIdentifier("as"))
            return parseVarDeclAfterType(maybeType, span);
        m_cursor.restore(cp);
    }

    Expr* expr = m_exprParser.parseExpression();
    if(!expr)
        return nullptr;
    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after expression"))
        return nullptr;

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::ExprStmt;
    stmt->expr = expr;
    stmt->span = span;
    return stmt;
}

bool StmtParser::parseRawNativeBlock(MStringView& outCode){
    if(!m_cursor.check(TokenType::LeftBrace)){
        m_cursor.errorExpected("expected '{' to start native block");
        return false;
    }

    const MChar* openBraceEnd = m_cursor.current().text.data() + m_cursor.current().text.size();
    m_cursor.advance();

    u32 braceDepth = 1u;
    const MChar* closeBraceStart = openBraceEnd;

    while(!m_cursor.check(TokenType::EndOfFile)){
        if(m_cursor.check(TokenType::LeftBrace)){
            ++braceDepth;
        }
        else if(m_cursor.check(TokenType::RightBrace)){
            --braceDepth;
            if(braceDepth == 0u){
                closeBraceStart = m_cursor.current().text.data();
                m_cursor.advance();
                outCode = MStringView(openBraceEnd, static_cast<usize>(closeBraceStart - openBraceEnd));
                return true;
            }
        }
        m_cursor.advance();
    }

    m_cursor.error("unterminated 'native' block; expected '}'");
    return false;
}

Stmt* StmtParser::parseIfStatement(const SourceSpan span){
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after 'if'"))
        return nullptr;

    Expr* cond = m_exprParser.parseExpression();
    if(!cond)
        return nullptr;

    if(!m_cursor.expect(TokenType::RightParen, "expected ')' after 'if' condition"))
        return nullptr;

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::If;
    stmt->condition = cond;
    stmt->span = span;

    if(m_cursor.check(TokenType::LeftBrace)){
        if(!parseBlockStatements(stmt->statements))
            return nullptr;
    }
    else{
        Stmt* thenStmt = parseStatement();
        if(!thenStmt)
            return nullptr;
        stmt->statements.push_back(thenStmt);
    }

    if(m_cursor.matchIdentifier("else")){
        if(m_cursor.check(TokenType::LeftBrace)){
            if(!parseBlockStatements(stmt->elseStatements))
                return nullptr;
        }
        else{
            Stmt* elseStmt = parseStatement();
            if(!elseStmt)
                return nullptr;
            stmt->elseStatements.push_back(elseStmt);
        }
    }

    return stmt;
}

Stmt* StmtParser::parseSwitchStatement(const SourceSpan span){
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after 'switch'"))
        return nullptr;

    Expr* subject = m_exprParser.parseExpression();
    if(!subject)
        return nullptr;

    if(!m_cursor.expect(TokenType::RightParen, "expected ')' after 'switch' expression"))
        return nullptr;

    if(!m_cursor.expect(TokenType::LeftBrace, "expected '{' to start 'switch' body"))
        return nullptr;

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::Switch;
    stmt->expr = subject;
    stmt->span = span;

    while(!m_cursor.check(TokenType::RightBrace) && !m_cursor.check(TokenType::EndOfFile)){
        SwitchCase switchCase(m_cursor.arena());
        switchCase.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};

        if(m_cursor.matchIdentifier("case")){
            switchCase.matchExpr = m_exprParser.parseExpression();
            if(!switchCase.matchExpr)
                return nullptr;
            if(!m_cursor.expect(TokenType::Colon, "expected ':' after 'case' value"))
                return nullptr;
        }
        else if(m_cursor.matchIdentifier("default")){
            switchCase.isDefault = true;
            if(!m_cursor.expect(TokenType::Colon, "expected ':' after 'default'"))
                return nullptr;
        }
        else{
            m_cursor.errorExpected("expected 'case' or 'default' inside 'switch'");
            return nullptr;
        }

        if(m_cursor.check(TokenType::LeftBrace)){
            if(!parseBlockStatements(switchCase.statements))
                return nullptr;
        }
        else{
            while(!m_cursor.check(TokenType::RightBrace)
                && !m_cursor.check(TokenType::EndOfFile)
                && !m_cursor.checkIdentifier("case")
                && !m_cursor.checkIdentifier("default"))
            {
                Stmt* caseStmt = parseStatement();
                if(!caseStmt)
                    return nullptr;
                switchCase.statements.push_back(caseStmt);
            }
        }

        const bool isStackedEmptyLabel = switchCase.statements.empty()
            && (m_cursor.checkIdentifier("case") || m_cursor.checkIdentifier("default"));
        if(!isStackedEmptyLabel){
            if(switchCase.statements.empty()){
                m_cursor.error(switchCase.span.line, switchCase.span.column, "'switch' case cannot be empty without 'break' or 'return'");
                return nullptr;
            }
            const StmtKind::Enum lastKind = switchCase.statements.back()->kind;
            if(lastKind != StmtKind::Break && lastKind != StmtKind::Return){
                m_cursor.error(switchCase.span.line, switchCase.span.column, "implicit fallthrough in 'switch' is prohibited in adhd; end each case with 'break;' or 'return'");
                return nullptr;
            }
        }

        stmt->switchCases.push_back(Move(switchCase));
    }

    if(!m_cursor.expect(TokenType::RightBrace, "expected '}' to close 'switch' statement"))
        return nullptr;

    return stmt;
}

Stmt* StmtParser::parseForOrParallelFor(const SourceSpan span, const bool isParallel, MVector<ParsedAttribute>&& attributes){
    ParallelCoreKind::Enum coreKind = ParallelCoreKind::Any;
    ParallelPriorityKind::Enum priorityKind = ParallelPriorityKind::Normal;
    if(isParallel){
        if(!applySchedulingAttributes(attributes, coreKind, priorityKind))
            return nullptr;
    }
    else if(!attributes.empty()){
        m_cursor.error(span.line, span.column, "scheduling attributes are only permitted on 'parallel_for' or 'parallel'");
        return nullptr;
    }

    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after loop keyword"))
        return nullptr;

    ParamPassingMode::Enum rangeMode = ParamPassingMode::None;
    if(m_cursor.matchIdentifier("in"))
        rangeMode = ParamPassingMode::In;
    else if(m_cursor.matchIdentifier("mut"))
        rangeMode = ParamPassingMode::Mut;

    if(rangeMode != ParamPassingMode::None){
        TypeRef* elemType = m_typeParser.parseType(false, true);
        if(!elemType)
            return nullptr;
        if(!m_cursor.check(TokenType::Identifier)){
            m_cursor.errorExpected("expected loop variable name");
            return nullptr;
        }
        const MStringView varName = m_cursor.current().text;
        m_cursor.advance();

        if(!m_cursor.expect(TokenType::Colon, "expected ':' in range-based loop"))
            return nullptr;

        Expr* rangeExpr = m_exprParser.parseExpression();
        if(!rangeExpr)
            return nullptr;

        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after range expression"))
            return nullptr;

        Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
        stmt->kind = isParallel ? StmtKind::ParallelForRange : StmtKind::ForRange;
        stmt->span = span;
        stmt->attributes = Move(attributes);
        stmt->rangeMode = rangeMode;
        stmt->varType = elemType;
        stmt->name = varName;
        stmt->expr = rangeExpr;
        stmt->parallelCore = coreKind;
        stmt->parallelPriority = priorityKind;

        if(m_cursor.check(TokenType::LeftBrace)){
            if(!parseBlockStatements(stmt->statements))
                return nullptr;
        }
        else{
            Stmt* bodyStmt = parseStatement();
            if(!bodyStmt)
                return nullptr;
            stmt->statements.push_back(bodyStmt);
        }
        return stmt;
    }

    TypeRef* indexType = m_typeParser.parseType(false, true);
    if(!indexType)
        return nullptr;

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected loop variable name");
        return nullptr;
    }
    const MStringView indexName = m_cursor.current().text;
    m_cursor.advance();

    if(m_cursor.check(TokenType::Colon)){
        m_cursor.error("range-based loop variable must explicitly specify 'in' or 'mut' in adhd");
        return nullptr;
    }

    if(!m_cursor.expect(TokenType::Equal, "expected '=' to initialize index loop variable"))
        return nullptr;

    Expr* initExpr = m_exprParser.parseExpression();
    if(!initExpr)
        return nullptr;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after loop initializer"))
        return nullptr;

    Expr* condExpr = m_exprParser.parseExpression();
    if(!condExpr)
        return nullptr;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after loop condition"))
        return nullptr;

    Expr* stepExpr = m_exprParser.parseExpression();
    if(!stepExpr)
        return nullptr;

    if(!m_cursor.expect(TokenType::RightParen, "expected ')' after loop step expression"))
        return nullptr;

    Stmt* initStmt = AllocateAstNode<Stmt>(m_cursor.arena());
    initStmt->kind = StmtKind::VarDecl;
    initStmt->varType = indexType;
    initStmt->name = indexName;
    initStmt->expr = initExpr;
    initStmt->span = span;

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = isParallel ? StmtKind::ParallelForIndex : StmtKind::ForIndex;
    stmt->span = span;
    stmt->attributes = Move(attributes);
    stmt->initStmt = initStmt;
    stmt->condition = condExpr;
    stmt->stepExpr = stepExpr;
    stmt->parallelCore = coreKind;
    stmt->parallelPriority = priorityKind;

    if(m_cursor.check(TokenType::LeftBrace)){
        if(!parseBlockStatements(stmt->statements))
            return nullptr;
    }
    else{
        Stmt* bodyStmt = parseStatement();
        if(!bodyStmt)
            return nullptr;
        stmt->statements.push_back(bodyStmt);
    }

    return stmt;
}

Stmt* StmtParser::parseWhileStatement(const SourceSpan span){
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after 'while'"))
        return nullptr;

    Expr* cond = m_exprParser.parseExpression();
    if(!cond)
        return nullptr;

    if(!m_cursor.expect(TokenType::RightParen, "expected ')' after 'while' condition"))
        return nullptr;

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::While;
    stmt->condition = cond;
    stmt->span = span;

    if(m_cursor.check(TokenType::LeftBrace)){
        if(!parseBlockStatements(stmt->statements))
            return nullptr;
    }
    else{
        Stmt* bodyStmt = parseStatement();
        if(!bodyStmt)
            return nullptr;
        stmt->statements.push_back(bodyStmt);
    }

    return stmt;
}

Stmt* StmtParser::parseDoWhileStatement(const SourceSpan span){
    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::DoWhile;
    stmt->span = span;

    if(m_cursor.check(TokenType::LeftBrace)){
        if(!parseBlockStatements(stmt->statements))
            return nullptr;
    }
    else{
        Stmt* bodyStmt = parseStatement();
        if(!bodyStmt)
            return nullptr;
        stmt->statements.push_back(bodyStmt);
    }

    if(!m_cursor.matchIdentifier("while")){
        m_cursor.errorExpected("expected 'while' after 'do' body");
        return nullptr;
    }

    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after 'while' in do-while"))
        return nullptr;

    stmt->condition = m_exprParser.parseExpression();
    if(!stmt->condition)
        return nullptr;

    if(!m_cursor.expect(TokenType::RightParen, "expected ')' after do-while condition"))
        return nullptr;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after do-while statement"))
        return nullptr;

    return stmt;
}

Stmt* StmtParser::parseReturnStatement(const SourceSpan span){
    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::Return;
    stmt->span = span;

    if(!m_cursor.match(TokenType::Semicolon)){
        stmt->expr = m_exprParser.parseExpression();
        if(!stmt->expr)
            return nullptr;
        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after return value"))
            return nullptr;
    }

    return stmt;
}

Stmt* StmtParser::parseParallelStatement(const SourceSpan span, MVector<ParsedAttribute>&& attributes){
    ParallelCoreKind::Enum coreKind = ParallelCoreKind::Any;
    ParallelPriorityKind::Enum priorityKind = ParallelPriorityKind::Normal;
    if(!applySchedulingAttributes(attributes, coreKind, priorityKind))
        return nullptr;

    const ParserCursor::Checkpoint cp = m_cursor.save();
    if(m_cursor.check(TokenType::Identifier) && !m_cursor.checkIdentifier("this")){
        const MStringView maybeTaskName = m_cursor.current().text;
        m_cursor.advance();

        if(m_cursor.check(TokenType::Equal)){
            m_cursor.error("named assignment in 'parallel' call is prohibited in adhd; use 'parallel func(args...);' or 'parallel TaskName { ... }'");
            return nullptr;
        }

        if(m_cursor.check(TokenType::Colon) || m_cursor.check(TokenType::LeftBrace)){
            Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
            stmt->kind = StmtKind::ParallelBlock;
            stmt->name = maybeTaskName;
            stmt->span = span;
            stmt->attributes = Move(attributes);
            stmt->parallelCore = coreKind;
            stmt->parallelPriority = priorityKind;

            if(m_cursor.match(TokenType::Colon)){
                for(;;){
                    if(!m_cursor.check(TokenType::Identifier)){
                        m_cursor.errorExpected("expected prerequisite task name after ':'");
                        return nullptr;
                    }
                    stmt->parallelDependencies.push_back(m_cursor.current().text);
                    m_cursor.advance();
                    if(!m_cursor.match(TokenType::Comma))
                        break;
                }
            }

            if(!parseBlockStatements(stmt->statements))
                return nullptr;
            return stmt;
        }
    }
    m_cursor.restore(cp);

    Expr* callExpr = m_exprParser.parseExpression();
    if(!callExpr)
        return nullptr;
    if(callExpr->kind != ExprKind::Call){
        m_cursor.error(span.line, span.column, "expected callable invocation 'parallel func(args...);' or named block 'parallel TaskName { ... }'");
        return nullptr;
    }
    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'parallel' callable dispatch"))
        return nullptr;

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::ParallelCall;
    stmt->expr = callExpr;
    stmt->span = span;
    stmt->attributes = Move(attributes);
    stmt->parallelCore = coreKind;
    stmt->parallelPriority = priorityKind;
    return stmt;
}

Stmt* StmtParser::parseNativeStatement(const SourceSpan span){
    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::NativeBlock;
    stmt->span = span;

    if(m_cursor.match(TokenType::LeftParen)){
        if(!m_cursor.check(TokenType::RightParen)){
            for(;;){
                NativeBinding binding;
                binding.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};
                if(m_cursor.matchIdentifier("in"))
                    binding.mode = ParamPassingMode::In;
                else if(m_cursor.matchIdentifier("mut"))
                    binding.mode = ParamPassingMode::Mut;
                else{
                    m_cursor.error("native block binding must explicitly specify 'in' or 'mut'");
                    return nullptr;
                }

                if(!m_cursor.check(TokenType::Identifier)){
                    m_cursor.errorExpected("expected binding identifier in native(...)");
                    return nullptr;
                }
                binding.aliasName = m_cursor.current().text;
                m_cursor.advance();

                if(m_cursor.match(TokenType::Equal)){
                    binding.sourceExpr = m_exprParser.parseExpression();
                    if(!binding.sourceExpr)
                        return nullptr;
                }
                else{
                    Expr* selfId = AllocateAstNode<Expr>(m_cursor.arena());
                    selfId->kind = ExprKind::Identifier;
                    selfId->text = binding.aliasName;
                    selfId->span = binding.span;
                    binding.sourceExpr = selfId;
                }

                stmt->nativeBindings.push_back(binding);

                if(m_cursor.check(TokenType::RightParen))
                    break;
                if(!m_cursor.expect(TokenType::Comma, "expected ',' or ')' in native(...) binding list"))
                    return nullptr;
            }
        }
        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after native(...) bindings"))
            return nullptr;
    }

    if(!parseRawNativeBlock(stmt->nativeCode))
        return nullptr;

    return stmt;
}

Stmt* StmtParser::parseDiagnosticStatement(const SourceSpan span, const DiagnosticKind::Enum kind, MVector<ParsedAttribute>&& attributes){
    bool isEssential = false;
    for(const ParsedAttribute& attr : attributes){
        if(attr.name == "essential" && attr.arguments.empty()){
            isEssential = true;
        }
        else{
            m_cursor.error(attr.line, attr.column, "only '[essential]' is permitted on diagnostic statements");
            return nullptr;
        }
    }

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::Diagnostic;
    stmt->diagnosticKind = kind;
    stmt->isEssential = isEssential;
    stmt->attributes = Move(attributes);
    stmt->span = span;

    if(!m_exprParser.parseArgumentList(stmt->arguments))
        return nullptr;

    if(stmt->arguments.empty()){
        m_cursor.error(span.line, span.column, "diagnostic statement requires at least one argument");
        return nullptr;
    }

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after diagnostic statement"))
        return nullptr;

    return stmt;
}

Stmt* StmtParser::parseConstDeclaration(const SourceSpan span){
    TypeRef* varType = m_typeParser.parseType(false, false);
    if(!varType)
        return nullptr;

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected constant name after type");
        return nullptr;
    }
    const MStringView name = m_cursor.current().text;
    m_cursor.advance();

    if(!m_cursor.expect(TokenType::Equal, "expected '=' to initialize 'const' value"))
        return nullptr;

    Expr* initExpr = m_exprParser.parseExpression();
    if(!initExpr)
        return nullptr;

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'const' declaration"))
        return nullptr;

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::VarDecl;
    stmt->varType = varType;
    stmt->name = name;
    stmt->expr = initExpr;
    stmt->isConst = true;
    stmt->span = span;
    return stmt;
}

Stmt* StmtParser::parseVarDeclAfterType(TypeRef* varType, const SourceSpan span){
    const MStringView varName = m_cursor.current().text;
    m_cursor.advance();

    if(m_cursor.check(TokenType::LeftBracket)){
        m_cursor.error(span.line, span.column, "C-style arrays ('T name[N]') are prohibited in adhd; use 'Array<T, N>' or 'Vector<T>'");
        return nullptr;
    }

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::VarDecl;
    stmt->varType = varType;
    stmt->name = varName;
    stmt->span = span;

    if(m_cursor.match(TokenType::Equal)){
        stmt->expr = m_exprParser.parseExpression();
        if(!stmt->expr)
            return nullptr;
    }
    else if(m_cursor.check(TokenType::LeftParen)){
        if(varType->isAuto){
            m_cursor.error(span.line, span.column, "'auto' variable must be initialized with '= expr'");
            return nullptr;
        }
        stmt->isCtorInit = true;
        if(!m_exprParser.parseArgumentList(stmt->arguments))
            return nullptr;
    }
    else if(m_cursor.check(TokenType::Semicolon)){
        m_cursor.error(span.line, span.column, "uninitialized variable declaration is prohibited in adhd; every local variable must be initialized at declaration");
        return nullptr;
    }
    else{
        m_cursor.errorExpected("expected '=' or '(' to initialize local variable");
        return nullptr;
    }

    if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after variable declaration"))
        return nullptr;

    return stmt;
}

bool StmtParser::applySchedulingAttributes(const MVector<ParsedAttribute>& attributes, ParallelCoreKind::Enum& outCore, ParallelPriorityKind::Enum& outPriority){
    for(const ParsedAttribute& attr : attributes){
        if(attr.name == "core"){
            if(attr.arguments.size() != 1u){
                m_cursor.error(attr.line, attr.column, "'core' attribute requires exactly one argument (Heavy, Light, MainThread, or Any)");
                return false;
            }
            const MStringView arg = attr.arguments[0].text;
            if(arg == "Heavy")
                outCore = ParallelCoreKind::Heavy;
            else if(arg == "Light")
                outCore = ParallelCoreKind::Light;
            else if(arg == "MainThread")
                outCore = ParallelCoreKind::MainThread;
            else if(arg == "Any")
                outCore = ParallelCoreKind::Any;
            else{
                m_cursor.error(attr.line, attr.column, "invalid 'core' attribute value; expected Heavy, Light, MainThread, or Any");
                return false;
            }
        }
        else if(attr.name == "priority"){
            if(attr.arguments.size() != 1u){
                m_cursor.error(attr.line, attr.column, "'priority' attribute requires exactly one argument (Critical, Normal, or Background)");
                return false;
            }
            const MStringView arg = attr.arguments[0].text;
            if(arg == "Critical")
                outPriority = ParallelPriorityKind::Critical;
            else if(arg == "Normal")
                outPriority = ParallelPriorityKind::Normal;
            else if(arg == "Background")
                outPriority = ParallelPriorityKind::Background;
            else{
                m_cursor.error(attr.line, attr.column, "invalid 'priority' attribute value; expected Critical, Normal, or Background");
                return false;
            }
        }
        else{
            m_cursor.error(attr.line, attr.column, "unsupported attribute on parallel statement; only 'core(...)' and 'priority(...)' are permitted");
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

