// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "parser.h"
#include "preprocessor_includes.h"

#include <core/alloc/scratch.h>

#include <global/source_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PreprocessorMacro{
    MString name;
    MVector<MString> parameters;
    MString body;
    b8 isFunctionLike = false;
    b8 isVariadic = false;


public:
    explicit PreprocessorMacro(MetaArena& arena)noexcept
        : name(arena)
        , parameters(arena)
        , body(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Preprocessor final{
    friend class PreprocessorIncludes;

public:
    using IncludedByteRange = PreprocessorIncludes::IncludedByteRange;


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
    using IncludedSet = PreprocessorIncludes::IncludedSet;
    using IncludedList = PreprocessorIncludes::IncludedList;
    using IncludedRangeList = PreprocessorIncludes::IncludedRangeList;


public:
    [[nodiscard]] static usize AppendPreprocessorNumber(MStringView text, usize index, b8 allowSignedExponent, MString& out);
    [[nodiscard]] static b8 TryParseRawStringOpening(MStringView text, usize quoteIndex, MStringView& outDelimiter)noexcept;
    [[nodiscard]] static b8 FindRawStringClosing(MStringView text, usize searchStart, MStringView delimiter, usize& outAfterCloseQuote)noexcept;


public:
    Preprocessor(MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, IIncludeResolver* includeResolver = nullptr, b8 allowNwbInclude = true);


public:
    void defineObjectMacro(MStringView name, MStringView body);
    void undefineMacro(MStringView name)noexcept;
    [[nodiscard]] b8 isOffsetInIncludedRange(usize offset)const noexcept;

    [[nodiscard]] b8 process(MStringView source, MString& outProcessed);

    [[nodiscard]] Span<const MString> includedFiles()const noexcept{ return m_includes.includedFiles(); }
    [[nodiscard]] Span<const MString> resolvedIncludedFiles()const noexcept{ return m_includes.resolvedIncludedFiles(); }


private:
    [[nodiscard]] b8 processInternal(MStringView source, u32 depth, MStringView currentFileKey, MString& outProcessed);
    [[nodiscard]] b8 handleDirective(MStringView directiveLine, u32 line, u32 depth, MStringView currentFileKey, Vector<ConditionalFrame, Alloc::ScratchArena>& condStack, MString& outProcessed);
    [[nodiscard]] b8 handleDefine(MStringView rest, u32 line);
    [[nodiscard]] b8 handleUndef(MStringView rest, u32 line);
    [[nodiscard]] b8 evaluateCondition(MStringView exprText, u32 line, b8& outResult);


private:
    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    MVector<ParseError>& m_errors;
    MacroMap m_macros;
    PreprocessorIncludes m_includes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

