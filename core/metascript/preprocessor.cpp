// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor.h"
#include "preprocessor_macro_expander.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_preprocessor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MaxIncludeDepth = 64u;

[[nodiscard]] MStringView StripLineComment(const MStringView line, MString& outRewritten)noexcept{
    b8 inString = false;
    b8 inChar = false;
    for(usize i = 0u; i < line.size(); ++i){
        if(!inString && !inChar && line[i] == '"'){
            MStringView rawDelim;
            if(Preprocessor::TryParseRawStringOpening(line, i, rawDelim)){
                const usize bodyStart = i + 1u + rawDelim.size() + 1u;
                usize afterClose = 0u;
                if(!Preprocessor::FindRawStringClosing(line, bodyStart, rawDelim, afterClose))
                    return line;
                i = afterClose - 1u;
                continue;
            }
        }
        if(!inChar && line[i] == '"' && !IsEscapedQuoteAt(line, i))
            inString = !inString;
        else if(!inString && line[i] == '\'' && !IsEscapedQuoteAt(line, i))
            inChar = !inChar;
        if(!inString && !inChar && i + 1u < line.size() && line[i] == '/' && line[i + 1u] == '/')
            return line.substr(0u, i);
        if(!inString && !inChar && i + 1u < line.size() && line[i] == '/' && line[i + 1u] == '*'){
            const usize endComment = line.find("*/", i + 2u);
            if(endComment == MStringView::npos || TrimView(line.substr(endComment + 2u)).empty())
                return line.substr(0u, i);
            MString tailRewritten(outRewritten.get_allocator());
            const MStringView tail = StripLineComment(line.substr(endComment + 2u), tailRewritten);
            outRewritten.clear();
            outRewritten.reserve(i + 1u + tail.size());
            outRewritten.append(line.data(), i);
            outRewritten.push_back(' ');
            outRewritten.append(tail.data(), tail.size());
            return MStringView(outRewritten.data(), outRewritten.size());
        }
    }
    return line;
}

