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

    [[nodiscard]] virtual bool resolveInclude(MStringView includePath, bool isAngleBracket, MetaArena& arena, MString& outResolvedKey, MString& outContent) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PreprocessorMacro{
    MString name;
    MVector<MString> parameters;
    MString body;
    bool isFunctionLike = false;


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
    using IncludedSet = MStringMap<bool>;
    using IncludedList = MVector<MString>;
    using IncludedRangeList = MVector<IncludedByteRange>;


public:
    Preprocessor(MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, IIncludeResolver* includeResolver = nullptr);


public:
    void defineObjectMacro(MStringView name, MStringView body);
    void undefineMacro(MStringView name);
    [[nodiscard]] bool isMacroDefined(MStringView name)const noexcept;

    [[nodiscard]] bool process(MStringView source, MString& outProcessed);

    [[nodiscard]] const IncludedList& includedFiles()const noexcept{ return m_includedFiles; }
    [[nodiscard]] bool isOffsetInIncludedRange(usize offset)const noexcept;


private:
    struct ConditionalFrame{
        u32 line = 1;
        bool parentActive = true;
        bool branchTaken = false;
        bool currentActive = true;
        bool seenElse = false;
    };


private:
    [[nodiscard]] bool processInternal(MStringView source, u32 depth, MString& outProcessed);
    [[nodiscard]] bool handleDirective(MStringView directiveLine, u32 line, u32 depth, Vector<ConditionalFrame, Alloc::ScratchArena>& condStack, MString& outProcessed);
    [[nodiscard]] bool handleInclude(MStringView rest, u32 line, u32 depth, MString& outProcessed);
    [[nodiscard]] bool handleDefine(MStringView rest, u32 line);
    [[nodiscard]] bool handleUndef(MStringView rest, u32 line);
    [[nodiscard]] bool evaluateCondition(MStringView exprText, u32 line, bool& outResult);
    [[nodiscard]] bool expandLineMacros(MStringView lineText, u32 line, MString& outExpanded);


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

