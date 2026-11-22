// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "preprocessor.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PreprocessorMacroExpander final{
public:
    struct LineScanOutcome{
        b8 replaced = false;
        b8 enteredBlockComment = false;
        b8 enteredRawString = false;
        MStringView rawStringDelimiter;
    };


public:
    PreprocessorMacroExpander(MetaArena& arena, const Preprocessor::MacroMap& macros, MVector<ParseError>& errors, const Preprocessor::MacroFilter* filter = nullptr)noexcept
        : m_arena(arena)
        , m_macros(macros)
        , m_errors(errors)
        , m_filter(filter)
    {}


public:
    [[nodiscard]] static usize AppendDefinedOperand(MStringView source, usize index, MString& out);
    [[nodiscard]] b8 expand(MStringView lineText, u32 line, MString& outExpanded);
    [[nodiscard]] b8 expandToView(MStringView lineText, u32 line, MString& outStorage, MStringView& outView, LineScanOutcome* outScanOutcome = nullptr);


private:
    MetaArena& m_arena;
    const Preprocessor::MacroMap& m_macros;
    MVector<ParseError>& m_errors;
    const Preprocessor::MacroFilter* m_filter = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

