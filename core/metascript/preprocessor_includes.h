// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "parser.h"

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IIncludeResolver{
    virtual ~IIncludeResolver()noexcept = default;


public:
    [[nodiscard]] virtual b8 resolveInclude(MStringView includePath, b8 isAngleBracket, MetaArena& arena, MString& outResolvedKey, MString& outContent) = 0;
    virtual void popIncludeScope()noexcept{}
    [[nodiscard]] virtual MStringView resolveIncludeNamespace(MStringView)const noexcept{ return {}; }
};


class Preprocessor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PreprocessorIncludes final{
public:
    struct IncludedByteRange{
        usize startOffset = 0u;
        usize endOffset = 0u;
        usize resolvedFileIndex = 0u;
    };
    using IncludedSet = MStringMap<b8>;
    using IncludedList = MVector<MString>;
    using IncludedRangeList = MVector<IncludedByteRange>;


public:
    PreprocessorIncludes(MetaArena& arena, MVector<ParseError>& errors, IIncludeResolver* includeResolver, b8 allowNwbInclude);


public:
    void clearRanges()noexcept{ m_includedByteRanges.clear(); }
    [[nodiscard]] b8 containsOffset(usize offset)const noexcept;
    [[nodiscard]] MStringView includedFileAtOffset(usize offset)const noexcept;
    [[nodiscard]] MStringView resolveNamespaceAtOffset(usize offset)const noexcept;
    [[nodiscard]] Span<const MString> includedFiles()const noexcept{ return {m_includedFiles.data(), m_includedFiles.size()}; }
    [[nodiscard]] Span<const MString> resolvedIncludedFiles()const noexcept{ return {m_resolvedIncludedFiles.data(), m_resolvedIncludedFiles.size()}; }
    [[nodiscard]] b8 process(Preprocessor& owner, MStringView rest, u32 line, u32 depth, MStringView currentFileKey, MString& outProcessed);


private:
    MetaArena& m_arena;
    MVector<ParseError>& m_errors;
    IIncludeResolver* m_includeResolver = nullptr;
    IncludedSet m_includedSet;
    IncludedList m_includedFiles;
    IncludedList m_resolvedIncludedFiles;
    IncludedRangeList m_includedByteRanges;
    b8 m_allowNwbInclude = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

