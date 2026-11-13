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


namespace __hidden_macro_expander{

[[nodiscard]] static usize SkipDefinedOperand(const MStringView source, usize i)noexcept{
    while(i < source.size() && (source[i] == ' ' || source[i] == '\t'))
        ++i;
    if(i < source.size() && source[i] == '('){
        ++i;
        while(i < source.size() && source[i] != ')')
            ++i;
        if(i < source.size())
            ++i;
    }
    else if(i < source.size() && IsAsciiIdentifierStart(source[i])){
        while(i < source.size() && IsAsciiIdentifierChar(source[i]))
            ++i;
    }
    return i;
}

[[nodiscard]] static usize SkipNumber(const MStringView text, usize index)noexcept{
    while(index < text.size()){
        const MChar ch = text[index];
        if((ch == 'e' || ch == 'E' || ch == 'p' || ch == 'P') && index + 1u < text.size() && (text[index + 1u] == '+' || text[index + 1u] == '-')){
            index += 2u;
            continue;
        }
        if(ch == '\'' && index + 1u < text.size() && (IsAsciiDigit(text[index + 1u]) || IsAsciiIdentifierChar(text[index + 1u]))){
            index += 2u;
            continue;
        }
        if(IsAsciiDigit(ch) || IsAsciiIdentifierChar(ch) || ch == '.'){
            ++index;
            continue;
        }
        break;
    }
    return index;
}

};


b8 PreprocessorMacroExpander::expand(const MStringView lineText, const u32 line, MString& outExpanded, const b8 initialPendingNativeBrace, const u32 initialNativeBraceDepth){
    MString storage(m_arena);
    MStringView expanded;
    if(!expandToView(lineText, line, storage, expanded, initialPendingNativeBrace, initialNativeBraceDepth))
        return false;
    if(expanded.data() == storage.data())
        outExpanded = Move(storage);
    else{
        MString owned(m_arena);
        if(!expanded.empty())
            owned.assign(expanded.data(), expanded.size());
        outExpanded = Move(owned);
    }
    return true;
}

