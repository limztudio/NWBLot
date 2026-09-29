// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/widgets/edit_box.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_box_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

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
        else if(request.text == "A\xcc\x81" "B"){
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 10.0f, 0.0f }, {} });
            run.glyphs.push_back({ {}, 2u, 3u, 4u, {}, { 10.0f, 0.0f }, {} });
        }
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
            if(request.direction == TextDirection::RightToLeft){
                for(usize index = 0u; index < run.glyphs.size() / 2u; ++index)
                    Swap(run.glyphs[index], run.glyphs[run.glyphs.size() - index - 1u]);
            }
        }
        output = Move(run);
        return TextLayoutStatus::Success;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
};

class EditBoxLayoutTests : public testing::Test{
public:
    EditBoxLayoutTests()
        : m_arena(Name("tests/ui/widgets/edit_box/layout"))
        , m_model(m_arena)
        , m_shaper(m_arena)
        , m_builder(m_arena, m_shaper)
        , m_view(m_arena)
    {}


protected:
    [[nodiscard]] bool shapeView(){
        if(!m_view.snapshot(m_model))
            return false;
        TextLayout layout(m_arena);
        return m_builder.layout({ m_view.displayText() }, layout) == TextLayoutStatus::Success && m_view.adoptLayout(Move(layout));
    }

