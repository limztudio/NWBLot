// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor_macro_expander.h"
#include "preprocessor_semantics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize PreprocessorMacroExpander::AppendDefinedOperand(const MStringView source, const usize i, MString& out){
    const usize end = PreprocessorSemantics::SkipDefinedOperand(source, i);
    if(end > i)
        out.append(source.data() + i, end - i);
    return end;
}


b8 PreprocessorMacroExpander::expand(const MStringView lineText, const u32 line, MString& outExpanded){
    MString storage(m_arena);
    MStringView expanded;
    if(!expandToView(lineText, line, storage, expanded))
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

b8 PreprocessorMacroExpander::expandToView(const MStringView lineText, const u32 line, MString& outStorage, MStringView& outView, LineScanOutcome* const outScanOutcome){
    constexpr u32 s_MaxMacroExpansionPasses = 16u;
    if(outScanOutcome){
        outScanOutcome->replaced = false;
        outScanOutcome->enteredBlockComment = false;
        outScanOutcome->enteredRawString = false;
        outScanOutcome->rawStringDelimiter = {};
    }

    MString current(m_arena);
    MStringView currentView = lineText;
    b8 hasOwnedCurrent = false;

    for(u32 pass = 0u; pass < s_MaxMacroExpansionPasses; ++pass){
        b8 anyReplaced = false;
        MString next(m_arena);
        usize copiedUntil = 0u;

        usize i = 0u;
        while(i < currentView.size()){
            const usize stepStart = i;
            b8 hitTerminator = false;
            if(PreprocessorSemantics::AdvancePreprocessorScanStep(currentView, i, 0u, true, hitTerminator)){
                if(hitTerminator){
                    if(outScanOutcome && pass == 0u && !anyReplaced){
                        if(currentView[stepStart] == '"' && stepStart > 0u){
                            MStringView delim;
                            if(PreprocessorSemantics::TryParseRawStringOpening(currentView, stepStart, delim)){
                                outScanOutcome->enteredRawString = true;
                                outScanOutcome->rawStringDelimiter = delim;
                            }
                        }
                        else if(stepStart + 1u < currentView.size() && currentView[stepStart] == '/' && currentView[stepStart + 1u] == '*'){
                            outScanOutcome->enteredBlockComment = true;
                        }
                    }
                    break;
                }
                continue;
            }

            if(IsAsciiDigit(currentView[i])){
                i = PreprocessorSemantics::SkipPreprocessorNumber(currentView, i, true);
                continue;
            }

            if(IsAsciiIdentifierStart(currentView[i])){
                const usize idStart = i;
                while(i < currentView.size() && IsAsciiIdentifierChar(currentView[i]))
                    ++i;
                const usize idLen = i - idStart;
                const MStringView ident(currentView.data() + idStart, idLen);
                if(m_filter && (idLen < m_filter->minLen || idLen > m_filter->maxLen || m_filter->firstCharMask[static_cast<u8>(currentView[idStart])] == 0u)){
                    if(idLen == PreprocessorSemantics::s_DefinedOperator.size() && ident == PreprocessorSemantics::s_DefinedOperator)
                        i = PreprocessorSemantics::SkipDefinedOperand(currentView, i);
                    continue;
                }
                const auto it = m_macros.find(ident);
                if(it == m_macros.end()){
                    if(ident == PreprocessorSemantics::s_DefinedOperator)
                        i = PreprocessorSemantics::SkipDefinedOperand(currentView, i);
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
                    b8 argTerminator = false;
                    if(PreprocessorSemantics::AdvancePreprocessorScanStep(currentView, argScan, lookahead + 1u, false, argTerminator)){
                        if(argTerminator)
                            break;
                        continue;
                    }
                    const MChar ch = currentView[argScan];
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
                    constexpr usize s_EllipsisLen = PreprocessorSemantics::s_VariadicEllipsis.size();
                    if(macro.isVariadic && b + s_EllipsisLen <= macro.body.size() && bodyView.substr(b, s_EllipsisLen) == PreprocessorSemantics::s_VariadicEllipsis){
                        next.append(vaArgsText.data(), vaArgsText.size());
                        b += s_EllipsisLen;
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
                        if(macro.isVariadic && tokenName == PreprocessorSemantics::s_VariadicArgsMacro){
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
        if(outScanOutcome)
            outScanOutcome->replaced = true;
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

