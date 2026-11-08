// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor_macro_expander.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize PreprocessorMacroExpander::AppendDefinedOperand(const MStringView source, usize i, MString& out){
    while(i < source.size() && (source[i] == ' ' || source[i] == '\t'))
        out.push_back(source[i++]);
    if(i < source.size() && source[i] == '('){
        out.push_back(source[i++]);
        while(i < source.size() && source[i] != ')')
            out.push_back(source[i++]);
        if(i < source.size())
            out.push_back(source[i++]);
    }
    else if(i < source.size() && IsAsciiIdentifierStart(source[i])){
        while(i < source.size() && IsAsciiIdentifierChar(source[i]))
            out.push_back(source[i++]);
    }
    return i;
}


b8 PreprocessorMacroExpander::expand(const MStringView lineText, const u32 line, MString& outExpanded){
    constexpr u32 s_MaxMacroExpansionPasses = 16u;

    MString current(lineText.data(), lineText.size(), m_arena);

    for(u32 pass = 0u; pass < s_MaxMacroExpansionPasses; ++pass){
        b8 anyReplaced = false;
        MString next(m_arena);
        next.reserve(current.size());

        usize i = 0u;
        while(i < current.size()){
            const MStringView currentView(current.data(), current.size());
            if(current[i] == '"' || current[i] == '\''){
                i = AppendQuotedLiteral(currentView, i, next);
                continue;
            }

            if(i + 1u < current.size() && current[i] == '/' && current[i + 1u] == '/'){
                next.append(current.data() + i, current.size() - i);
                break;
            }

            if(i + 1u < current.size() && current[i] == '/' && current[i + 1u] == '*'){
                const usize closeComment = currentView.find("*/", i + 2u);
                if(closeComment == MStringView::npos){
                    next.append(current.data() + i, current.size() - i);
                    break;
                }
                next.append(current.data() + i, (closeComment + 2u) - i);
                i = closeComment + 2u;
                continue;
            }

            if(IsAsciiDigit(current[i])){
                i = Preprocessor::AppendPreprocessorNumber(currentView, i, true, next);
                continue;
            }

            if(IsAsciiIdentifierStart(current[i])){
                const usize idStart = i;
                while(i < current.size() && IsAsciiIdentifierChar(current[i]))
                    ++i;
                const MStringView ident(current.data() + idStart, i - idStart);
                const auto it = m_macros.find(ident);
                if(it == m_macros.end()){
                    next.append(ident.data(), ident.size());
                    if(ident == "defined")
                        i = AppendDefinedOperand(currentView, i, next);
                    continue;
                }

                const PreprocessorMacro& macro = it.value();
                if(!macro.isFunctionLike){
                    next.append(macro.body.data(), macro.body.size());
                    anyReplaced = true;
                    continue;
                }

                usize lookahead = i;
                while(lookahead < current.size() && (current[lookahead] == ' ' || current[lookahead] == '\t'))
                    ++lookahead;
                if(lookahead >= current.size() || current[lookahead] != '('){
                    next.append(ident.data(), ident.size());
                    continue;
                }

                usize argScan = lookahead + 1u;
                u32 parenDepth = 1u;
                MVector<MString> args(m_arena);
                usize argStart = argScan;
                while(argScan < current.size() && parenDepth > 0u){
                    const MChar ch = current[argScan];
                    if(ch == '\'' && argScan > lookahead + 1u && (IsAsciiDigit(current[argScan - 1u]) || IsAsciiIdentifierChar(current[argScan - 1u]))){
                        ++argScan;
                        continue;
                    }
                    if(ch == '"' || ch == '\''){
                        argScan = SkipQuotedLiteral(currentView, argScan);
                        continue;
                    }
                    if(argScan + 1u < current.size() && ch == '/' && current[argScan + 1u] == '*'){
                        const usize closeComment = currentView.find("*/", argScan + 2u);
                        if(closeComment == MStringView::npos)
                            break;
                        argScan = closeComment + 2u;
                        continue;
                    }
                    if(ch == '(')
                        ++parenDepth;
                    else if(ch == ')'){
                        --parenDepth;
                        if(parenDepth == 0u){
                            const MStringView argText = TrimView(MStringView(current.data() + argStart, argScan - argStart));
                            if(!argText.empty() || !macro.parameters.empty())
                                args.push_back(MString(argText.data(), argText.size(), m_arena));
                            ++argScan;
                            break;
                        }
                    }
                    else if(ch == ',' && parenDepth == 1u){
                        const MStringView argText = TrimView(MStringView(current.data() + argStart, argScan - argStart));
                        args.push_back(MString(argText.data(), argText.size(), m_arena));
                        argStart = argScan + 1u;
                    }
                    ++argScan;
                }

                if(parenDepth != 0u){
                    m_errors.push_back(ParseError{line, 1u, MString("unterminated function-like macro invocation", m_arena)});
                    return false;
                }
                if(args.size() != macro.parameters.size()){
                    m_errors.push_back(ParseError{line, 1u, MString("function-like macro argument count mismatch", m_arena)});
                    return false;
                }

                i = argScan;
                const MStringView bodyView(macro.body.data(), macro.body.size());
                usize b = 0u;
                while(b < macro.body.size()){
                    if(macro.body[b] == '"' || macro.body[b] == '\''){
                        b = AppendQuotedLiteral(bodyView, b, next);
                        continue;
                    }
                    if(IsAsciiDigit(macro.body[b])){
                        b = Preprocessor::AppendPreprocessorNumber(bodyView, b, true, next);
                        continue;
                    }
                    if(IsAsciiIdentifierStart(macro.body[b])){
                        const usize pStart = b;
                        while(b < macro.body.size() && IsAsciiIdentifierChar(macro.body[b]))
                            ++b;
                        const MStringView tokenName(macro.body.data() + pStart, b - pStart);
                        b8 matchedParam = false;
                        for(usize pIdx = 0u; pIdx < macro.parameters.size(); ++pIdx){
                            if(MStringView(macro.parameters[pIdx]) == tokenName){
                                next.append(args[pIdx].data(), args[pIdx].size());
                                matchedParam = true;
                                break;
                            }
                        }
                        if(!matchedParam)
                            next.append(tokenName.data(), tokenName.size());
                    }
                    else{
                        next.push_back(macro.body[b++]);
                    }
                }
                anyReplaced = true;
            }
            else{
                next.push_back(current[i++]);
            }
        }

        current = Move(next);
        if(!anyReplaced)
            break;
    }

    outExpanded = Move(current);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

