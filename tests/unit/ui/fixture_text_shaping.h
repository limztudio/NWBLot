// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/text/shaper.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::UiFixtureText{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AppendCodePointGlyphs(const AStringView text, const Impl::Ui::Rect& ink, Impl::Ui::ShapedRun& run){
    usize begin = 0u;
    while(begin < text.size()){
        usize end = begin + 1u;
        while(end < text.size() && IsUtf8Continuation(static_cast<u8>(text[end])))
            ++end;
        run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {}, { 10.0f, 0.0f }, ink });
        begin = end;
    }
}

inline void ReverseGlyphs(Impl::Ui::ShapedRun& run)noexcept{
    for(usize index = 0u; index < run.glyphs.size() / 2u; ++index)
        Swap(run.glyphs[index], run.glyphs[run.glyphs.size() - index - 1u]);
}

class FixtureShaper final : public Impl::Ui::ITextShaper{
public:
    explicit FixtureShaper(Core::Alloc::GlobalArena& arena)noexcept : m_arena(arena){}
    virtual ~FixtureShaper()noexcept override = default;


public:
    [[nodiscard]] virtual Expected<Impl::Ui::ShapedRun, Impl::Ui::TextLayoutStatus::Enum> shape(const Impl::Ui::ShapeRequest& request)override{
        if(request.text == "FAIL")
            return MakeUnexpected(Impl::Ui::TextLayoutStatus::MissingGlyph);
        Impl::Ui::ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        if(request.text == "ffi")
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 30.0f, 0.0f }, {} });
        else if(request.text == "A\xcc\x81"){
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 10.0f, 0.0f }, { -1.0f, -8.0f, 12.0f, 10.0f } });
            run.glyphs.push_back({ {}, 2u, 0u, 3u, { -4.0f, -2.0f }, {}, { 0.0f, -8.0f, 3.0f, 2.0f } });
        }
        else if(request.text == "A\xcc\x81" "B"){
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 10.0f, 0.0f }, {} });
            run.glyphs.push_back({ {}, 2u, 3u, 4u, {}, { 10.0f, 0.0f }, {} });
        }
        else if(request.text == "\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab")
            run.glyphs.push_back({ {}, 1u, 0u, 9u, {}, { 12.0f, 0.0f }, {} });
        else
            AppendCodePointGlyphs(request.text, {}, run);
        if(request.direction == Impl::Ui::TextDirection::RightToLeft)
            ReverseGlyphs(run);
        return run;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
