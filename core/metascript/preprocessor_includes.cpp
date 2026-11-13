// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor_includes.h"
#include "preprocessor.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PreprocessorIncludes::PreprocessorIncludes(MetaArena& arena, MVector<ParseError>& errors, IIncludeResolver* includeResolver, const b8 allowNwbInclude)
    : m_arena(arena)
    , m_errors(errors)
    , m_includeResolver(includeResolver)
    , m_includedSet(0, TransparentStringHash<MString>(), TransparentStringEqual<MString>(), arena)
    , m_includedFiles(arena)
    , m_resolvedIncludedFiles(arena)
    , m_includedByteRanges(arena)
    , m_allowNwbInclude(allowNwbInclude)
{}


b8 PreprocessorIncludes::containsOffset(const usize offset)const noexcept{
    for(const IncludedByteRange& range : m_includedByteRanges){
        if(offset >= range.startOffset && offset < range.endOffset)
            return true;
    }
    return false;
}

MStringView PreprocessorIncludes::includedFileAtOffset(const usize offset)const noexcept{
    for(const IncludedByteRange& range : m_includedByteRanges){
        if(offset >= range.startOffset && offset < range.endOffset && range.resolvedFileIndex < m_resolvedIncludedFiles.size())
            return MStringView(m_resolvedIncludedFiles[range.resolvedFileIndex].data(), m_resolvedIncludedFiles[range.resolvedFileIndex].size());
    }
    return {};
}

MStringView PreprocessorIncludes::resolveNamespaceAtOffset(const usize offset)const noexcept{
    const MStringView fileKey = includedFileAtOffset(offset);
    if(fileKey.empty() || !m_includeResolver)
        return {};
    return m_includeResolver->resolveIncludeNamespace(fileKey);
}


b8 PreprocessorIncludes::process(Preprocessor& owner, const MStringView rest, const u32 line, const u32 depth, const MStringView currentFileKey, MString& outProcessed){
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
    if(!TrimView(rest.substr(closePos + 1u)).empty()){
        m_errors.push_back(ParseError{line, 1u, MString("unexpected tokens after #include path", m_arena)});
        return false;
    }

    const MStringView includePath = rest.substr(1u, closePos - 1u);
    const b8 hasAdhdExt = includePath.size() > 5u && includePath.substr(includePath.size() - 5u) == ".adhd";
    const b8 hasNwbExt = m_allowNwbInclude && includePath.size() > 4u && includePath.substr(includePath.size() - 4u) == ".nwb";
    if(!hasAdhdExt && !hasNwbExt){
        m_errors.push_back(ParseError{line, 1u, MString("outside 'native { ... }', '#include' may only include '.adhd' or '.nwb' files; place C++ '#include' directives inside a 'native' block", m_arena)});
        return false;
    }
    MString scopedIncludeKey(m_arena);
    scopedIncludeKey.append(currentFileKey.data(), currentFileKey.size());
    scopedIncludeKey.push_back('|');
    scopedIncludeKey.append(includePath.data(), includePath.size());
    if(m_includedSet.find(MStringView(scopedIncludeKey.data(), scopedIncludeKey.size())) != m_includedSet.end())
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

    m_includedSet.emplace(Move(scopedIncludeKey), true);
    if(depth == 0u){
        b8 alreadyListed = false;
        for(const MString& existing : m_includedFiles){
            if(MStringView(existing.data(), existing.size()) == includePath){
                alreadyListed = true;
                break;
            }
        }
        if(!alreadyListed)
            m_includedFiles.push_back(MString(includePath.data(), includePath.size(), m_arena));
    }
    if(m_includedSet.find(resolvedKey) != m_includedSet.end()){
        m_includeResolver->popIncludeScope();
        return true;
    }

    m_includedSet.emplace(MString(resolvedKey.data(), resolvedKey.size(), m_arena), true);
    const usize fileIndex = m_resolvedIncludedFiles.size();
    m_resolvedIncludedFiles.push_back(MString(resolvedKey.data(), resolvedKey.size(), m_arena));

    const usize startOffset = outProcessed.size();
    outProcessed.append(s_IncludePushMarker.data(), s_IncludePushMarker.size());
    const b8 ok = owner.processInternal(content, depth + 1u, MStringView(resolvedKey.data(), resolvedKey.size()), outProcessed);
    outProcessed.append(s_IncludePopMarker.data(), s_IncludePopMarker.size());
    m_includeResolver->popIncludeScope();
    if(!ok)
        return false;
    m_includedByteRanges.push_back(IncludedByteRange{startOffset, outProcessed.size(), fileIndex});
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

