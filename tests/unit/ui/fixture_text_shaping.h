// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/text/shaper.h>

#include <global/algorithm.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::UiFixtureText{


inline void AppendCodePointGlyphs(Impl::Ui::ShapedRun& run, const AStringView text){
    usize begin = 0u;
    while(begin < text.size()){
        usize end = begin + 1u;
        while(end < text.size() && IsUtf8Continuation(static_cast<u8>(text[end])))
            ++end;
        run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {}, { 10.0f, 0.0f }, {} });
        begin = end;
    }
}

inline void ReverseGlyphs(Impl::Ui::ShapedRun& run){
    for(usize index = 0u; index < run.glyphs.size() / 2u; ++index)
        Swap(run.glyphs[index], run.glyphs[run.glyphs.size() - index - 1u]);
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

