// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_preprocessor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MaxIncludeDepth = 64u;

[[nodiscard]] constexpr MStringView StripLineComment(MStringView line)noexcept{
    b8 inString = false;
    b8 inChar = false;
    for(usize i = 0u; i < line.size(); ++i){
        if(!inChar && line[i] == '"' && (i == 0u || line[i - 1u] != '\\'))
            inString = !inString;
        else if(!inString && line[i] == '\'' && (i == 0u || line[i - 1u] != '\\'))
            inChar = !inChar;
        if(!inString && !inChar && i + 1u < line.size() && line[i] == '/' && line[i + 1u] == '/')
            return line.substr(0u, i);
        if(!inString && !inChar && i + 1u < line.size() && line[i] == '/' && line[i + 1u] == '*'){
            const usize endComment = line.find("*/", i + 2u);
            if(endComment == MStringView::npos || TrimView(line.substr(endComment + 2u)).empty())
                return line.substr(0u, i);
        }
    }
    return line;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Preprocessor::Preprocessor(MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, IIncludeResolver* includeResolver)
    : m_arena(arena)
    , m_scratchArena(scratchArena)
    , m_errors(errors)
    , m_includeResolver(includeResolver)
    , m_macros(0, MStringHash(), MStringEqual(), arena)
    , m_includedSet(0, MStringHash(), MStringEqual(), arena)
    , m_includedFiles(arena)
    , m_includedByteRanges(arena)
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

void Preprocessor::undefineMacro(const MStringView name){
    const auto it = m_macros.find(name);
    if(it != m_macros.end())
        m_macros.erase(it);
}

b8 Preprocessor::isMacroDefined(const MStringView name)const noexcept{
    return m_macros.find(name) != m_macros.end();
}

b8 Preprocessor::isOffsetInIncludedRange(const usize offset)const noexcept{
    for(const IncludedByteRange& range : m_includedByteRanges){
        if(offset >= range.startOffset && offset < range.endOffset)
            return true;
    }
    return false;
}

b8 Preprocessor::process(const MStringView source, MString& outProcessed){
    outProcessed.clear();
    outProcessed.reserve(source.size());
    m_includedByteRanges.clear();
    return processInternal(source, 0u, outProcessed);
}

b8 Preprocessor::processInternal(const MStringView source, const u32 depth, MString& outProcessed){
    using namespace __hidden_metascript_preprocessor;

    if(depth > s_MaxIncludeDepth){
        m_errors.push_back(ParseError{1u, 1u, MString("maximum #include nesting depth exceeded", m_arena)});
        return false;
    }

    Vector<ConditionalFrame, Alloc::ScratchArena> condStack{m_scratchArena};
    condStack.reserve(8u);

    b8 inBlockComment = false;
    b8 inNativeKeywordPendingBrace = false;
    u32 nativeBraceDepth = 0u;
    u32 currentLine = 1u;
    usize pos = 0u;

    while(pos < source.size()){
        usize lineEnd = pos;
        while(lineEnd < source.size() && source[lineEnd] != '\n')
            ++lineEnd;

        MStringView rawLine = source.substr(pos, lineEnd - pos);
        if(!rawLine.empty() && rawLine.back() == '\r')
            rawLine.remove_suffix(1u);

        const b8 isActive = condStack.empty() || condStack.back().currentActive;

        MStringView effectiveLine = rawLine;
        MStringView commentPrefix;
        if(inBlockComment){
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

        const MStringView trimmed = TrimView(effectiveLine);

        if(commentPrefix.empty() && !trimmed.empty() && trimmed[0] == '#'){
            const MStringView afterHash = TrimView(trimmed.substr(1u));
            const b8 isNativeInclude = (nativeBraceDepth > 0u)
                && afterHash.starts_with("include")
                && (afterHash.size() == 7u || !IsAsciiIdentifierChar(afterHash[7u]));
            if(isNativeInclude){
                if(isActive)
                    outProcessed.append(rawLine.data(), rawLine.size());
                outProcessed.push_back('\n');
            }
            else{
                if(!handleDirective(afterHash, currentLine, depth, condStack, outProcessed))
                    return false;
                outProcessed.push_back('\n');
            }
        }
        else if(isActive){
            MString expandedLine(m_arena);
            if(!commentPrefix.empty())
                expandedLine.append(commentPrefix.data(), commentPrefix.size());
            MString expandedTail(m_arena);
            if(!expandLineMacros(effectiveLine, currentLine, expandedTail))
                return false;
            expandedLine.append(expandedTail.data(), expandedTail.size());

            for(usize scanIdx = commentPrefix.size(); scanIdx < expandedLine.size();){
                const MChar ch = expandedLine[scanIdx];
                if(ch == '/' && scanIdx + 1u < expandedLine.size() && expandedLine[scanIdx + 1u] == '/')
                    break;
                if(ch == '/' && scanIdx + 1u < expandedLine.size() && expandedLine[scanIdx + 1u] == '*'){
                    const usize closeComment = MStringView(expandedLine.data(), expandedLine.size()).find("*/", scanIdx + 2u);
                    if(closeComment == MStringView::npos){
                        inBlockComment = true;
                        break;
                    }
                    scanIdx = closeComment + 2u;
                    continue;
                }
                if(ch == '"' || ch == '\''){
                    const MChar quote = ch;
                    ++scanIdx;
                    while(scanIdx < expandedLine.size() && expandedLine[scanIdx] != quote){
                        if(expandedLine[scanIdx] == '\\' && scanIdx + 1u < expandedLine.size())
                            ++scanIdx;
                        ++scanIdx;
                    }
                    if(scanIdx < expandedLine.size())
                        ++scanIdx;
                    continue;
                }
                if(IsAsciiIdentifierStart(ch)){
                    const usize idStart = scanIdx;
                    while(scanIdx < expandedLine.size() && IsAsciiIdentifierChar(expandedLine[scanIdx]))
                        ++scanIdx;
                    const MStringView idText(expandedLine.data() + idStart, scanIdx - idStart);
                    if(idText == "native" && nativeBraceDepth == 0u)
                        inNativeKeywordPendingBrace = true;
                    continue;
                }
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
                ++scanIdx;
            }

            outProcessed.append(expandedLine.data(), expandedLine.size());
            outProcessed.push_back('\n');
        }
        else{
            for(usize scanIdx = 0u; scanIdx + 1u < effectiveLine.size(); ++scanIdx){
                if(effectiveLine[scanIdx] == '/' && effectiveLine[scanIdx + 1u] == '/')
                    break;
                if(effectiveLine[scanIdx] == '/' && effectiveLine[scanIdx + 1u] == '*'){
                    const usize closeComment = effectiveLine.find("*/", scanIdx + 2u);
                    if(closeComment == MStringView::npos){
                        inBlockComment = true;
                        break;
                    }
                    scanIdx = closeComment + 1u;
                }
            }
            outProcessed.push_back('\n');
        }

        pos = (lineEnd < source.size()) ? (lineEnd + 1u) : lineEnd;
        ++currentLine;
    }

    if(!condStack.empty()){
        m_errors.push_back(ParseError{condStack.back().line, 1u, MString("unterminated preprocessor conditional (#if/#ifdef/#ifndef without #endif)", m_arena)});
        return false;
    }

    return true;
}

b8 Preprocessor::handleDirective(const MStringView directiveLine, const u32 line, const u32 depth, Vector<ConditionalFrame, Alloc::ScratchArena>& condStack, MString& outProcessed){
    using namespace __hidden_metascript_preprocessor;

    const MStringView cleanLine = TrimView(StripLineComment(directiveLine));
    usize cmdEnd = 0u;
    while(cmdEnd < cleanLine.size() && IsAsciiIdentifierChar(cleanLine[cmdEnd]))
        ++cmdEnd;

    const MStringView cmd = cleanLine.substr(0u, cmdEnd);
    const MStringView rest = TrimView(cleanLine.substr(cmdEnd));
    const b8 parentActive = condStack.empty() || condStack.back().currentActive;

    if(cmd == "ifdef" || cmd == "ifndef"){
        ConditionalFrame frame;
        frame.line = line;
        frame.parentActive = parentActive;
        if(!parentActive){
            frame.branchTaken = true;
            frame.currentActive = false;
            condStack.push_back(frame);
            return true;
        }
        if(rest.empty()){
            m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #ifdef/#ifndef", m_arena)});
            return false;
        }
        usize nameEnd = 0u;
        while(nameEnd < rest.size() && IsAsciiIdentifierChar(rest[nameEnd]))
            ++nameEnd;
        const MStringView macroName = rest.substr(0u, nameEnd);
        const b8 defined = isMacroDefined(macroName);
        frame.currentActive = (cmd == "ifdef") ? defined : !defined;
        frame.branchTaken = frame.currentActive;
        condStack.push_back(frame);
        return true;
    }

    if(cmd == "if"){
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
        if(!evaluateCondition(rest, line, condResult))
            return false;
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
        return handleInclude(rest, line, depth, outProcessed);

    m_errors.push_back(ParseError{line, 1u, MString("unknown preprocessor directive", m_arena)});
    return false;
}

b8 Preprocessor::handleInclude(const MStringView rest, const u32 line, const u32 depth, MString& outProcessed){
    if(rest.size() < 3u){
        m_errors.push_back(ParseError{line, 1u, MString("invalid #include syntax", m_arena)});
        return false;
    }

    const MChar openChar = rest[0];
    const MChar closeChar = (openChar == '"') ? '"' : ((openChar == '<') ? '>' : '\0');
    if(closeChar == '\0'){
        m_errors.push_back(ParseError{line, 1u, MString("expected '\"' or '<' after #include", m_arena)});
        return false;
    }

    const usize closePos = rest.find(closeChar, 1u);
    if(closePos == MStringView::npos || closePos <= 1u){
        m_errors.push_back(ParseError{line, 1u, MString("unterminated #include path", m_arena)});
        return false;
    }

    const MStringView includePath = rest.substr(1u, closePos - 1u);
    const b8 hasAdhdExt = includePath.size() > 5u && includePath.substr(includePath.size() - 5u) == ".adhd";
    const b8 hasNwbExt = includePath.size() > 4u && includePath.substr(includePath.size() - 4u) == ".nwb";
    if(!hasAdhdExt && !hasNwbExt){
        m_errors.push_back(ParseError{line, 1u, MString("outside 'native { ... }', '#include' may only include '.adhd' or '.nwb' files; place C++ '#include' directives inside a 'native' block", m_arena)});
        return false;
    }
    if(m_includedSet.find(includePath) != m_includedSet.end())
        return true;
    if(!m_includeResolver){
        m_errors.push_back(ParseError{line, 1u, MString("#include is not available without an include resolver", m_arena)});
        return false;
    }

    MString resolvedKey(m_arena);
    MString content(m_arena);
    if(!m_includeResolver->resolveInclude(includePath, openChar == '<', m_arena, resolvedKey, content)){
        m_errors.push_back(ParseError{line, 1u, MString("failed to resolve #include file", m_arena)});
        return false;
    }

    if(m_includedSet.find(resolvedKey) != m_includedSet.end())
        return true;

    m_includedSet.emplace(MString(includePath.data(), includePath.size(), m_arena), true);
    m_includedSet.emplace(MString(resolvedKey.data(), resolvedKey.size(), m_arena), true);
    m_includedFiles.push_back(MString(includePath.data(), includePath.size(), m_arena));

    const usize startOffset = outProcessed.size();
    if(!processInternal(content, depth + 1u, outProcessed))
        return false;
    if(depth == 0u)
        m_includedByteRanges.push_back(IncludedByteRange{startOffset, outProcessed.size()});
    return true;
}

b8 Preprocessor::handleDefine(const MStringView rest, const u32 line){
    using namespace __hidden_metascript_preprocessor;

    if(rest.empty() || !IsAsciiIdentifierStart(rest[0])){
        m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #define", m_arena)});
        return false;
    }

    usize nameEnd = 0u;
    while(nameEnd < rest.size() && IsAsciiIdentifierChar(rest[nameEnd]))
        ++nameEnd;

    const MStringView name = rest.substr(0u, nameEnd);
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
            while(pStart < paramList.size()){
                usize comma = paramList.find(',', pStart);
                if(comma == MStringView::npos)
                    comma = paramList.size();
                const MStringView paramName = TrimView(paramList.substr(pStart, comma - pStart));
                if(paramName.empty() || !IsAsciiIdentifierStart(paramName[0])){
                    m_errors.push_back(ParseError{line, 1u, MString("invalid parameter name in function-like #define", m_arena)});
                    return false;
                }
                macro.parameters.push_back(MString(paramName.data(), paramName.size(), m_arena));
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
    usize nameEnd = 0u;
    while(nameEnd < rest.size() && IsAsciiIdentifierChar(rest[nameEnd]))
        ++nameEnd;
    undefineMacro(rest.substr(0u, nameEnd));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
