// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "expr_parser.h"

#include "stmt_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_expr_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsAssignmentOperator(const TokenType::Enum type)noexcept{
    return type == TokenType::Equal
        || type == TokenType::PlusEqual
        || type == TokenType::MinusEqual
        || type == TokenType::StarEqual
        || type == TokenType::SlashEqual
        || type == TokenType::PercentEqual
        || type == TokenType::AmpersandEqual
        || type == TokenType::PipeEqual
        || type == TokenType::CaretEqual
        || type == TokenType::LessLessEqual
        || type == TokenType::GreaterGreaterEqual;
}

[[nodiscard]] Expr* MakeBinaryExpr(MetaArena& arena, const Token& opTok, Expr* lhs, Expr* rhs){
    Expr* node = AllocateAstNode<Expr>(arena);
    node->kind = ExprKind::Binary;
    node->op = opTok.type;
    node->text = opTok.text;
    node->lhs = lhs;
    node->rhs = rhs;
    node->span = SourceSpan{opTok.line, opTok.column};
    return node;
}

[[nodiscard]] Expr* MakeTokenExpr(MetaArena& arena, const ExprKind::Enum kind, const Token& tok, const SourceSpan span){
    Expr* node = AllocateAstNode<Expr>(arena);
    node->kind = kind;
    node->text = tok.text;
    node->span = span;
    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expr* ExprParser::parseExpression(){
    return parseAssignment();
}

bool ExprParser::parseArgumentList(MVector<Expr*>& outArgs){
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' before argument list"))
        return false;

    if(!m_cursor.check(TokenType::RightParen)){
        for(;;){
            if((m_cursor.checkIdentifier("in") || m_cursor.checkIdentifier("mut"))){
                m_cursor.error("do not write 'in' or 'mut' at call sites in adhd");
                return false;
            }
            Expr* arg = parseExpression();
            if(!arg)
                return false;
            outArgs.push_back(arg);

            if(m_cursor.check(TokenType::RightParen))
                break;
            if(!m_cursor.expect(TokenType::Comma, "expected ',' or ')' in argument list"))
                return false;
        }
    }

    return m_cursor.expect(TokenType::RightParen, "expected ')' after argument list");
}

Expr* ExprParser::parseAssignment(){
    Expr* lhs = parseLogicalOr();
    if(!lhs)
        return nullptr;

    if(m_cursor.check(TokenType::Question)){
        m_cursor.error("ternary operator '? :' is prohibited in adhd; use 'if' / 'else' statements instead");
        return nullptr;
    }

    if(m_cursor.checkIdentifier("as")){
        m_cursor.error("Rust-style 'as' cast is prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'");
        return nullptr;
    }

    if(__hidden_adhd_expr_parser::IsAssignmentOperator(m_cursor.current().type)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* rhs = parseAssignment();
        if(!rhs)
            return nullptr;

        return __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, lhs, rhs);
    }

    return lhs;
}

