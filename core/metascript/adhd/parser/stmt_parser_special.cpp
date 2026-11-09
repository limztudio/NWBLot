// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/keyword_semantics.h>

#include "stmt_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Stmt* StmtParser::parseParallelStatement(const SourceSpan span, const Span<const ParsedAttribute> attributes){
    ParallelCoreKind::Enum coreKind = ParallelCoreKind::Any;
    ParallelPriorityKind::Enum priorityKind = ParallelPriorityKind::Normal;
    if(!applySchedulingAttributes(attributes, coreKind, priorityKind))
        return nullptr;

    if(m_cursor.check(TokenType::LeftBrace)){
        Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
        stmt->kind = StmtKind::ParallelBlock;
        stmt->span = span;
        stmt->parallelCore = coreKind;
        stmt->parallelPriority = priorityKind;
        if(!parseBlockStatements(stmt->statements))
            return nullptr;
        return stmt;
    }

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
                binding.mode = MatchParamPassingMode(m_cursor);
                if(binding.mode == ParamPassingMode::None){
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

    if(!m_cursor.consumeBalancedBracesRaw(stmt->nativeCode))
        return nullptr;

    while(m_cursor.match(TokenType::Semicolon)){}

    return stmt;
}

Stmt* StmtParser::parseDiagnosticStatement(const SourceSpan span, const DiagnosticKind::Enum kind, const Span<const ParsedAttribute> attributes){
    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::Diagnostic;
    stmt->diagnosticKind = kind;
    stmt->span = span;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
#define NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE(attrName, stmtField) \
        if(attr.name == attrName && attr.arguments.empty()){ \
            stmt->stmtField = true; \
            matchedAttr = true; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_ATTRIBUTE
        if(!matchedAttr){
            m_cursor.error(attr.line, attr.column, "only '[essential]' is permitted on diagnostic statements");
            return nullptr;
        }
    }

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

Stmt* StmtParser::parseVarDeclWithType(TypeRef& varType, const SourceSpan span, const b8 isConst){
    const MStringView varName = m_cursor.current().text;
    m_cursor.advance();

    if(!isConst && m_cursor.check(TokenType::LeftBracket)){
        m_cursor.error(span.line, span.column, s_ProhibitedCStyleArrayError);
        return nullptr;
    }

    Stmt* stmt = AllocateAstNode<Stmt>(m_cursor.arena());
    stmt->kind = StmtKind::VarDecl;
    stmt->varType = &varType;
    stmt->name = varName;
    stmt->isConst = isConst;
    stmt->span = span;

    if(m_cursor.match(TokenType::Equal)){
        stmt->expr = m_exprParser.parseExpression();
        if(!stmt->expr)
            return nullptr;
    }
    else if(m_cursor.check(TokenType::LeftParen)){
        if(varType.isAuto){
            m_cursor.error(span.line, span.column, isConst ? "'auto' constant must be initialized with '= expr'" : "'auto' variable must be initialized with '= expr'");
            return nullptr;
        }
        stmt->isCtorInit = true;
        if(!m_exprParser.parseArgumentList(stmt->arguments))
            return nullptr;
    }
    else if(!isConst && m_cursor.check(TokenType::Semicolon)){
        if(varType.isAuto){
            m_cursor.error(span.line, span.column, "uninitialized variable declaration is prohibited in adhd; every local variable must be initialized at declaration");
            return nullptr;
        }
    }
    else{
        m_cursor.errorExpected(isConst ? "expected '=' or '(' to initialize 'const' value" : "expected '=' or '(' to initialize local variable");
        return nullptr;
    }

    if(!m_cursor.expect(TokenType::Semicolon, isConst ? "expected ';' after 'const' declaration" : "expected ';' after variable declaration"))
        return nullptr;

    return stmt;
}

b8 StmtParser::applySchedulingAttributes(const Span<const ParsedAttribute> attributes, ParallelCoreKind::Enum& outCore, ParallelPriorityKind::Enum& outPriority){
    b8 seenCore = false;
    b8 seenPriority = false;
    for(const ParsedAttribute& attr : attributes){
        b8 matchedAttr = false;
#define NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE(attrName, seenVar, outVar, parseFn, duplicateErrorMessage, argErrorMessage, invalidValueErrorMessage) \
        if(attr.name == attrName){ \
            if(seenVar){ \
                m_cursor.error(attr.line, attr.column, duplicateErrorMessage); \
                return false; \
            } \
            seenVar = true; \
            if(attr.arguments.size() != 1u || attr.arguments[0].kind != TokenType::Identifier){ \
                m_cursor.error(attr.line, attr.column, argErrorMessage); \
                return false; \
            } \
            if(!KeywordSemantics::parseFn(attr.arguments[0].text, outVar)){ \
                m_cursor.error(attr.line, attr.column, invalidValueErrorMessage); \
                return false; \
            } \
            matchedAttr = true; \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_SCHEDULING_ATTRIBUTE
        if(!matchedAttr){
            m_cursor.error(attr.line, attr.column, "unsupported attribute on parallel statement; only 'core(...)' and 'priority(...)' are permitted");
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