    [[nodiscard]] bool place(f32 width = 100.0f, f32 scroll = 0.0f){
        return m_view.arrange({ 10.0f, 20.0f, width, 20.0f }, {}, { 0.0f, 0.0f, 500.0f, 500.0f }, scroll, m_placement);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    EditModel m_model;
    FixtureShaper m_shaper;
    TextLayoutBuilder m_builder;
    EditBoxView m_view;
    EditBoxPlacement m_placement;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(EditBoxLayoutTests, LigaturesExposeEachGraphemeCaretAndPartialSelection){
    ASSERT_TRUE(m_model.setText("ffi"));
    ASSERT_TRUE(m_model.setSelection(1u, 2u));
    ASSERT_TRUE(shapeView());
    ASSERT_EQ(m_view.layout().clusters().size(), 1u);
    ASSERT_EQ(m_view.caretStops().size(), 4u);
    for(usize index = 0u; index < 4u; ++index){
        EXPECT_EQ(m_view.caretStops()[index].committedByte, index);
        EXPECT_FLOAT_EQ(m_view.caretStops()[index].x, static_cast<f32>(index) * 10.0f);
    }
    ASSERT_TRUE(place());
    EXPECT_FLOAT_EQ(m_placement.caret.x, 30.0f);
    EXPECT_FLOAT_EQ(m_placement.selection.x, 20.0f);
    EXPECT_FLOAT_EQ(m_placement.selection.width, 10.0f);
    usize hit = 999u;
    ASSERT_TRUE(m_view.hitTest({ 21.0f, 24.0f }, m_placement, hit));
    EXPECT_EQ(hit, 1u);
    ASSERT_TRUE(m_view.hitTest({ 27.0f, 24.0f }, m_placement, hit));
    EXPECT_EQ(hit, 2u);
}

TEST_F(EditBoxLayoutTests, CombiningAndDecomposedHangulHitsStayOnWholeGraphemes){
    ASSERT_TRUE(m_model.setText("A\xcc\x81" "B"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    ASSERT_EQ(m_view.caretStops().size(), 3u);
    EXPECT_EQ(m_view.caretStops()[1].committedByte, 3u);
    usize hit = 999u;
    ASSERT_TRUE(m_view.hitTest({ 14.9f, 24.0f }, m_placement, hit));
    EXPECT_EQ(hit, 0u);
    ASSERT_TRUE(m_view.hitTest({ 15.1f, 24.0f }, m_placement, hit));
    EXPECT_EQ(hit, 3u);
    ASSERT_TRUE(m_model.setText("\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    ASSERT_EQ(m_view.caretStops().size(), 2u);
    EXPECT_EQ(m_view.caretStops()[1].committedByte, 9u);
    ASSERT_TRUE(m_view.hitTest({ 17.0f, 24.0f }, m_placement, hit));
    EXPECT_EQ(hit, 9u);
}

TEST_F(EditBoxLayoutTests, PreeditReplacesOnlyTheDisplayAndOwnsItsSelectionAndUnderline){
    ASSERT_TRUE(m_model.setText("ABC"));
    ASSERT_TRUE(m_model.setSelection(1u, 2u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("\xed\x95\x9c\xea\xb8\x80", 0u, 3u));
    ASSERT_TRUE(shapeView());
    EXPECT_EQ(m_model.text(), "ABC");
    EXPECT_EQ(m_view.displayText(), "A\xed\x95\x9c\xea\xb8\x80" "C");
    EXPECT_EQ(m_view.preeditRange().begin, 1u);
    EXPECT_EQ(m_view.preeditRange().end, 7u);
    EXPECT_EQ(m_view.displayCaret(), 4u);
    EXPECT_EQ(m_view.selectionRange().begin, 1u);
    EXPECT_EQ(m_view.selectionRange().end, 4u);
    ASSERT_EQ(m_view.caretStops().size(), 4u);
    EXPECT_EQ(m_view.caretStops()[2].committedByte, 2u);
    EXPECT_EQ(m_view.caretStops()[2].displayByte, 7u);
    ASSERT_TRUE(place());
    EXPECT_FLOAT_EQ(m_placement.selection.x, 20.0f);
    EXPECT_FLOAT_EQ(m_placement.selection.width, 10.0f);
    EXPECT_FLOAT_EQ(m_placement.preeditUnderline.x, 20.0f);
    EXPECT_FLOAT_EQ(m_placement.preeditUnderline.width, 20.0f);
    EXPECT_FLOAT_EQ(m_placement.preeditUnderline.height, 1.0f);
    usize hit = 999u;
    ASSERT_TRUE(m_view.hitTest({ 39.0f, 24.0f }, m_placement, hit));
    EXPECT_EQ(hit, 2u);
    m_model.cancelComposition();
    ASSERT_TRUE(m_model.setText("changed"));
    EXPECT_EQ(m_view.displayText(), "A\xed\x95\x9c\xea\xb8\x80" "C");
    EXPECT_TRUE(m_view.composing());
}

TEST_F(EditBoxLayoutTests, NativePreeditCaretCanAddressScalarEdgesInsideOneGrapheme){
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab", 3u, 6u));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    EXPECT_FLOAT_EQ(m_placement.caret.x, 18.0f);
    EXPECT_FLOAT_EQ(m_placement.selection.x, 14.0f);
    EXPECT_FLOAT_EQ(m_placement.selection.width, 4.0f);
    EXPECT_FLOAT_EQ(m_placement.preeditUnderline.width, 12.0f);
    ASSERT_EQ(m_view.caretStops().size(), 2u);
    EXPECT_EQ(m_view.caretStops()[0].committedByte, 0u);
    EXPECT_EQ(m_view.caretStops()[1].committedByte, 0u);
}

TEST_F(EditBoxLayoutTests, HorizontalScrollFollowsCaretAndClampsWhenTextShrinks){
    ASSERT_TRUE(m_model.setText("abcdef"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(m_view.arrange({ 10.0f, 20.0f, 26.0f, 20.0f }, { 3.0f, 0.0f, 3.0f, 0.0f },
        { 0.0f, 0.0f, 500.0f, 500.0f }, 0.0f, m_placement));
    EXPECT_FLOAT_EQ(m_placement.content.x, 13.0f);
    EXPECT_FLOAT_EQ(m_placement.content.width, 20.0f);
    EXPECT_FLOAT_EQ(m_placement.scroll, 41.0f);
    EXPECT_FLOAT_EQ(m_placement.caret.x + m_placement.caret.width, 33.0f);
    ASSERT_TRUE(m_model.move(EditMove::Home));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(20.0f, 41.0f));
    EXPECT_FLOAT_EQ(m_placement.scroll, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.caret.x, 10.0f);
    ASSERT_TRUE(m_model.setText("A"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(20.0f, 41.0f));
    EXPECT_FLOAT_EQ(m_placement.scroll, 0.0f);
}

TEST_F(EditBoxLayoutTests, PlacementIntersectsFrameAndContentClipsAndRejectsInvalidInputsAtomically){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(m_view.arrange({ 10.0f, 20.0f, 100.0f, 30.0f }, { 8.0f, 4.0f, 8.0f, 4.0f },
        { 30.0f, 22.0f, 20.0f, 40.0f }, 0.0f, m_placement));
    EXPECT_FLOAT_EQ(m_placement.frameClip.x, 30.0f);
    EXPECT_FLOAT_EQ(m_placement.frameClip.y, 22.0f);
    EXPECT_FLOAT_EQ(m_placement.frameClip.height, 28.0f);
    EXPECT_FLOAT_EQ(m_placement.clip.y, 24.0f);
    EXPECT_FLOAT_EQ(m_placement.clip.height, 22.0f);
    const f32 oldCaret = m_placement.caret.x;
    EXPECT_FALSE(m_view.arrange({ 0.0f, 0.0f, -1.0f, 20.0f }, {}, {}, 0.0f, m_placement));
    EXPECT_FALSE(m_view.arrange({ 0.0f, 0.0f, 20.0f, 20.0f }, { -1.0f }, {}, 0.0f, m_placement));
    EXPECT_FALSE(m_view.arrange({ 0.0f, 0.0f, 20.0f, 20.0f }, {}, {}, Limit<f32>::s_QuietNaN, m_placement));
    EXPECT_FLOAT_EQ(m_placement.caret.x, oldCaret);
    usize hit = 123u;
    EXPECT_FALSE(m_view.hitTest({ Limit<f32>::s_QuietNaN, 0.0f }, m_placement, hit));
    EXPECT_EQ(hit, 123u);
}

TEST_F(EditBoxLayoutTests, MismatchedAndRtlLayoutsPreserveTheAdmittedGeometry){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    const f32 oldCaret = m_placement.caret.x;
    TextLayout wrong(m_arena);
    ASSERT_EQ(m_builder.layout({ "other" }, wrong), TextLayoutStatus::Success);
    EXPECT_FALSE(m_view.adoptLayout(Move(wrong)));
    EXPECT_EQ(m_view.layout().utf8(), "abc");
    TextLayout rtl(m_arena);
    ShapeRequest request{ "abc" };
    request.direction = TextDirection::RightToLeft;
    ASSERT_EQ(m_builder.layout(request, rtl), TextLayoutStatus::Success);
    EXPECT_FALSE(m_view.adoptLayout(Move(rtl)));
    ASSERT_TRUE(place());
    EXPECT_FLOAT_EQ(m_placement.caret.x, oldCaret);
    TextService service(m_arena);
    EXPECT_EQ(m_view.shape(service, request), TextLayoutStatus::InvalidParameters);
    EXPECT_EQ(m_view.layout().utf8(), "abc");
}

TEST_F(EditBoxLayoutTests, SnapshotOwnsModelBytesAndReshapingRejectsUnsupportedSingleLineControls){
    ASSERT_TRUE(m_model.setText("owned"));
    ASSERT_TRUE(shapeView());
    const u64 revision = m_view.revision();
    ASSERT_TRUE(m_model.setText("replacement"));
    EXPECT_EQ(m_view.displayText(), "owned");
    EXPECT_EQ(m_view.layout().utf8(), "owned");
    EXPECT_EQ(m_view.revision(), revision);
    EditBoxView moved(Move(m_view));
    EXPECT_EQ(moved.displayText(), "owned");
    EXPECT_EQ(moved.layout().utf8(), "owned");
    EXPECT_TRUE(moved.ready());
    ASSERT_TRUE(m_model.setText("A\tB"));
    ASSERT_TRUE(moved.snapshot(m_model));
    EXPECT_FALSE(moved.ready());
    TextService service(m_arena);
    EXPECT_EQ(moved.shape(service), TextLayoutStatus::UnsupportedControl);
    EXPECT_FALSE(moved.arrange({}, {}, {}, 0.0f, m_placement));
}

TEST_F(EditBoxLayoutTests, EmptyTextKeepsOneCaretAndCanPaintWithoutAFontOrEditSkinRegion){
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    ASSERT_EQ(m_view.caretStops().size(), 1u);
    EXPECT_FLOAT_EQ(m_placement.caret.x, 10.0f);
    EXPECT_FLOAT_EQ(m_placement.caret.height, 12.0f);
    UiSkin skin(m_arena, Name("tests/ui/edit_box/skin"));
    UiSkin::RegionVector regions(m_arena);
    skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/edit_box/texture"), 4u, 4u, 1.0f, Move(regions));
    PaintBuilder paint(m_arena);
    TextService service(m_arena);
    EditBoxPaintFlags flags;
    flags.focused = true;
    flags.caretVisible = false;
    paint.begin({ 200.0f, 100.0f, 1.0f, 1.0f }, 1u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/edit_box/skin"), skin);
    ASSERT_TRUE(m_view.paint(service, paint, skin, m_placement, {}, flags));
    EXPECT_EQ(paint.freeze().vertices().size(), 4u);
    flags.caretVisible = true;
    paint.begin({ 200.0f, 100.0f, 1.0f, 1.0f }, 2u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/edit_box/skin"), skin);
    ASSERT_TRUE(m_view.paint(service, paint, skin, m_placement, {}, flags));
    EXPECT_EQ(paint.freeze().vertices().size(), 8u);
    flags.readOnly = true;
    paint.begin({ 200.0f, 100.0f, 1.0f, 1.0f }, 3u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/edit_box/skin"), skin);
    ASSERT_TRUE(m_view.paint(service, paint, skin, m_placement, {}, flags));
    EXPECT_EQ(paint.freeze().vertices().size(), 4u);
}

TEST_F(EditBoxLayoutTests, FontPaintingUsesLogicalCaretGeometryAndCurrentDisplayScale){
    Font font(m_arena, Name("tests/ui/edit_box/font"));
    const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY)
        / "latin.font";
    Core::Assets::AssetBytes bytes(m_arena);
    ASSERT_TRUE(Tests::ReadBundledFontBytes(path, bytes));
    font.setFontBytes(Move(bytes));
    ASSERT_TRUE(font.validatePayload());
    TextService service(m_arena);
    const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/edit_box/font"), font, 1u };
    ASSERT_TRUE(service.setFonts(&source, 1u));
    ASSERT_TRUE(m_model.setText("ffi"));
    ASSERT_TRUE(m_model.setSelection(1u, 2u));
    ASSERT_TRUE(m_view.snapshot(m_model));
    ASSERT_EQ(m_view.shape(service), TextLayoutStatus::Success);
    ASSERT_TRUE(place());
    const f32 caretX = m_placement.caret.x;
    UiSkin skin(m_arena, Name("tests/ui/edit_box/skin"));
    UiSkin::RegionVector regions(m_arena);
    regions.push_back({ Name("button.normal"), { 0u, 0u, 4u, 4u } });
    skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/edit_box/texture"), 4u, 4u, 1.0f, Move(regions));
    PaintBuilder paint(m_arena);
    EditBoxPaintFlags flags;
    flags.focused = true;
    for(const f32 scale : { 1.0f, 2.0f }){
        paint.begin({ 200.0f, 100.0f, scale, scale }, static_cast<u64>(scale), 1u,
            Core::Assets::AssetRef<UiSkin>("tests/ui/edit_box/skin"), skin);
        ASSERT_TRUE(m_view.paint(service, paint, skin, m_placement, {}, flags));
        EXPECT_FALSE(paint.popClip());
        const auto snapshot = paint.freeze();
        ASSERT_FALSE(snapshot.glyphPages().empty());
        EXPECT_FLOAT_EQ(snapshot.displayMetrics().pixelScaleX, scale);
        EXPECT_EQ(snapshot.commands()[0].material, PaintMaterial::Skin);
        for(const Vertex& vertex : snapshot.vertices()){
            EXPECT_GE(vertex.position.x, m_placement.bounds.x);
            EXPECT_LE(vertex.position.x, m_placement.bounds.x + m_placement.bounds.width);
        }
        EXPECT_FLOAT_EQ(m_placement.caret.x, caretX);
    }
    ASSERT_TRUE(m_model.setText(""));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("A", 0u, 0u));
    ASSERT_TRUE(m_view.snapshot(m_model));
    ASSERT_EQ(m_view.shape(service), TextLayoutStatus::Success);
    ASSERT_TRUE(place());
    flags.caretVisible = false;
    flags.preeditCaretVisible = false;
    paint.begin({ 200.0f, 100.0f, 1.0f, 1.0f }, 3u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/edit_box/skin"), skin);
    ASSERT_TRUE(m_view.paint(service, paint, skin, m_placement, {}, flags));
    const usize hiddenVertices = paint.freeze().vertices().size();
    flags.preeditCaretVisible = true;
    paint.begin({ 200.0f, 100.0f, 1.0f, 1.0f }, 4u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/edit_box/skin"), skin);
    ASSERT_TRUE(m_view.paint(service, paint, skin, m_placement, {}, flags));
    EXPECT_EQ(paint.freeze().vertices().size(), hiddenVertices + 4u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

