// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stmt_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_stmt_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 CaseBranchTerminates(const Stmt* stmt)noexcept{
    if(!stmt)
        return false;
    if(stmt->kind == StmtKind::Break || stmt->kind == StmtKind::Return || stmt->kind == StmtKind::Continue)
        return true;
    if(stmt->kind == StmtKind::Block)
        return !stmt->statements.empty() && CaseBranchTerminates(stmt->statements.back());
    if(stmt->kind == StmtKind::If){
        return !stmt->statements.empty()
            && !stmt->elseStatements.empty()
            && CaseBranchTerminates(stmt->statements.back())
            && CaseBranchTerminates(stmt->elseStatements.back());
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 StmtParser::parseBlockStatements(MVector<Stmt*>& outStatements){
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

#define NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD(keyword, errorMessage) \
    if(m_cursor.checkIdentifier(keyword)){ \
        m_cursor.error(errorMessage); \
        return nullptr; \
    }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD

#define NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD(keyword, isClassNestedKeyword) \
    if(m_cursor.checkIdentifier(keyword)){ \
        m_cursor.error("local or nested type declarations are prohibited in adhd; declare types at top-level file scope"); \
        return nullptr; \
    }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD

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
        return parseForOrParallelFor(span, false, attributes);

    if(m_cursor.matchIdentifier("parallel_for"))
        return parseForOrParallelFor(span, true, attributes);

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
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'break' statements");
            return nullptr;
        }
        if(!m_cursor.expect(TokenType::Semicolon, "expected ';' after 'break'"))
            return nullptr;
        Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
        stmt->kind = StmtKind::Break;
        stmt->span = span;
        return stmt;
    }

    if(m_cursor.matchIdentifier("continue")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'continue' statements");
            return nullptr;
        }
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
        return parseParallelStatement(span, attributes);

    if(m_cursor.matchIdentifier("native")){
        if(!attributes.empty()){
            m_cursor.error(span.line, span.column, "attributes are not permitted on 'native' blocks");
            return nullptr;
        }
        return parseNativeStatement(span);
    }

#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, astEnum, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
    if(m_cursor.matchIdentifier(adhdKeyword)) \
        return parseDiagnosticStatement(span, astEnum, attributes);
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND

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
        if(maybeType && !m_typeParser.hasPendingRightAngles() && m_cursor.check(TokenType::Identifier) && !m_cursor.checkIdentifier("as"))
            return parseVarDeclAfterType(maybeType, span);
        m_cursor.restore(cp);
        m_typeParser.resetPendingRightAngles();
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

b8 StmtParser::parseRawNativeBlock(MStringView& outCode){
    return m_cursor.consumeBalancedBracesRaw(outCode);
}

b8 StmtParser::parseScopedBodyStatements(MVector<Stmt*>& outStatements){
    if(m_cursor.check(TokenType::LeftBrace))
        return parseBlockStatements(outStatements);

    Stmt* bodyStmt = parseStatement();
    if(!bodyStmt)
        return false;
    outStatements.push_back(bodyStmt);
    return true;
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

    if(!parseScopedBodyStatements(stmt->statements))
        return nullptr;

    if(m_cursor.matchIdentifier("else")){
        if(!parseScopedBodyStatements(stmt->elseStatements))
            return nullptr;
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

    b8 hasDefault = false;
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
            if(hasDefault){
                m_cursor.error(switchCase.span.line, switchCase.span.column, "duplicate 'default' label in 'switch' statement");
                return nullptr;
            }
            hasDefault = true;
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

        const b8 isStackedEmptyLabel = switchCase.statements.empty()
            && (m_cursor.checkIdentifier("case") || m_cursor.checkIdentifier("default"));
        if(!isStackedEmptyLabel){
            if(switchCase.statements.empty()){
                m_cursor.error(switchCase.span.line, switchCase.span.column, "'switch' case cannot be empty without 'break' or 'return'");
                return nullptr;
            }
            if(!__hidden_adhd_stmt_parser::CaseBranchTerminates(switchCase.statements.back())){
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

Stmt* StmtParser::parseForOrParallelFor(const SourceSpan span, const b8 isParallel, const MVector<ParsedAttribute>& attributes){
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
        stmt->rangeMode = rangeMode;
        stmt->varType = elemType;
        stmt->name = varName;
        stmt->expr = rangeExpr;
        stmt->parallelCore = coreKind;
        stmt->parallelPriority = priorityKind;

        if(!parseScopedBodyStatements(stmt->statements))
            return nullptr;
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
    stmt->initStmt = initStmt;
    stmt->condition = condExpr;
    stmt->stepExpr = stepExpr;
    stmt->parallelCore = coreKind;
    stmt->parallelPriority = priorityKind;

    if(!parseScopedBodyStatements(stmt->statements))
        return nullptr;

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

    if(!parseScopedBodyStatements(stmt->statements))
        return nullptr;

    return stmt;
}

Stmt* StmtParser::parseDoWhileStatement(const SourceSpan span){
    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::DoWhile;
    stmt->span = span;

    if(!parseScopedBodyStatements(stmt->statements))
        return nullptr;

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

