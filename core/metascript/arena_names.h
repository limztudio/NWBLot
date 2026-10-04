// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MetascriptArenaScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_ParserScratch("core/metascript/parser_scratch");
inline constexpr Name s_DocumentReaderScratch("core/metascript/document_reader_scratch");

inline constexpr TStringView s_IntegerOverflowMessage = GLOBAL_TEXT("integer overflow");
inline constexpr TStringView s_DivisionByZeroMessage = GLOBAL_TEXT("division by zero");
inline constexpr TStringView s_ListAppendSizeOverflowMessage = GLOBAL_TEXT("list append size overflow");
inline constexpr AStringView s_IntegerOverflowText = "integer overflow";
inline constexpr AStringView s_DivisionByZeroText = "division by zero";
inline constexpr AStringView s_ListAppendSizeOverflowText = "list append size overflow";
inline constexpr AStringView s_StringConcatenationSizeOverflowText = "string concatenation size overflow";
inline constexpr AStringView s_ListConcatenationSizeOverflowText = "list concatenation size overflow";
inline constexpr AStringView s_StringAppendSizeOverflowText = "string append size overflow";
inline constexpr TStringView s_StringConcatenationSizeOverflowMessage = GLOBAL_TEXT("string concatenation size overflow");
inline constexpr TStringView s_ListConcatenationSizeOverflowMessage = GLOBAL_TEXT("list concatenation size overflow");
inline constexpr TStringView s_StringAppendSizeOverflowMessage = GLOBAL_TEXT("string append size overflow");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

