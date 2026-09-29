// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ui/widgets/edit_box.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiMultilineViewTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;

class FixtureShaper final : public ITextShaper{
public:
    explicit FixtureShaper(Core::Alloc::GlobalArena& arena)
        : m_arena(arena)
    {}
    virtual ~FixtureShaper()override = default;


public:
    [[nodiscard]] virtual TextLayoutStatus::Enum shape(const ShapeRequest& request, ShapedRun& output)override{
        ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        if(request.text == "ffi")
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 30.0f, 0.0f }, {} });
        else if(request.text == "\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab")
            run.glyphs.push_back({ {}, 1u, 0u, 9u, {}, { 12.0f, 0.0f }, {} });
        else{
            usize begin = 0u;
            while(begin < request.text.size()){
                usize end = begin + 1u;
                while(end < request.text.size() && IsUtf8Continuation(static_cast<u8>(request.text[end])))
                    ++end;
                run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {}, { 10.0f, 0.0f }, {} });
                begin = end;
            }
        }
        if(request.direction == TextDirection::RightToLeft){
            for(usize index = 0u; index < run.glyphs.size() / 2u; ++index)
                Swap(run.glyphs[index], run.glyphs[run.glyphs.size() - index - 1u]);
        }
        output = Move(run);
        return TextLayoutStatus::Success;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
};

inline void ExpectRect(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}

inline void ExpectPlacement(const EditBoxPlacement& actual, const EditBoxPlacement& expected){
    ExpectRect(actual.bounds, expected.bounds);
    ExpectRect(actual.frameClip, expected.frameClip);
    ExpectRect(actual.content, expected.content);
    ExpectRect(actual.clip, expected.clip);
    ExpectRect(actual.caret, expected.caret);
    ExpectRect(actual.selection, expected.selection);
    ExpectRect(actual.preeditUnderline, expected.preeditUnderline);
    EXPECT_FLOAT_EQ(actual.textOrigin.x, expected.textOrigin.x);
    EXPECT_FLOAT_EQ(actual.textOrigin.y, expected.textOrigin.y);
    EXPECT_FLOAT_EQ(actual.scroll, expected.scroll);
    EXPECT_FLOAT_EQ(actual.scrollY, expected.scrollY);
}

inline void CollectSolidRects(const DrawSnapshot& snapshot, const Color& color, PaintVector<Rect>& output){
    output.clear();
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Solid)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            if(first.color.r != color.r || first.color.g != color.g || first.color.b != color.b || first.color.a != color.a)
                continue;
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            output.push_back({ first.position.x, first.position.y,
                opposite.position.x - first.position.x, opposite.position.y - first.position.y });
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MultilineViewFixture : public testing::Test{
public:
    MultilineViewFixture()
        : m_arena(Name("tests/ui/widgets/multiline_view"))
        , m_model(m_arena, {}, EditTextMode::Multiline)
        , m_shaper(m_arena)
        , m_layoutBuilder(m_arena, m_shaper)
        , m_view(m_arena)
        , m_font(m_arena, Name("tests/ui/widgets/multiline_view/font"))
        , m_text(m_arena)
        , m_skin(m_arena, Name("tests/ui/widgets/multiline_view/skin"))
        , m_paint(m_arena)
    {
        UiSkin::RegionVector regions(m_arena);
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/widgets/multiline_view/texture"), 4u, 4u, 1.0f, Move(regions));
    }


protected:
    [[nodiscard]] bool shapeView(){
        if(!m_view.snapshot(m_model))
            return false;
        TextLayout layout(m_arena);
        return
            m_layoutBuilder.layout({ m_view.displayText() }, layout) == TextLayoutStatus::Success
            && m_view.adoptLayout(Move(layout))
        ;
    }

    [[nodiscard]] bool place(const f32 width = 100.0f, const f32 height = 100.0f, const Point previousScroll = {}){
        return m_view.arrange({ 10.0f, 20.0f, width, height }, {}, { 0.0f, 0.0f, 500.0f, 500.0f }, previousScroll, m_placement);
    }

    [[nodiscard]] bool loadFont(){
        const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY)
            / "latin.font";
        Core::Assets::AssetBytes bytes(m_arena);
        if(!Tests::ReadBundledFontBytes(path, bytes))
            return false;
        m_font.setFontBytes(Move(bytes));
        if(!m_font.validatePayload())
            return false;
        const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/widgets/multiline_view/font"), m_font, 1u };
        return m_text.setFonts(&source, 1u);
    }

    [[nodiscard]] bool paintView(DrawSnapshot& output, const EditBoxStyle& style, const EditBoxPaintFlags& flags){
        m_paint.begin({ 500.0f, 500.0f, 1.0f, 1.0f }, 1u, 1u,
            Core::Assets::AssetRef<UiSkin>("tests/ui/widgets/multiline_view/skin"), m_skin);
        if(!m_view.paint(m_text, m_paint, m_skin, m_placement, style, flags))
            return false;
        EXPECT_FALSE(m_paint.popClip());
        output = m_paint.freeze();
        return true;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    EditModel m_model;
    FixtureShaper m_shaper;
    TextLayoutBuilder m_layoutBuilder;
    EditBoxView m_view;
    EditBoxPlacement m_placement;
    Font m_font;
    TextService m_text;
    UiSkin m_skin;
    PaintBuilder m_paint;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