[[nodiscard]] b8 HasUnclosedFunctionMacroCall(const MStringView text, const Preprocessor::MacroMap& macros)noexcept{
    usize i = 0u;
    while(i < text.size()){
        if(text[i] == '"'){
            MStringView rawDelim;
            if(Preprocessor::TryParseRawStringOpening(text, i, rawDelim)){
                const usize bodyStart = i + 1u + rawDelim.size() + 1u;
                usize afterClose = 0u;
                if(!Preprocessor::FindRawStringClosing(text, bodyStart, rawDelim, afterClose))
                    break;
                i = afterClose;
                continue;
            }
        }
        if(text[i] == '"' || text[i] == '\''){
            i = SkipQuotedLiteral(text, i);
            continue;
        }
        if(i + 1u < text.size() && text[i] == '/' && text[i + 1u] == '/')
            break;
        if(i + 1u < text.size() && text[i] == '/' && text[i + 1u] == '*'){
            const usize closeIdx = text.find("*/", i + 2u);
            if(closeIdx == MStringView::npos)
                break;
            i = closeIdx + 2u;
            continue;
        }
        if(IsAsciiIdentifierStart(text[i])){
            const usize idStart = i;
            while(i < text.size() && IsAsciiIdentifierChar(text[i]))
                ++i;
            const MStringView ident = text.substr(idStart, i - idStart);
            const auto it = macros.find(ident);
            if(it == macros.end() || !it.value().isFunctionLike)
                continue;

            usize lookahead = i;
            while(lookahead < text.size() && (text[lookahead] == ' ' || text[lookahead] == '\t'))
                ++lookahead;
            if(lookahead >= text.size() || text[lookahead] != '(')
                continue;

            u32 parenDepth = 1u;
            usize scan = lookahead + 1u;
            while(scan < text.size() && parenDepth > 0u){
                const MChar ch = text[scan];
                if(ch == '\'' && scan > 0u && (IsAsciiDigit(text[scan - 1u]) || IsAsciiIdentifierChar(text[scan - 1u]))){
                    ++scan;
                    continue;
                }
                if(ch == '"'){
                    MStringView rawDelim;
                    if(Preprocessor::TryParseRawStringOpening(text, scan, rawDelim)){
                        const usize bodyStart = scan + 1u + rawDelim.size() + 1u;
                        usize afterClose = 0u;
                        if(!Preprocessor::FindRawStringClosing(text, bodyStart, rawDelim, afterClose)){
                            scan = text.size();
                            break;
                        }
                        scan = afterClose;
                        continue;
                    }
                }
                if(ch == '"' || ch == '\''){
                    scan = SkipQuotedLiteral(text, scan);
                    continue;
                }
                if(scan + 1u < text.size() && ch == '/' && text[scan + 1u] == '/')
                    break;
                if(scan + 1u < text.size() && ch == '/' && text[scan + 1u] == '*'){
                    const usize closeComment = text.find("*/", scan + 2u);
                    if(closeComment == MStringView::npos)
                        break;
                    scan = closeComment + 2u;
                    continue;
                }
                if(ch == '(')
                    ++parenDepth;
                else if(ch == ')')
                    --parenDepth;
                ++scan;
            }
            if(parenDepth > 0u)
                return true;
            i = scan;
            continue;
        }
        ++i;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize Preprocessor::AppendPreprocessorNumber(const MStringView text, usize index, const b8 allowSignedExponent, MString& out){
    while(index < text.size()){
        const MChar c = text[index];
        if(allowSignedExponent && (c == 'e' || c == 'E' || c == 'p' || c == 'P') && index + 1u < text.size() && (text[index + 1u] == '+' || text[index + 1u] == '-')){
            out.push_back(text[index++]);
            out.push_back(text[index++]);
            continue;
        }
        if(c == '\'' && index + 1u < text.size() && (IsAsciiDigit(text[index + 1u]) || IsAsciiIdentifierChar(text[index + 1u]))){
            out.push_back(text[index++]);
            out.push_back(text[index++]);
            continue;
        }
        if(IsAsciiDigit(c) || IsAsciiIdentifierChar(c) || c == '.'){
            out.push_back(text[index++]);
            continue;
        }
        break;
    }
    return index;
}

b8 Preprocessor::TryParseRawStringOpening(const MStringView text, const usize quoteIndex, MStringView& outDelimiter)noexcept{
    if(quoteIndex == 0u || quoteIndex >= text.size() || text[quoteIndex] != '"' || text[quoteIndex - 1u] != 'R')
        return false;

    usize prefixStart = quoteIndex - 1u;
    while(prefixStart > 0u && IsAsciiIdentifierChar(text[prefixStart - 1u]))
        --prefixStart;

    const MStringView prefix = text.substr(prefixStart, (quoteIndex - 1u) - prefixStart);
    if(!prefix.empty() && prefix != "L" && prefix != "u" && prefix != "U" && prefix != "u8")
        return false;

    const usize delimStart = quoteIndex + 1u;
    usize cursor = delimStart;
    while(cursor < text.size() && (cursor - delimStart) <= 16u){
        const MChar c = text[cursor];
        if(c == '('){
            outDelimiter = text.substr(delimStart, cursor - delimStart);
            return true;
        }
        if(c == ')' || c == '\\' || c == '"' || c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n')
            return false;
        ++cursor;
    }
    return false;
}

b8 Preprocessor::FindRawStringClosing(const MStringView text, const usize searchStart, const MStringView delimiter, usize& outAfterCloseQuote)noexcept{
    const usize closeSeqLen = delimiter.size() + 2u;
    if(searchStart + closeSeqLen > text.size())
        return false;
    for(usize pos = searchStart; pos + closeSeqLen <= text.size(); ++pos){
        if(text[pos] == ')' && text[pos + 1u + delimiter.size()] == '"' && text.substr(pos + 1u, delimiter.size()) == delimiter){
            outAfterCloseQuote = pos + closeSeqLen;
            return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Preprocessor::Preprocessor(MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, IIncludeResolver* includeResolver, const b8 allowNwbInclude)
    : m_arena(arena)
    , m_scratchArena(scratchArena)
    , m_errors(errors)
    , m_macros(0, TransparentStringHash<MString>(), TransparentStringEqual<MString>(), arena)
    , m_includes(arena, errors, includeResolver, allowNwbInclude)
{
#if defined(NWB_DEBUG) && NWB_DEBUG
    defineObjectMacro("NWB_DEBUG", "1");
#endif
#if defined(NWB_OPTIMIZE) && NWB_OPTIMIZE
    defineObjectMacro("NWB_OPTIMIZE", "1");
#endif
#if defined(NWB_FINAL) && NWB_FINAL
    defineObjectMacro("NWB_FINAL", "1");
#endif
}


void Preprocessor::defineObjectMacro(const MStringView name, const MStringView body){
    PreprocessorMacro macro(m_arena);
    macro.name.assign(name.data(), name.size());
    macro.body.assign(body.data(), body.size());
    macro.isFunctionLike = false;
    MString key(name.data(), name.size(), m_arena);
    m_macros.insert_or_assign(Move(key), Move(macro));
}

void Preprocessor::undefineMacro(const MStringView name)noexcept{
    const auto it = m_macros.find(name);
    if(it != m_macros.end())
        m_macros.erase(it);
}

b8 Preprocessor::isOffsetInIncludedRange(const usize offset)const noexcept{
    return m_includes.containsOffset(offset);
}

b8 Preprocessor::process(const MStringView source, MString& outProcessed){
    outProcessed.clear();
    outProcessed.reserve(source.size());
    m_includes.clearRanges();
    return processInternal(source, 0u, "<root>", outProcessed);
}

b8 Preprocessor::processInternal(const MStringView source, const u32 depth, const MStringView currentFileKey, MString& outProcessed){
    using namespace __hidden_metascript_preprocessor;

    if(depth > s_MaxIncludeDepth){
        m_errors.push_back(ParseError{1u, 1u, MString("maximum #include nesting depth exceeded", m_arena)});
        return false;
    }

    Vector<ConditionalFrame, Alloc::ScratchArena> condStack{m_scratchArena};
    condStack.reserve(8u);

    b8 inBlockComment = false;
    b8 inRawString = false;
    MString rawStringDelimiter(m_arena);
    b8 inNativeKeywordPendingBrace = false;
    u32 nativeBraceDepth = 0u;
    u32 currentLine = 1u;
    usize pos = 0u;

    const auto updateLineScanState = [&](const MStringView lineToScan, const usize startOffset, const b8 updateNativeBraces){
        for(usize scanIdx = startOffset; scanIdx < lineToScan.size();){
            const MChar ch = lineToScan[scanIdx];
            if(ch == '/' && scanIdx + 1u < lineToScan.size() && lineToScan[scanIdx + 1u] == '/')
                break;
            if(ch == '/' && scanIdx + 1u < lineToScan.size() && lineToScan[scanIdx + 1u] == '*'){
                const usize closeComment = lineToScan.find("*/", scanIdx + 2u);
                if(closeComment == MStringView::npos){
                    inBlockComment = true;
                    break;
                }
                scanIdx = closeComment + 2u;
                continue;
            }
            if(ch == '\'' && scanIdx > startOffset && (IsAsciiDigit(lineToScan[scanIdx - 1u]) || IsAsciiIdentifierChar(lineToScan[scanIdx - 1u]))){
                ++scanIdx;
                continue;
            }
            if(ch == '"'){
                MStringView rawDelim;
                if(TryParseRawStringOpening(lineToScan, scanIdx, rawDelim)){
                    const usize bodyStart = scanIdx + 1u + rawDelim.size() + 1u;
                    usize afterClose = 0u;
                    if(!FindRawStringClosing(lineToScan, bodyStart, rawDelim, afterClose)){
                        inRawString = true;
                        rawStringDelimiter.assign(rawDelim.data(), rawDelim.size());
                        break;
                    }
                    scanIdx = afterClose;
                    continue;
                }
            }
            if(ch == '"' || ch == '\''){
                scanIdx = SkipQuotedLiteral(lineToScan, scanIdx);
                continue;
            }
            if(IsAsciiIdentifierStart(ch)){
                const usize idStart = scanIdx;
                while(scanIdx < lineToScan.size() && IsAsciiIdentifierChar(lineToScan[scanIdx]))
                    ++scanIdx;
                if(updateNativeBraces){
                    const MStringView idText = lineToScan.substr(idStart, scanIdx - idStart);
                    if(idText == "native" && nativeBraceDepth == 0u)
                        inNativeKeywordPendingBrace = true;
                }
                continue;
            }
            if(updateNativeBraces){
                if(ch == '{'){
                    if(inNativeKeywordPendingBrace || nativeBraceDepth > 0u){
                        ++nativeBraceDepth;
                        inNativeKeywordPendingBrace = false;
                    }
                }
                else if(ch == '}'){
                    if(nativeBraceDepth > 0u)
                        --nativeBraceDepth;
                }
                else if(ch == ';' && nativeBraceDepth == 0u){
                    inNativeKeywordPendingBrace = false;
                }
            }
            ++scanIdx;
        }
    };

    while(pos < source.size()){
        usize lineEnd = pos;
        while(lineEnd < source.size() && source[lineEnd] != '\n')
            ++lineEnd;

        MStringView rawLine = source.substr(pos, lineEnd - pos);
        if(!rawLine.empty() && rawLine.back() == '\r')
            rawLine.remove_suffix(1u);

        const b8 isActive = condStack.empty() || condStack.back().currentActive;
        const u32 startLine = currentLine;
        u32 joinedLines = 0u;

        MString joinedBuffer(m_arena);
        if(!inBlockComment && !inRawString && nativeBraceDepth == 0u){
            b8 hasBackslashCont = !rawLine.empty() && rawLine.back() == '\\';
            const MStringView initialTrimmed = TrimView(rawLine);
            const b8 isDirectiveLine = !initialTrimmed.empty() && initialTrimmed[0] == '#';
            b8 hasUnclosedMacro = !hasBackslashCont && !isDirectiveLine && isActive && HasUnclosedFunctionMacroCall(rawLine, m_macros);

            if(hasBackslashCont || hasUnclosedMacro){
                if(hasBackslashCont){
                    joinedBuffer.append(rawLine.data(), rawLine.size() - 1u);
                }
                else{
                    MString rewrittenFirst(m_arena);
                    const MStringView strippedFirst = StripLineComment(rawLine, rewrittenFirst);
                    joinedBuffer.append(strippedFirst.data(), strippedFirst.size());
                }

                while((hasBackslashCont || hasUnclosedMacro) && lineEnd < source.size()){
                    pos = lineEnd + 1u;
                    ++currentLine;
                    ++joinedLines;
                    lineEnd = pos;
                    while(lineEnd < source.size() && source[lineEnd] != '\n')
                        ++lineEnd;

                    MStringView nextRaw = source.substr(pos, lineEnd - pos);
                    if(!nextRaw.empty() && nextRaw.back() == '\r')
                        nextRaw.remove_suffix(1u);

                    hasBackslashCont = !nextRaw.empty() && nextRaw.back() == '\\';
                    if(!joinedBuffer.empty() && joinedBuffer.back() != ' ' && joinedBuffer.back() != '\t')
                        joinedBuffer.push_back(' ');
                    if(hasBackslashCont){
                        joinedBuffer.append(nextRaw.data(), nextRaw.size() - 1u);
                    }
                    else{
                        MString rewrittenNext(m_arena);
                        const MStringView strippedNext = StripLineComment(nextRaw, rewrittenNext);
                        joinedBuffer.append(strippedNext.data(), strippedNext.size());
                    }

                    hasUnclosedMacro = !hasBackslashCont && !isDirectiveLine && isActive && HasUnclosedFunctionMacroCall(MStringView(joinedBuffer.data(), joinedBuffer.size()), m_macros);
                }
                rawLine = MStringView(joinedBuffer.data(), joinedBuffer.size());
            }
        }

        MStringView effectiveLine = rawLine;
        MStringView commentPrefix;
        if(inRawString){
            usize afterClose = 0u;
            if(!FindRawStringClosing(rawLine, 0u, MStringView(rawStringDelimiter.data(), rawStringDelimiter.size()), afterClose)){
                if(isActive)
                    outProcessed.append(rawLine.data(), rawLine.size());
                outProcessed.push_back('\n');
                pos = (lineEnd < source.size()) ? (lineEnd + 1u) : lineEnd;
                ++currentLine;
                continue;
            }
            commentPrefix = rawLine.substr(0u, afterClose);
            effectiveLine = rawLine.substr(afterClose);
            inRawString = false;
            rawStringDelimiter.clear();
        }
        else if(inBlockComment){
            const usize closeIdx = rawLine.find("*/");
            if(closeIdx == MStringView::npos){
                if(isActive)
                    outProcessed.append(rawLine.data(), rawLine.size());
                outProcessed.push_back('\n');
                pos = (lineEnd < source.size()) ? (lineEnd + 1u) : lineEnd;
                ++currentLine;
                continue;
            }
            commentPrefix = rawLine.substr(0u, closeIdx + 2u);
            effectiveLine = rawLine.substr(closeIdx + 2u);
            inBlockComment = false;
        }

        MStringView directiveCandidate = TrimView(effectiveLine);
        while(directiveCandidate.size() >= 4u && directiveCandidate[0] == '/' && directiveCandidate[1] == '*'){
            const usize closeInlineComment = directiveCandidate.find("*/", 2u);
            if(closeInlineComment == MStringView::npos)
                break;
            directiveCandidate = TrimView(directiveCandidate.substr(closeInlineComment + 2u));
        }
        const MStringView trimmed = directiveCandidate;

        if(!trimmed.empty() && trimmed[0] == '#'){
            if(nativeBraceDepth > 0u){
                if(isActive){
                    updateLineScanState(rawLine, commentPrefix.size(), true);
                    outProcessed.append(rawLine.data(), rawLine.size());
                }
                outProcessed.push_back('\n');
            }
            else{
                const MStringView afterHash = TrimView(trimmed.substr(1u));
                if(isActive && !commentPrefix.empty())
                    outProcessed.append(commentPrefix.data(), commentPrefix.size());
                if(!handleDirective(afterHash, startLine, depth, currentFileKey, condStack, outProcessed))
                    return false;
                outProcessed.push_back('\n');
            }
        }
        else if(isActive){
            MString expandedLine(m_arena);
            if(!commentPrefix.empty())
                expandedLine.append(commentPrefix.data(), commentPrefix.size());
            MString expandedTail(m_arena);
            PreprocessorMacroExpander macroExpander(m_arena, m_macros, m_errors);
            if(!macroExpander.expand(effectiveLine, startLine, expandedTail, inNativeKeywordPendingBrace, nativeBraceDepth))
                return false;
            expandedLine.append(expandedTail.data(), expandedTail.size());

            updateLineScanState(MStringView(expandedLine.data(), expandedLine.size()), commentPrefix.size(), true);

            outProcessed.append(expandedLine.data(), expandedLine.size());
            outProcessed.push_back('\n');
        }
        else{
            updateLineScanState(effectiveLine, 0u, false);
            outProcessed.push_back('\n');
        }

        for(u32 extraNl = 0u; extraNl < joinedLines; ++extraNl)
            outProcessed.push_back('\n');

        pos = (lineEnd < source.size()) ? (lineEnd + 1u) : lineEnd;
        ++currentLine;
    }

    if(!condStack.empty()){
        m_errors.push_back(ParseError{condStack.back().line, 1u, MString("unterminated preprocessor conditional (#if/#ifdef/#ifndef without #endif)", m_arena)});
        return false;
    }

    return true;
}

b8 Preprocessor::handleDirective(const MStringView directiveLine, const u32 line, const u32 depth, const MStringView currentFileKey, Vector<ConditionalFrame, Alloc::ScratchArena>& condStack, MString& outProcessed){
    using namespace __hidden_metascript_preprocessor;

    MString rewrittenDirectiveBuf(m_arena);
    const MStringView cleanLine = TrimView(StripLineComment(directiveLine, rewrittenDirectiveBuf));
    usize cmdEnd = 0u;
    while(cmdEnd < cleanLine.size() && IsAsciiIdentifierChar(cleanLine[cmdEnd]))
        ++cmdEnd;

    const MStringView cmd = cleanLine.substr(0u, cmdEnd);
    const MStringView rest = TrimView(cleanLine.substr(cmdEnd));
    const b8 parentActive = condStack.empty() || condStack.back().currentActive;

    if(cmd == "ifdef" || cmd == "ifndef" || cmd == "if"){
        ConditionalFrame frame;
        frame.line = line;
        frame.parentActive = parentActive;
        if(!parentActive){
            frame.branchTaken = true;
            frame.currentActive = false;
            condStack.push_back(frame);
            return true;
        }
        b8 condResult = false;
        if(cmd == "if"){
            if(!evaluateCondition(rest, line, condResult))
                return false;
        }
        else{
            if(rest.empty() || !IsAsciiIdentifierStart(rest[0])){
                m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #ifdef/#ifndef", m_arena)});
                return false;
            }
            const MStringView macroName = ExtractLeadingIdentifier(rest);
            if(!TrimView(rest.substr(macroName.size())).empty()){
                m_errors.push_back(ParseError{line, 1u, MString("unexpected tokens after macro name in #ifdef/#ifndef", m_arena)});
                return false;
            }
            const b8 defined = m_macros.find(macroName) != m_macros.end();
            condResult = (cmd == "ifdef") ? defined : !defined;
        }
        frame.currentActive = condResult;
        frame.branchTaken = condResult;
        condStack.push_back(frame);
        return true;
    }

    if(cmd == "elif"){
        if(condStack.empty() || condStack.back().seenElse){
            m_errors.push_back(ParseError{line, 1u, MString("unexpected #elif", m_arena)});
            return false;
        }
        ConditionalFrame& frame = condStack.back();
        if(!frame.parentActive || frame.branchTaken){
            frame.currentActive = false;
            return true;
        }
        b8 condResult = false;
        if(!evaluateCondition(rest, line, condResult))
            return false;
        frame.currentActive = condResult;
        frame.branchTaken = condResult;
        return true;
    }

    if(cmd == "else"){
        if(condStack.empty() || condStack.back().seenElse){
            m_errors.push_back(ParseError{line, 1u, MString("unexpected #else", m_arena)});
            return false;
        }
        ConditionalFrame& frame = condStack.back();
        frame.seenElse = true;
        frame.currentActive = frame.parentActive && !frame.branchTaken;
        frame.branchTaken = true;
        return true;
    }

    if(cmd == "endif"){
        if(condStack.empty()){
            m_errors.push_back(ParseError{line, 1u, MString("unexpected #endif without matching #if", m_arena)});
            return false;
        }
        condStack.pop_back();
        return true;
    }

    if(!parentActive)
        return true;

    if(cmd == "define")
        return handleDefine(rest, line);
    if(cmd == "undef")
        return handleUndef(rest, line);
    if(cmd == "include")
        return m_includes.process(*this, rest, line, depth, currentFileKey, outProcessed);

    m_errors.push_back(ParseError{line, 1u, MString("unknown preprocessor directive", m_arena)});
    return false;
}

b8 Preprocessor::handleDefine(const MStringView rest, const u32 line){
    using namespace __hidden_metascript_preprocessor;

    if(rest.empty() || !IsAsciiIdentifierStart(rest[0])){
        m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #define", m_arena)});
        return false;
    }

    const MStringView name = ExtractLeadingIdentifier(rest);
    if(name == "defined"){
        m_errors.push_back(ParseError{line, 1u, MString("'defined' cannot be used as a macro name", m_arena)});
        return false;
    }
    const usize nameEnd = name.size();
    PreprocessorMacro macro(m_arena);
    macro.name.assign(name.data(), name.size());

    if(nameEnd < rest.size() && rest[nameEnd] == '('){
        macro.isFunctionLike = true;
        const usize closeParen = rest.find(')', nameEnd + 1u);
        if(closeParen == MStringView::npos){
            m_errors.push_back(ParseError{line, 1u, MString("unterminated parameter list in function-like #define", m_arena)});
            return false;
        }
        const MStringView paramList = TrimView(rest.substr(nameEnd + 1u, closeParen - (nameEnd + 1u)));
        if(!paramList.empty()){
            usize pStart = 0u;
            for(;;){
                usize comma = paramList.find(',', pStart);
                const b8 hasComma = (comma != MStringView::npos);
                if(!hasComma)
                    comma = paramList.size();
                const MStringView paramName = TrimView(paramList.substr(pStart, comma - pStart));
                if(paramName == "..." && !hasComma){
                    macro.isVariadic = true;
                    break;
                }
                b8 validParam = !paramName.empty() && IsAsciiIdentifierStart(paramName[0]);
                for(usize cIdx = 1u; validParam && cIdx < paramName.size(); ++cIdx){
                    if(!IsAsciiIdentifierChar(paramName[cIdx]))
                        validParam = false;
                }
                for(usize existingIdx = 0u; validParam && existingIdx < macro.parameters.size(); ++existingIdx){
                    if(MStringView(macro.parameters[existingIdx].data(), macro.parameters[existingIdx].size()) == paramName)
                        validParam = false;
                }
                if(!validParam){
                    m_errors.push_back(ParseError{line, 1u, MString("invalid parameter name in function-like #define", m_arena)});
                    return false;
                }
                macro.parameters.push_back(MString(paramName.data(), paramName.size(), m_arena));
                if(!hasComma)
                    break;
                pStart = comma + 1u;
            }
        }
        const MStringView body = TrimView(rest.substr(closeParen + 1u));
        macro.body.assign(body.data(), body.size());
    }
    else{
        const MStringView body = TrimView(rest.substr(nameEnd));
        macro.body.assign(body.data(), body.size());
    }

    MString key(name.data(), name.size(), m_arena);
    m_macros.insert_or_assign(Move(key), Move(macro));
    return true;
}

b8 Preprocessor::handleUndef(const MStringView rest, const u32 line){
    if(rest.empty() || !IsAsciiIdentifierStart(rest[0])){
        m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #undef", m_arena)});
        return false;
    }
    const MStringView name = ExtractLeadingIdentifier(rest);
    if(name == "defined"){
        m_errors.push_back(ParseError{line, 1u, MString("'defined' cannot be undefined", m_arena)});
        return false;
    }
    if(!TrimView(rest.substr(name.size())).empty()){
        m_errors.push_back(ParseError{line, 1u, MString("unexpected tokens after macro name in #undef", m_arena)});
        return false;
    }
    undefineMacro(name);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

