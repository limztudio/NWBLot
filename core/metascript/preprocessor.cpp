// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor.h"
#include "preprocessor_macro_expander.h"
#include "preprocessor_semantics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_preprocessor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MaxIncludeDepth = 64u;

[[nodiscard]] b8 ParseSingleMacroDirectiveName(
    const MStringView rest,
    const u32 line,
    MetaArena& arena,
    MVector<ParseError>& errors,
    const MStringView expectedMsg,
    const MStringView trailingMsg,
    const b8 rejectDefined,
    MStringView& outName
){
    if(rest.empty() || !IsAsciiIdentifierStart(rest[0])){
        errors.push_back(ParseError{line, 1u, MString(expectedMsg.data(), expectedMsg.size(), arena)});
        return false;
    }
    outName = ExtractLeadingIdentifier(rest);
    if(rejectDefined && outName == PreprocessorSemantics::s_DefinedOperator){
        errors.push_back(ParseError{line, 1u, MString("'defined' cannot be undefined", arena)});
        return false;
    }
    if(!TrimView(rest.substr(outName.size())).empty()){
        errors.push_back(ParseError{line, 1u, MString(trailingMsg.data(), trailingMsg.size(), arena)});
        return false;
    }
    return true;
}

[[nodiscard]] MStringView StripLineComment(const MStringView line, MString& outRewritten)noexcept{
    b8 inString = false;
    b8 inChar = false;
    for(usize i = 0u; i < line.size(); ++i){
        if(!inString && !inChar && line[i] == '"'){
            usize afterClose = 0u;
            b8 unterminated = false;
            if(PreprocessorSemantics::TrySkipRawStringLiteral(line, i, afterClose, unterminated)){
                if(unterminated)
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
        b8 hitTerminator = false;
        if(PreprocessorSemantics::AdvancePreprocessorScanStep(text, i, 0u, true, hitTerminator)){
            if(hitTerminator)
                break;
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
                b8 innerTerminator = false;
                if(PreprocessorSemantics::AdvancePreprocessorScanStep(text, scan, 0u, true, innerTerminator)){
                    if(innerTerminator)
                        break;
                    continue;
                }
                const MChar ch = text[scan];
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


usize Preprocessor::AppendPreprocessorNumber(const MStringView text, const usize index, const b8 allowSignedExponent, MString& out){
    const usize end = PreprocessorSemantics::SkipPreprocessorNumber(text, index, allowSignedExponent);
    if(end > index)
        out.append(text.data() + index, end - index);
    return end;
}

b8 Preprocessor::TryParseRawStringOpening(const MStringView text, const usize quoteIndex, MStringView& outDelimiter)noexcept{
    return PreprocessorSemantics::TryParseRawStringOpening(text, quoteIndex, outDelimiter);
}

b8 Preprocessor::FindRawStringClosing(const MStringView text, const usize searchStart, const MStringView delimiter, usize& outAfterCloseQuote)noexcept{
    return PreprocessorSemantics::FindRawStringClosing(text, searchStart, delimiter, outAfterCloseQuote);
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
    defineObjectMacro(PreprocessorSemantics::s_DebugConfigMacro, PreprocessorSemantics::s_DefaultDefinedMacroBody);
#endif
#if defined(NWB_OPTIMIZE) && NWB_OPTIMIZE
    defineObjectMacro(PreprocessorSemantics::s_OptimizeConfigMacro, PreprocessorSemantics::s_DefaultDefinedMacroBody);
#endif
#if defined(NWB_FINAL) && NWB_FINAL
    defineObjectMacro(PreprocessorSemantics::s_FinalConfigMacro, PreprocessorSemantics::s_DefaultDefinedMacroBody);
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

MStringView Preprocessor::resolveIncludedNamespaceAtOffset(const usize offset)const noexcept{
    return m_includes.resolveNamespaceAtOffset(offset);
}

b8 Preprocessor::process(const MStringView source, MString& outProcessed){
    outProcessed.clear();
    outProcessed.reserve(source.size());
    m_includes.clearRanges();
    return processInternal(source, 0u, PreprocessorSemantics::s_RootFileKey, outProcessed);
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
    u32 currentLine = 1u;
    usize pos = 0u;

    const auto updateLineScanState = [&](const MStringView lineToScan, const usize startOffset){
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
                while(scanIdx < lineToScan.size() && IsAsciiIdentifierChar(lineToScan[scanIdx]))
                    ++scanIdx;
                continue;
            }
            ++scanIdx;
        }
    };

    const auto readTrimmedLineAt = [&](const usize start, usize& outLineEnd)noexcept -> MStringView{
        outLineEnd = start;
        while(outLineEnd < source.size() && source[outLineEnd] != '\n')
            ++outLineEnd;
        MStringView line = source.substr(start, outLineEnd - start);
        if(!line.empty() && line.back() == '\r')
            line.remove_suffix(1u);
        return line;
    };

    while(pos < source.size()){
        usize lineEnd = pos;
        MStringView rawLine = readTrimmedLineAt(pos, lineEnd);

        const b8 isActive = condStack.empty() || condStack.back().currentActive;
        const u32 startLine = currentLine;
        u32 joinedLines = 0u;

        MString joinedBuffer(m_arena);
        if(!inBlockComment && !inRawString){
            b8 hasBackslashCont = !rawLine.empty() && rawLine.back() == '\\';
            const MStringView initialTrimmed = TrimView(rawLine);
            const b8 isDirectiveLine = !initialTrimmed.empty() && initialTrimmed[0] == '#';
            b8 hasUnclosedMacro = !hasBackslashCont && !isDirectiveLine && isActive && HasUnclosedFunctionMacroCall(rawLine, m_macros);

            if(hasBackslashCont || hasUnclosedMacro){
                const auto appendJoinedSegment = [&](const MStringView segment, const b8 hasBackslash){
                    if(!joinedBuffer.empty() && joinedBuffer.back() != ' ' && joinedBuffer.back() != '\t')
                        joinedBuffer.push_back(' ');
                    if(hasBackslash){
                        joinedBuffer.append(segment.data(), segment.size() - 1u);
                        return;
                    }
                    MString rewrittenSegment(m_arena);
                    const MStringView strippedSegment = StripLineComment(segment, rewrittenSegment);
                    joinedBuffer.append(strippedSegment.data(), strippedSegment.size());
                };

                appendJoinedSegment(rawLine, hasBackslashCont);

                while((hasBackslashCont || hasUnclosedMacro) && lineEnd < source.size()){
                    pos = lineEnd + 1u;
                    ++currentLine;
                    ++joinedLines;
                    const MStringView nextRaw = readTrimmedLineAt(pos, lineEnd);
                    hasBackslashCont = !nextRaw.empty() && nextRaw.back() == '\\';
                    appendJoinedSegment(nextRaw, hasBackslashCont);
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
            const MStringView afterHash = TrimView(trimmed.substr(1u));
            if(isActive && !commentPrefix.empty())
                outProcessed.append(commentPrefix.data(), commentPrefix.size());
            if(!handleDirective(afterHash, startLine, depth, currentFileKey, condStack, outProcessed))
                return false;
            outProcessed.push_back('\n');
        }
        else if(isActive){
            MString expandedTail(m_arena);
            MStringView expandedView = effectiveLine;
            if(!m_macros.empty()){
                PreprocessorMacroExpander macroExpander(m_arena, m_macros, m_errors);
                if(!macroExpander.expandToView(effectiveLine, startLine, expandedTail, expandedView))
                    return false;
            }
            if(commentPrefix.empty()){
                updateLineScanState(expandedView, 0u);
                outProcessed.append(expandedView.data(), expandedView.size());
            }
            else{
                MString expandedLine(m_arena);
                expandedLine.append(commentPrefix.data(), commentPrefix.size());
                expandedLine.append(expandedView.data(), expandedView.size());
                updateLineScanState(MStringView(expandedLine.data(), expandedLine.size()), commentPrefix.size());
                outProcessed.append(expandedLine.data(), expandedLine.size());
            }
            outProcessed.push_back('\n');
        }
        else{
            updateLineScanState(effectiveLine, 0u);
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
    const PreprocessorDirectiveKind::Enum kind = PreprocessorSemantics::ParseDirectiveKind(cmd);

    switch(kind){
    case PreprocessorDirectiveKind::If:
    case PreprocessorDirectiveKind::Ifdef:
    case PreprocessorDirectiveKind::Ifndef:{
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
        if(kind == PreprocessorDirectiveKind::If){
            if(!evaluateCondition(rest, line, condResult))
                return false;
        }
        else{
            MStringView macroName;
            if(!__hidden_metascript_preprocessor::ParseSingleMacroDirectiveName(
                rest,
                line,
                m_arena,
                m_errors,
                MStringView("expected macro name after #ifdef/#ifndef"),
                MStringView("unexpected tokens after macro name in #ifdef/#ifndef"),
                false,
                macroName
            ))
                return false;
            const b8 defined = m_macros.find(macroName) != m_macros.end();
            condResult = (kind == PreprocessorDirectiveKind::Ifdef) ? defined : !defined;
        }
        frame.currentActive = condResult;
        frame.branchTaken = condResult;
        condStack.push_back(frame);
        return true;
    }

    case PreprocessorDirectiveKind::Elif:{
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

    case PreprocessorDirectiveKind::Else:{
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

    case PreprocessorDirectiveKind::Endif:{
        if(condStack.empty()){
            m_errors.push_back(ParseError{line, 1u, MString("unexpected #endif without matching #if", m_arena)});
            return false;
        }
        condStack.pop_back();
        return true;
    }

    default:
        break;
    }

    if(!parentActive)
        return true;

    if(kind == PreprocessorDirectiveKind::Define)
        return handleDefine(rest, line);
    if(kind == PreprocessorDirectiveKind::Undef)
        return handleUndef(rest, line);
    if(kind == PreprocessorDirectiveKind::Include)
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
    if(name == PreprocessorSemantics::s_DefinedOperator){
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
                if(paramName == PreprocessorSemantics::s_VariadicEllipsis && !hasComma){
                    macro.isVariadic = true;
                    break;
                }
                b8 validParam = IsAsciiIdentifier(paramName);
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
    MStringView name;
    if(!__hidden_metascript_preprocessor::ParseSingleMacroDirectiveName(
        rest,
        line,
        m_arena,
        m_errors,
        MStringView("expected macro name after #undef"),
        MStringView("unexpected tokens after macro name in #undef"),
        true,
        name
    ))
        return false;
    undefineMacro(name);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

