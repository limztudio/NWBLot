// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "parser.h"

#include <core/alloc/scratch.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IIncludeResolver{
    virtual ~IIncludeResolver()noexcept = default;


public:
    [[nodiscard]] virtual b8 resolveInclude(MStringView includePath, b8 isAngleBracket, MetaArena& arena, MString& outResolvedKey, MString& outContent) = 0;
    virtual void popIncludeScope()noexcept{}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PreprocessorMacro{
    MString name;
    MVector<MString> parameters;
    MString body;
    b8 isFunctionLike = false;


public:
    explicit PreprocessorMacro(MetaArena& arena)noexcept
        : name(arena)
        , parameters(arena)
        , body(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Preprocessor final{
public:
    struct IncludedByteRange{
        usize startOffset = 0u;
        usize endOffset = 0u;
    };


private:
    struct ConditionalFrame{
        u32 line = 1;
        b8 parentActive = true;
        b8 branchTaken = false;
        b8 currentActive = true;
        b8 seenElse = false;
    };


public:
    using MacroMap = MStringMap<PreprocessorMacro>;
    using IncludedSet = MStringMap<b8>;
    using IncludedList = MVector<MString>;
    using IncludedRangeList = MVector<IncludedByteRange>;


public:
    [[nodiscard]] static usize SkipQuotedLiteral(MStringView text, usize index)noexcept;
    [[nodiscard]] static usize AppendQuotedLiteral(MStringView text, usize index, MString& out);
    [[nodiscard]] static usize AppendPreprocessorNumber(MStringView text, usize index, b8 allowSignedExponent, MString& out);


public:
    Preprocessor(MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, IIncludeResolver* includeResolver = nullptr, b8 allowNwbInclude = true);


public:
    void defineObjectMacro(MStringView name, MStringView body);
    void undefineMacro(MStringView name)noexcept;
    [[nodiscard]] b8 isOffsetInIncludedRange(usize offset)const noexcept;

    [[nodiscard]] b8 process(MStringView source, MString& outProcessed);

    [[nodiscard]] Span<const MString> includedFiles()const noexcept{ return {m_includedFiles.data(), m_includedFiles.size()}; }
    [[nodiscard]] Span<const MString> resolvedIncludedFiles()const noexcept{ return {m_resolvedIncludedFiles.data(), m_resolvedIncludedFiles.size()}; }


private:
    [[nodiscard]] b8 processInternal(MStringView source, u32 depth, MStringView currentFileKey, MString& outProcessed);
    [[nodiscard]] b8 handleDirective(MStringView directiveLine, u32 line, u32 depth, MStringView currentFileKey, Vector<ConditionalFrame, Alloc::ScratchArena>& condStack, MString& outProcessed);
    [[nodiscard]] b8 handleInclude(MStringView rest, u32 line, u32 depth, MStringView currentFileKey, MString& outProcessed);
    [[nodiscard]] b8 handleDefine(MStringView rest, u32 line);
    [[nodiscard]] b8 handleUndef(MStringView rest, u32 line);
    [[nodiscard]] b8 expandLineMacros(MStringView lineText, u32 line, MString& outExpanded);
    [[nodiscard]] b8 evaluateCondition(MStringView exprText, u32 line, b8& outResult);


private:
    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    MVector<ParseError>& m_errors;
    IIncludeResolver* m_includeResolver = nullptr;
    MacroMap m_macros;
    IncludedSet m_includedSet;
    IncludedList m_includedFiles;
    IncludedList m_resolvedIncludedFiles;
    IncludedRangeList m_includedByteRanges;
    b8 m_allowNwbInclude = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

