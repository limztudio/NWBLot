// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ConsumeScopedConstantTail(ParserCursor& cursor, const MChar* const startPtr, const MChar* endPtr, MStringView& outConstantText){
    while(cursor.match(TokenType::ColonColon)){
        if(!cursor.check(TokenType::Identifier)){
            cursor.errorExpected("expected identifier after '::' in template argument");
            return false;
        }
        endPtr = cursor.current().text.data() + cursor.current().text.size();
        cursor.advance();
    }
    outConstantText = MStringView(startPtr, static_cast<usize>(endPtr - startPtr));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TypeRef* TypeParser::parseType(const b8 allowRefMode, const b8 allowAuto){
    ParamPassingMode::Enum refMode = ParamPassingMode::None;
    const SourceSpan startSpan{m_cursor.current().line, m_cursor.current().column};

    if(allowRefMode)
        refMode = MatchParamPassingMode(m_cursor);

    if(m_cursor.check(TokenType::Identifier)){
        const MStringView prohibitedErr = FindProhibitedTypeKeywordError(m_cursor.current().text);
        if(!prohibitedErr.empty()){
            m_cursor.error(prohibitedErr);
            return nullptr;
        }
    }

    if(allowAuto && m_cursor.matchIdentifier("auto")){
        TypeRef* type = AllocateAstNode<TypeRef>(m_cursor.arena());
        type->name = "auto";
        type->refMode = refMode;
        type->isAuto = true;
        type->span = startSpan;
        return type;
    }

    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected type name");
        return nullptr;
    }

    TypeRef* type = AllocateAstNode<TypeRef>(m_cursor.arena());
    type->name = m_cursor.current().text;
    type->refMode = refMode;
    type->span = startSpan;
    m_cursor.advance();

    if(type->name == "Func"){
        type->isFuncType = true;
        if(!m_cursor.expect(TokenType::Less, "expected '<' after 'Func'"))
            return nullptr;

        if(m_cursor.check(TokenType::IntegerLiteral)){
            u64 capVal = 0u;
            if(!TryParseUnsignedIntegerLiteral(m_cursor.current().text, capVal) || capVal > Limit<u32>::s_Max){
                m_cursor.error("invalid Func capture byte capacity literal");
                return nullptr;
            }
            type->inferredCaptureBytes = static_cast<u32>(capVal);
            m_cursor.advance();
            if(!m_cursor.expect(TokenType::Comma, "expected ',' after Func capture byte capacity"))
                return nullptr;
        }

        type->funcReturnType = parseType(true, false);
        if(!type->funcReturnType)
            return nullptr;

        if(!m_cursor.expect(TokenType::LeftParen, "expected '(' in Func<Ret(...)> signature"))
            return nullptr;

        if(!m_cursor.check(TokenType::RightParen)){
            for(;;){
                FuncParamType param;
                param.mode = MatchParamPassingMode(m_cursor);
                if(param.mode == ParamPassingMode::None){
                    m_cursor.error("Func parameter must explicitly specify 'in' or 'mut'");
                    return nullptr;
                }

                param.type = parseType(false, false);
                if(!param.type)
                    return nullptr;

                type->funcParams.push_back(param);

                if(m_cursor.check(TokenType::RightParen))
                    break;
                if(!m_cursor.expect(TokenType::Comma, "expected ',' or ')' in Func parameter list"))
                    return nullptr;
            }
        }

        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after Func parameters"))
            return nullptr;

        if(!expectClosingAngle())
            return nullptr;
    }
    else if(m_cursor.check(TokenType::Less)){
        if(!parseTypeArgList(type->templateArgs))
            return nullptr;
    }

    if(m_pendingRightAngles == 0u){
        if(m_cursor.check(TokenType::Star)){
            m_cursor.error("raw pointer types ('*') are prohibited in adhd");
            return nullptr;
        }
        if(m_cursor.check(TokenType::Ampersand)){
            m_cursor.error("C++ reference syntax ('&') is prohibited in adhd; use 'in' or 'mut'");
            return nullptr;
        }
        if(m_cursor.check(TokenType::LeftBracket)){
            m_cursor.error("C-style array types ('T[N]') are prohibited in adhd; use 'Array<T, N>' or 'Vector<T>'");
            return nullptr;
        }
    }

    return type;
}

