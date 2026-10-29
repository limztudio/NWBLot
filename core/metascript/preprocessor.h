// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "parser.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IIncludeResolver{
    virtual ~IIncludeResolver()noexcept = default;

    [[nodiscard]] virtual b8 resolveInclude(MStringView includePath, b8 isAngleBracket, MetaArena& arena, MString& outResolvedKey, MString& outContent) = 0;
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


class Preprocessor{
public:
    struct IncludedByteRange{
        usize startOffset = 0u;
        usize endOffset = 0u;
    };

    using MacroMap = MStringMap<PreprocessorMacro>;
    using IncludedSet = MStringMap<b8>;
    using IncludedList = MVector<MString>;
    using IncludedRangeList = MVector<IncludedByteRange>;


public:
    Preprocessor(MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, IIncludeResolver* includeResolver = nullptr);


public:
    void defineObjectMacro(MStringView name, MStringView body);
    void undefineMacro(MStringView name);
    [[nodiscard]] b8 isMacroDefined(MStringView name)const noexcept;

    [[nodiscard]] b8 process(MStringView source, MString& outProcessed);

    [[nodiscard]] const IncludedList& includedFiles()const noexcept{ return m_includedFiles; }
    [[nodiscard]] b8 isOffsetInIncludedRange(usize offset)const noexcept;


private:
    struct ConditionalFrame{
        u32 line = 1;
        b8 parentActive = true;
        b8 branchTaken = false;
        b8 currentActive = true;
        b8 seenElse = false;
    };


private:
    [[nodiscard]] b8 processInternal(MStringView source, u32 depth, MString& outProcessed);
    [[nodiscard]] b8 handleDirective(MStringView directiveLine, u32 line, u32 depth, Vector<ConditionalFrame, Alloc::ScratchArena>& condStack, MString& outProcessed);
    [[nodiscard]] b8 handleInclude(MStringView rest, u32 line, u32 depth, MString& outProcessed);
    [[nodiscard]] b8 handleDefine(MStringView rest, u32 line);
    [[nodiscard]] b8 handleUndef(MStringView rest, u32 line);
    [[nodiscard]] b8 evaluateCondition(MStringView exprText, u32 line, b8& outResult);
    [[nodiscard]] b8 expandLineMacros(MStringView lineText, u32 line, MString& outExpanded);


private:
    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    MVector<ParseError>& m_errors;
    IIncludeResolver* m_includeResolver = nullptr;
    MacroMap m_macros;
    IncludedSet m_includedSet;
    IncludedList m_includedFiles;
    IncludedRangeList m_includedByteRanges;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

