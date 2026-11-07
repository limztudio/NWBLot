// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stmt_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_expr_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] constexpr b8 IsUnambiguousCastOperandStartToken(const TokenType::Enum type)noexcept{
    switch(type){
    case TokenType::Identifier:
    case TokenType::IntegerLiteral:
    case TokenType::FloatLiteral:
    case TokenType::DoubleLiteral:
    case TokenType::StringLiteral:
    case TokenType::LeftParen:
    case TokenType::Bang:
    case TokenType::Tilde:
        return true;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ExprParser::parseArgumentList(MVector<Expr*>& outArgs){
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' before argument list"))
        return false;

    if(!m_cursor.check(TokenType::RightParen)){
        for(;;){
            if(m_cursor.check(TokenType::Identifier) && ParseParamPassingModeKeyword(m_cursor.current().text) != ParamPassingMode::None){
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
    Expr* lhs = parseBinaryFromPrecedence();
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

    if(IsCompoundOrSimpleAssignment(m_cursor.current().type)){
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* rhs = parseAssignment();
        if(!rhs)
            return nullptr;

        return __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, lhs, rhs);
    }

    return lhs;
}

Expr* ExprParser::parseBinaryFromPrecedence(const u8 minPrecedence){
    Expr* left = parseUnary();
    while(left){
        const u8 prec = BinaryOperatorPrecedence(m_cursor.current().type);
        if(prec == 0u || prec < minPrecedence)
            break;
        const Token opTok = m_cursor.current();
        m_cursor.advance();
        Expr* right = parseBinaryFromPrecedence(static_cast<u8>(prec + 1u));
        if(!right)
            return nullptr;
        left = __hidden_adhd_expr_parser::MakeBinaryExpr(m_cursor.arena(), opTok, left, right);
    }
    return left;
}

Expr* ExprParser::parseUnary(){
    const MStringView prohibitedUnaryErr = FindProhibitedUnaryOperatorError(m_cursor.current().type);
    if(!prohibitedUnaryErr.empty()){
        m_cursor.error(prohibitedUnaryErr);
        return nullptr;
    }

    if(IsPrefixUnaryOperatorToken(m_cursor.current().type)){
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
            if(m_cursor.check(TokenType::ColonColon)){
                expr = parseScopeAccessChain(expr, {}, memberTok.text, SourceSpan{memberTok.line, memberTok.column});
                if(!expr)
                    return nullptr;
                continue;
            }
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
            if(m_typeParser.parseTypeArgList(targs) && !m_typeParser.hasPendingRightAngles() && m_cursor.check(TokenType::LeftParen)){
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
            m_typeParser.resetPendingRightAngles();
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

        if(IsIncrementOrDecrementOperatorToken(m_cursor.current().type)){
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

    ExprKind::Enum literalKind = ExprKind::IntegerLiteral;
    if(TryMapLiteralTokenToExprKind(tok.type, literalKind)){
        m_cursor.advance();
        return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), literalKind, tok, span);
    }

    if(m_cursor.match(TokenType::LeftParen)){
        if(m_cursor.check(TokenType::Identifier)){
            const MStringView prohibitedTypeErr = FindProhibitedTypeKeywordError(m_cursor.current().text);
            if(!prohibitedTypeErr.empty()){
                m_cursor.error(prohibitedTypeErr);
                return nullptr;
            }
            const ParserCursor::Checkpoint cp = m_cursor.save();
            TypeRef* maybeType = m_typeParser.parseType(false, false);
            if(!maybeType && (m_cursor.check(TokenType::Star) || m_cursor.check(TokenType::Ampersand))){
                while(m_cursor.check(TokenType::Star) || m_cursor.check(TokenType::Ampersand))
                    m_cursor.advance();
                if(m_cursor.match(TokenType::RightParen)){
                    const b8 nextIsCastOperand = __hidden_adhd_expr_parser::IsUnambiguousCastOperandStartToken(m_cursor.current().type)
                        || m_cursor.check(TokenType::Plus)
                        || m_cursor.check(TokenType::Minus);
                    if(nextIsCastOperand)
                        return nullptr;
                }
            }
            if(maybeType && !m_typeParser.hasPendingRightAngles() && m_cursor.match(TokenType::RightParen)){
                const b8 isKnownType = maybeType->refMode != ParamPassingMode::None || maybeType->isFuncType || !maybeType->templateArgs.empty() || IsBuiltinTypeName(maybeType->name);
                const b8 looksLikeType = isKnownType || (!maybeType->name.empty() && IsAsciiUpper(maybeType->name[0]));
                const b8 nextIsUnambiguousCastOperand = __hidden_adhd_expr_parser::IsUnambiguousCastOperandStartToken(m_cursor.current().type);
                const b8 nextIsPlusOrMinus = m_cursor.check(TokenType::Plus) || m_cursor.check(TokenType::Minus);
                if((looksLikeType && nextIsUnambiguousCastOperand) || (isKnownType && nextIsPlusOrMinus)){
                    m_cursor.error("C-style '(T)x' cast is prohibited in adhd; use 'cast<T>(x)' or 'bit_cast<T>(x)'");
                    return nullptr;
                }
            }
            m_cursor.restore(cp);
            m_typeParser.resetPendingRightAngles();
        }
        Expr* inner = parseExpression();
        if(!inner)
            return nullptr;
        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after expression"))
            return nullptr;
        return inner;
    }

    if(tok.type == TokenType::Identifier){
        const MStringView prohibitedErr = FindProhibitedExprKeywordError(tok.text);
        if(!prohibitedErr.empty()){
            m_cursor.error(prohibitedErr);
            return nullptr;
        }

        if(IsBoolLiteralText(tok.text)){
            m_cursor.advance();
            return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), ExprKind::BoolLiteral, tok, span);
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
            if(m_cursor.check(TokenType::ColonColon))
                return parseScopeAccessChain(nullptr, "this", memberTok.text, span);
            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = ExprKind::ThisMember;
            node->text = memberTok.text;
            node->span = span;
            return node;
        }

        ExprKind::Enum castKind = ExprKind::Cast;
        if(TryParseCastKeyword(tok.text, castKind)){
            m_cursor.advance();
            if(!m_cursor.expect(TokenType::Less, "expected '<' after cast keyword"))
                return nullptr;
            TypeRef* targetType = m_typeParser.parseType(false, false);
            if(!targetType)
                return nullptr;
            if(!m_typeParser.expectClosingAngle())
                return nullptr;
            if(!m_cursor.expect(TokenType::LeftParen, "expected '(' after cast<T>"))
                return nullptr;
            Expr* inner = parseExpression();
            if(!inner)
                return nullptr;
            if(!m_cursor.expect(TokenType::RightParen, "expected ')' after cast expression"))
                return nullptr;

            Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
            node->kind = castKind;
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

        if(m_cursor.check(TokenType::ColonColon))
            return parseScopeAccessChain(nullptr, {}, tok.text, span);

        return __hidden_adhd_expr_parser::MakeTokenExpr(m_cursor.arena(), ExprKind::Identifier, tok, span);
    }

    m_cursor.errorExpected("expected expression");
    return nullptr;
}

Expr* ExprParser::parseScopeAccessChain(Expr* lhs, const MStringView text, const MStringView firstSegment, const SourceSpan span){
    Expr* scopeNode = AllocateAstNode<Expr>(m_cursor.arena());
    scopeNode->kind = ExprKind::ScopeAccess;
    scopeNode->lhs = lhs;
    scopeNode->text = text;
    scopeNode->span = span;
    scopeNode->scopeChain.push_back(firstSegment);
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

Expr* ExprParser::parseLambda(const SourceSpan span){
    Expr* node = AllocateAstNode<Expr>(m_cursor.arena());
    node->kind = ExprKind::Lambda;
    node->span = span;

    if(m_cursor.match(TokenType::LeftBracket)){
        if(!m_cursor.check(TokenType::RightBracket)){
            for(;;){
                LambdaCapture cap;
                cap.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};
                cap.mode = MatchParamPassingMode(m_cursor);
                if(cap.mode == ParamPassingMode::None){
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

    node->lambdaReturnType = m_typeParser.parseType(true, false);
    if(!node->lambdaReturnType)
        return nullptr;

    if(!m_stmtParser || !m_stmtParser->parseBlockStatements(node->lambdaBody))
        return nullptr;

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