Expr* ExprParser::parseLogicalOr(){
    Expr* left = parseLogicalAnd();
    while(left && m_cursor.check(TokenType::PipePipe)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseLogicalAnd();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseLogicalAnd(){
    Expr* left = parseBitwiseOr();
    while(left && m_cursor.check(TokenType::AmpersandAmpersand)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseBitwiseOr();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseBitwiseOr(){
    Expr* left = parseBitwiseXor();
    while(left && m_cursor.check(TokenType::Pipe)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseBitwiseXor();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseBitwiseXor(){
    Expr* left = parseBitwiseAnd();
    while(left && m_cursor.check(TokenType::Caret)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseBitwiseAnd();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseBitwiseAnd(){
    Expr* left = parseEquality();
    while(left && m_cursor.check(TokenType::Ampersand)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseEquality();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseEquality(){
    Expr* left = parseRelational();
    while(left && (m_cursor.check(TokenType::EqualEqual) || m_cursor.check(TokenType::BangEqual))){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseRelational();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseRelational(){
    Expr* left = parseShift();
    while(left && (m_cursor.check(TokenType::Less) || m_cursor.check(TokenType::LessEqual) || m_cursor.check(TokenType::Greater) || m_cursor.check(TokenType::GreaterEqual))){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseShift();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseShift(){
    Expr* left = parseAdditive();
    while(left && (m_cursor.check(TokenType::LessLess) || m_cursor.check(TokenType::GreaterGreater))){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseAdditive();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseAdditive(){
    Expr* left = parseMultiplicative();
    while(left && (m_cursor.check(TokenType::Plus) || m_cursor.check(TokenType::Minus))){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseMultiplicative();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseMultiplicative(){
    Expr* left = parseUnary();
    while(left && (m_cursor.check(TokenType::Star) || m_cursor.check(TokenType::Slash) || m_cursor.check(TokenType::Percent))){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseUnary();
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseUnary(){
    if(m_cursor.check(TokenType::Star)){
        m_cursor.error("unary '*' dereference is prohibited in adhd");
        return nullptr;
    }
    if(m_cursor.check(TokenType::Ampersand)){
        m_cursor.error("unary '&' address-of is prohibited in adhd");
        return nullptr;
    }

    if(m_cursor.check(TokenType::Plus) || m_cursor.check(TokenType::Minus) || m_cursor.check(TokenType::Bang) || m_cursor.check(TokenType::Tilde) || m_cursor.check(TokenType::PlusPlus) || m_cursor.check(TokenType::MinusMinus)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* operand = parseUnary();
        if(!operand)
            return nullptr;
        Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
        node->kind = ExprKind::Unary;
        node->op = opTok.type;
        node->text = opTok.text;
        node->lhs = operand;
        node->span = SourceSpan{opTok.line, opTok.column};
        return node;
    }

    return parsePostfix();
}

Expr* ExprParser::parsePostfix(){
    Expr* expr = parsePrimary();
    if(!expr)
        return nullptr;

    for(;;){
        if(m_cursor.check(TokenType::Arrow)){
            m_cursor.error("'->' pointer access is prohibited in adhd; use '.'");
            return nullptr;
        }

        if(m_cursor.match(TokenType::Dot)){
            if(!m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected member or swizzle name after '.'");
                return nullptr;
            }
            const Token memberTok = m_cursor.current();
            m_cursor.advance();
            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = ExprKind::MemberAccess;
            node->lhs = expr;
            node->text = memberTok.text;
            node->span = SourceSpan{memberTok.line, memberTok.column};
            expr = node;
            continue;
        }

        if(m_cursor.match(TokenType::LeftBracket)){
            Expr* indexExpr = parseExpression();
            if(!indexExpr)
                return nullptr;
            if(!m_cursor.expect(TokenType::RightBracket, "expected ']' after index expression"))
                return nullptr;
            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = ExprKind::Index;
            node->lhs = expr;
            node->rhs = indexExpr;
            node->span = expr->span;
            expr = node;
            continue;
        }

        if(m_cursor.check(TokenType::Less)){
            const ParserCursor::Checkpoint cp = m_cursor.save();
            MVector<TypeArg> targs(m_cursor.arena());
            if(m_typeParser.parseTypeArgList(targs) && m_cursor.check(TokenType::LeftParen)){
                Expr* callNode = AllocateAstNode<Expr>(m_cursor.arena());
                callNode->kind = ExprKind::Call;
                callNode->lhs = expr;
                callNode->callTemplateArgs = Move(targs);
                callNode->span = expr->span;
                if(!parseArgumentList(callNode->arguments))
                    return nullptr;
                expr = callNode;
                continue;
            }
            m_cursor.restore(cp);
        }

        if(m_cursor.check(TokenType::LeftParen)){
            Expr* callNode = AllocateAstNode<Expr>(m_cursor.arena());
            callNode->kind = ExprKind::Call;
            callNode->lhs = expr;
            callNode->span = expr->span;
            if(!parseArgumentList(callNode->arguments))
                return nullptr;
            expr = callNode;
            continue;
        }

        if(m_cursor.check(TokenType::PlusPlus) || m_cursor.check(TokenType::MinusMinus)){
            const Token opTok = m_cursor.current();
            m_cursor.advance();
            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = ExprKind::Unary;
            node->op = opTok.type;
            node->text = opTok.text;
            node->lhs = expr;
            node->isPostfix = true;
            node->span = SourceSpan{opTok.line, opTok.column};
            expr = node;
            continue;
        }

        break;
    }

    return expr;
}

Expr* ExprParser::parsePrimary(){
    const Token tok = m_cursor.current();
    const SourceSpan span{tok.line, tok.column};

    if(tok.type == TokenType::IntegerLiteral){
        m_cursor.advance();
        return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), ExprKind::IntegerLiteral, tok, span);
    }

    if(tok.type == TokenType::FloatLiteral){
        m_cursor.advance();
        return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), ExprKind::FloatLiteral, tok, span);
    }

    if(tok.type == TokenType::DoubleLiteral){
        m_cursor.advance();
        return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), ExprKind::DoubleLiteral, tok, span);
    }

    if(tok.type == TokenType::StringLiteral){
        m_cursor.advance();
        return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), ExprKind::StringLiteral, tok, span);
    }

    if(m_cursor.match(TokenType::LeftParen)){
        if(m_cursor.check(TokenType::Identifier)){
            const ParserCursor::Checkpoint cp = m_cursor.save();
            TypeRef* maybeType = m_typeParser.parseType(false, false);
            if(maybeType && m_cursor.match(TokenType::RightParen)){
                if(m_cursor.check(TokenType::Identifier) || m_cursor.check(TokenType::IntegerLiteral) || m_cursor.check(TokenType::FloatLiteral) || m_cursor.check(TokenType::DoubleLiteral) || m_cursor.check(TokenType::LeftParen)){
                    m_cursor.error("C-style '(T)x' cast is prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'");
                    return nullptr;
                }
            }
            m_cursor.restore(cp);
        }
        Expr* inner = parseExpression();
        if(!inner)
            return nullptr;
        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after expression"))
            return nullptr;
        return inner;
    }

    if(tok.type == TokenType::Identifier){
        if(tok.text == "null" || tok.text == "nullptr"){
            m_cursor.error("'null' and 'nullptr' are prohibited in adhd; use Optional<T>");
            return nullptr;
        }
        if(tok.text == "new" || tok.text == "delete"){
            m_cursor.error("'new' and 'delete' are prohibited in adhd; use arena-backed containers");
            return nullptr;
        }
        if(tok.text == "Move"){
            m_cursor.error("'Move' is prohibited in adhd; parameters borrow with 'in'/'mut' and last-use moves are automatic");
            return nullptr;
        }
        if(tok.text == "dynamic_cast" || tok.text == "static_cast" || tok.text == "reinterpret_cast" || tok.text == "const_cast"){
            m_cursor.error("C++ cast keywords are prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'");
            return nullptr;
        }

        if(tok.text == "true" || tok.text == "false"){
            m_cursor.advance();
            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = ExprKind::BoolLiteral;
            node->text = tok.text;
            node->span = span;
            return node;
        }

        if(tok.text == "this"){
            m_cursor.advance();
            if(!m_cursor.expect(TokenType::Dot, "expected '.' after 'this' (use 'this.member')"))
                return nullptr;
            if(!m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected member name after 'this.'");
                return nullptr;
            }
            const Token memberTok = m_cursor.current();
            m_cursor.advance();
            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = ExprKind::ThisMember;
            node->text = memberTok.text;
            node->span = span;
            return node;
        }

        if(tok.text == "cast" || tok.text == "bit_cast"){
            const bool isBitCast = (tok.text == "bit_cast");
            m_cursor.advance();
            if(!m_cursor.expect(TokenType::Less, "expected '<' after cast keyword"))
                return nullptr;
            TypeRef* targetType = m_typeParser.parseType(false, false);
            if(!targetType)
                return nullptr;
            if(!m_cursor.expect(TokenType::Greater, "expected '>' after cast target type"))
                return nullptr;
            if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after cast<T>"))
                return nullptr;
            Expr* inner = parseExpression();
            if(!inner)
                return nullptr;
            if(!m_cursor.expect(TokenType::RightParen, "expected ')' after cast expression"))
                return nullptr;

            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = isBitCast ? ExprKind::BitCast : ExprKind::Cast;
            node->targetType = targetType;
            node->lhs = inner;
            node->span = span;
            return node;
        }

        if(tok.text == "lambda"){
            m_cursor.advance();
            return parseLambda(span);
        }

        m_cursor.advance();

        if(m_cursor.check(TokenType::ColonColon)){
            Expr* scopeNode = AllocateAstNode<Expr>(m_cursor.arena());
            scopeNode->kind = ExprKind::ScopeAccess;
            scopeNode->span = span;
            scopeNode->scopeChain.push_back(tok.text);
            while(m_cursor.match(TokenType::ColonColon)){
                if(!m_cursor.check(TokenType::Identifier)){
                    m_cursor.errorExpected("expected identifier after '::'");
                    return nullptr;
                }
                scopeNode->scopeChain.push_back(m_cursor.current().text);
                m_cursor.advance();
            }
            return scopeNode;
        }

        return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), ExprKind::Identifier, tok, span);
    }

    m_cursor.errorExpected("expected expression");
    return nullptr;
}

Expr* ExprParser::parseLambda(const SourceSpan span){
    Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
    node->kind = ExprKind::Lambda;
    node->span = span;

    if(m_cursor.match(TokenType::LeftBracket)){
        if(!m_cursor.check(TokenType::RightBracket)){
            for(;;){
                LambdaCapture cap;
                cap.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};
                if(m_cursor.matchIdentifier("in"))
                    cap.mode = ParamPassingMode::In;
                else if(m_cursor.matchIdentifier("mut"))
                    cap.mode = ParamPassingMode::Mut;
                else{
                    m_cursor.error("lambda capture must explicitly specify 'in' or 'mut'");
                    return nullptr;
                }

                if(!m_cursor.check(TokenType::Identifier)){
                    m_cursor.errorExpected("expected captured variable name after 'in'/'mut'");
                    return nullptr;
                }
                cap.name = m_cursor.current().text;
                m_cursor.advance();

                node->lambdaCaptures.push_back(cap);

                if(m_cursor.check(TokenType::RightBracket))
                    break;
                if(!m_cursor.expect(TokenType::Comma, "expected ',' or ']' in lambda capture list"))
                    return nullptr;
            }
        }
        if(!m_cursor.expect(TokenType::RightBracket, "expected ']' after lambda captures"))
            return nullptr;
    }

    if(!m_typeParser.parseParamList(node->lambdaParams))
        return nullptr;

    if(!m_cursor.expect(TokenType::Arrow, "expected '-> ReturnType' after lambda parameters"))
        return nullptr;

    node->lambdaReturnType = m_typeParser.parseType(false, false);
    if(!node->lambdaReturnType)
        return nullptr;

    if(!m_stmtParser || !m_stmtParser->parseBlockStatements(node->lambdaBody))
        return nullptr;

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