b8 TypeParser::parseTemplateParamList(MVector<TemplateParamDecl>& outParams){
    if(!m_cursor.match(TokenType::Less))
        return true;

    if(m_cursor.check(TokenType::Greater)){
        m_cursor.error("template parameter list cannot be empty");
        return false;
    }

    for(;;){
        TemplateParamDecl param(m_cursor.arena());
        param.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};

        if(m_cursor.matchIdentifier("const")){
            param.constValueType = parseType(false, false);
            if(!param.constValueType)
                return false;
            if(!m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected compile-time constant parameter name");
                return false;
            }
            param.name = m_cursor.current().text;
            m_cursor.advance();
        }
        else{
            if(m_cursor.checkIdentifier("typename") || m_cursor.checkIdentifier("class")){
                m_cursor.error("do not write 'typename' or 'class' inside '<...>' template parameters in adhd");
                return false;
            }
            if(!m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected template type parameter name");
                return false;
            }
            param.name = m_cursor.current().text;
            m_cursor.advance();

            if(m_cursor.match(TokenType::Colon)){
                for(;;){
                    if(!m_cursor.check(TokenType::Identifier)){
                        m_cursor.errorExpected("expected interface bound name after ':'");
                        return false;
                    }
                    TypeRef* boundType = parseType(false, false);
                    if(!boundType)
                        return false;
                    param.interfaceBounds.push_back(boundType);
                    if(!m_cursor.match(TokenType::Plus))
                        break;
                }
            }
        }

        outParams.push_back(Move(param));

        if(m_cursor.check(TokenType::Greater) || m_cursor.check(TokenType::GreaterGreater) || m_pendingRightAngles > 0u)
            break;
        if(!m_cursor.expect(TokenType::Comma, "expected ',' or '>' in template parameter list"))
            return false;
    }

    return expectClosingAngle();
}

b8 TypeParser::parseTypeArgList(MVector<TypeArg>& outArgs){
    if(!m_cursor.expect(TokenType::Less, "expected '<' before template arguments"))
        return false;

    for(;;){
        TypeArg arg;
        if(m_cursor.check(TokenType::IntegerLiteral)){
            arg.constantText = m_cursor.current().text;
            m_cursor.advance();
        }
        else if(m_cursor.check(TokenType::Minus) || m_cursor.check(TokenType::Plus)){
            const MChar* const signStartPtr = m_cursor.current().text.data();
            m_cursor.advance();
            if(!m_cursor.check(TokenType::IntegerLiteral) && !m_cursor.check(TokenType::Identifier)){
                m_cursor.errorExpected("expected integer literal or constant identifier after unary sign in template argument");
                return false;
            }
            const Token& valTok = m_cursor.current();
            const MChar* const endPtr = valTok.text.data() + valTok.text.size();
            m_cursor.advance();
            if(!__hidden_adhd_type_parser::ConsumeScopedConstantTail(m_cursor, signStartPtr, endPtr, arg.constantText))
                return false;
        }
        else if(m_cursor.check(TokenType::LeftParen)){
            const MChar* const parenStartPtr = m_cursor.current().text.data();
            u32 parenDepth = 1u;
            m_cursor.advance();
            const MChar* parenEndPtr = parenStartPtr + 1u;
            while(parenDepth > 0u && !m_cursor.check(TokenType::EndOfFile)){
                if(m_cursor.check(TokenType::LeftParen))
                    ++parenDepth;
                else if(m_cursor.check(TokenType::RightParen))
                    --parenDepth;
                parenEndPtr = m_cursor.current().text.data() + m_cursor.current().text.size();
                m_cursor.advance();
            }
            if(parenDepth != 0u){
                m_cursor.errorExpected("unterminated '(' in template argument expression");
                return false;
            }
            arg.constantText = MStringView(parenStartPtr, static_cast<usize>(parenEndPtr - parenStartPtr));
        }
        else{
            arg.type = parseType(true, false);
            if(!arg.type)
                return false;
            if(m_pendingRightAngles == 0u
                && arg.type->refMode == ParamPassingMode::None
                && !arg.type->isFuncType
                && arg.type->templateArgs.empty()
                && m_cursor.check(TokenType::ColonColon))
            {
                const MChar* const startPtr = arg.type->name.data();
                const MChar* const endPtr = startPtr + arg.type->name.size();
                if(!__hidden_adhd_type_parser::ConsumeScopedConstantTail(m_cursor, startPtr, endPtr, arg.constantText))
                    return false;
                arg.type = nullptr;
            }
        }

        outArgs.push_back(arg);

        if(m_cursor.check(TokenType::Greater) || m_cursor.check(TokenType::GreaterGreater) || m_pendingRightAngles > 0u)
            break;
        if(!m_cursor.expect(TokenType::Comma, "expected ',' or '>' in template arguments"))
            return false;
    }

    return expectClosingAngle();
}

b8 TypeParser::parseParamList(MVector<ParamDecl>& outParams){
    if(!m_cursor.expect(TokenType::LeftParen, "expected '(' for parameter list"))
        return false;

    if(!m_cursor.check(TokenType::RightParen)){
        for(;;){
            ParamDecl param;
            param.span = SourceSpan{m_cursor.current().line, m_cursor.current().column};
            param.mode = MatchParamPassingMode(m_cursor);
            if(param.mode == ParamPassingMode::None){
                m_cursor.error("every parameter in adhd must explicitly specify 'in' or 'mut'");
                return false;
            }

            param.type = parseType(false, false);
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

b8 TypeParser::expectClosingAngle(){
    if(m_pendingRightAngles > 0u){
        --m_pendingRightAngles;
        return true;
    }
    if(m_cursor.match(TokenType::Greater))
        return true;
    if(m_cursor.match(TokenType::GreaterGreater)){
        m_pendingRightAngles = 1u;
        return true;
    }
    m_cursor.errorExpected("expected '>'");
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