b8 PreprocessorMacroExpander::expandToView(const MStringView lineText, const u32 line, MString& outStorage, MStringView& outView, const b8 initialPendingNativeBrace, const u32 initialNativeBraceDepth){
    constexpr u32 s_MaxMacroExpansionPasses = 16u;

    MString current(m_arena);
    MStringView currentView = lineText;
    b8 hasOwnedCurrent = false;

    for(u32 pass = 0u; pass < s_MaxMacroExpansionPasses; ++pass){
        b8 anyReplaced = false;
        b8 pendingNativeBrace = initialPendingNativeBrace;
        u32 nativeBraceDepth = initialNativeBraceDepth;
        MString next(m_arena);
        usize copiedUntil = 0u;

        usize i = 0u;
        while(i < currentView.size()){
            if(currentView[i] == '\'' && i > 0u && (IsAsciiDigit(currentView[i - 1u]) || IsAsciiIdentifierChar(currentView[i - 1u]))){
                ++i;
                continue;
            }
            if(currentView[i] == '"'){
                MStringView rawDelim;
                if(Preprocessor::TryParseRawStringOpening(currentView, i, rawDelim)){
                    const usize bodyStart = i + 1u + rawDelim.size() + 1u;
                    usize afterClose = 0u;
                    if(!Preprocessor::FindRawStringClosing(currentView, bodyStart, rawDelim, afterClose)){
                        break;
                    }
                    i = afterClose;
                    continue;
                }
            }
            if(currentView[i] == '"' || currentView[i] == '\''){
                i = SkipQuotedLiteral(currentView, i);
                continue;
            }

            if(i + 1u < currentView.size() && currentView[i] == '/' && currentView[i + 1u] == '/'){
                break;
            }

            if(i + 1u < currentView.size() && currentView[i] == '/' && currentView[i + 1u] == '*'){
                const usize closeComment = currentView.find("*/", i + 2u);
                if(closeComment == MStringView::npos){
                    break;
                }
                i = closeComment + 2u;
                continue;
            }

            if(nativeBraceDepth > 0u){
                if(currentView[i] == '{')
                    ++nativeBraceDepth;
                else if(currentView[i] == '}')
                    --nativeBraceDepth;
                ++i;
                continue;
            }

            if(currentView[i] == '{' && pendingNativeBrace){
                nativeBraceDepth = 1u;
                pendingNativeBrace = false;
                ++i;
                continue;
            }

            if(currentView[i] == ';'){
                pendingNativeBrace = false;
                ++i;
                continue;
            }

            if(IsAsciiDigit(currentView[i])){
                i = __hidden_macro_expander::SkipNumber(currentView, i);
                continue;
            }

            if(IsAsciiIdentifierStart(currentView[i])){
                const usize idStart = i;
                while(i < currentView.size() && IsAsciiIdentifierChar(currentView[i]))
                    ++i;
                const MStringView ident(currentView.data() + idStart, i - idStart);
                const auto it = m_macros.find(ident);
                if(it == m_macros.end()){
                    if(ident == "defined")
                        i = __hidden_macro_expander::SkipDefinedOperand(currentView, i);
                    else if(ident == "native")
                        pendingNativeBrace = true;
                    continue;
                }

                const PreprocessorMacro& macro = it.value();
                if(!macro.isFunctionLike){
                    if(!anyReplaced)
                        next.reserve(currentView.size());
                    if(idStart > copiedUntil)
                        next.append(currentView.data() + copiedUntil, idStart - copiedUntil);
                    next.append(macro.body.data(), macro.body.size());
                    copiedUntil = i;
                    anyReplaced = true;
                    continue;
                }

                usize lookahead = i;
                while(lookahead < currentView.size() && (currentView[lookahead] == ' ' || currentView[lookahead] == '\t'))
                    ++lookahead;
                if(lookahead >= currentView.size() || currentView[lookahead] != '('){
                    continue;
                }

                usize argScan = lookahead + 1u;
                u32 parenDepth = 1u;
                MVector<MStringView> args(m_arena);
                usize argStart = argScan;
                while(argScan < currentView.size() && parenDepth > 0u){
                    const MChar ch = currentView[argScan];
                    if(ch == '\'' && argScan > lookahead + 1u && (IsAsciiDigit(currentView[argScan - 1u]) || IsAsciiIdentifierChar(currentView[argScan - 1u]))){
                        ++argScan;
                        continue;
                    }
                    if(ch == '"'){
                        MStringView rawDelim;
                        if(Preprocessor::TryParseRawStringOpening(currentView, argScan, rawDelim)){
                            const usize bodyStart = argScan + 1u + rawDelim.size() + 1u;
                            usize afterClose = 0u;
                            if(!Preprocessor::FindRawStringClosing(currentView, bodyStart, rawDelim, afterClose)){
                                argScan = currentView.size();
                                break;
                            }
                            argScan = afterClose;
                            continue;
                        }
                    }
                    if(ch == '"' || ch == '\''){
                        argScan = SkipQuotedLiteral(currentView, argScan);
                        continue;
                    }
                    if(argScan + 1u < currentView.size() && ch == '/' && currentView[argScan + 1u] == '*'){
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
                            const MStringView argText = TrimView(MStringView(currentView.data() + argStart, argScan - argStart));
                            if(!argText.empty() || (!macro.isVariadic && !macro.parameters.empty()) || !args.empty())
                                args.push_back(argText);
                            ++argScan;
                            break;
                        }
                    }
                    else if(ch == ',' && parenDepth == 1u){
                        const MStringView argText = TrimView(MStringView(currentView.data() + argStart, argScan - argStart));
                        args.push_back(argText);
                        argStart = argScan + 1u;
                    }
                    ++argScan;
                }

                if(parenDepth != 0u){
                    m_errors.push_back(ParseError{line, 1u, MString("unterminated function-like macro invocation", m_arena)});
                    return false;
                }
                if(macro.isVariadic ? (args.size() < macro.parameters.size()) : (args.size() != macro.parameters.size())){
                    m_errors.push_back(ParseError{line, 1u, MString("function-like macro argument count mismatch", m_arena)});
                    return false;
                }

                MVector<MString> expandedArgs(m_arena);
                expandedArgs.reserve(args.size());
                for(usize argIdx = 0u; argIdx < args.size(); ++argIdx){
                    MString expandedArg(m_arena);
                    if(!expand(args[argIdx], line, expandedArg))
                        return false;
                    expandedArgs.push_back(Move(expandedArg));
                }

                MString vaArgsText(m_arena);
                if(macro.isVariadic){
                    for(usize vaIdx = macro.parameters.size(); vaIdx < expandedArgs.size(); ++vaIdx){
                        if(vaIdx > macro.parameters.size())
                            vaArgsText.append(", ");
                        vaArgsText.append(expandedArgs[vaIdx].data(), expandedArgs[vaIdx].size());
                    }
                }

                if(!anyReplaced)
                    next.reserve(currentView.size());
                if(idStart > copiedUntil)
                    next.append(currentView.data() + copiedUntil, idStart - copiedUntil);
                i = argScan;
                copiedUntil = i;
                const MStringView bodyView(macro.body.data(), macro.body.size());
                usize b = 0u;
                while(b < macro.body.size()){
                    if(macro.body[b] == '"' || macro.body[b] == '\''){
                        b = AppendQuotedLiteral(bodyView, b, next);
                        continue;
                    }
                    if(macro.isVariadic && b + 2u < macro.body.size() && macro.body[b] == '.' && macro.body[b + 1u] == '.' && macro.body[b + 2u] == '.'){
                        next.append(vaArgsText.data(), vaArgsText.size());
                        b += 3u;
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
                        if(macro.isVariadic && tokenName == "__VA_ARGS__"){
                            next.append(vaArgsText.data(), vaArgsText.size());
                            continue;
                        }
                        b8 matchedParam = false;
                        for(usize pIdx = 0u; pIdx < macro.parameters.size(); ++pIdx){
                            if(MStringView(macro.parameters[pIdx]) == tokenName){
                                next.append(expandedArgs[pIdx].data(), expandedArgs[pIdx].size());
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
                ++i;
            }
        }

        if(!anyReplaced)
            break;
        if(copiedUntil < currentView.size())
            next.append(currentView.data() + copiedUntil, currentView.size() - copiedUntil);
        current = Move(next);
        currentView = MStringView(current.data(), current.size());
        hasOwnedCurrent = true;
    }

    if(hasOwnedCurrent){
        outStorage = Move(current);
        outView = MStringView(outStorage.data(), outStorage.size());
    }
    else
        outView = lineText;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

