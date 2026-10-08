// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/sha256.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

class UiBuilderTests : public testing::Test{
public:
    UiBuilderTests()
        : m_arena(Name("tests/ui/builder"))
        , m_font(m_arena, Name("tests/ui/fonts/latin"))
        , m_text(m_arena)
        , m_skin(m_arena, Name("tests/ui/skin"))
        , m_paint(m_arena)
        , m_context(m_arena)
        , m_builder(m_arena, m_context, m_paint, m_text)
    {}


protected:
    virtual void SetUp()override{
        const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY)
            / "latin.font";
        Core::Assets::AssetBytes bytes(m_arena);
        auto bytesResult = Tests::ReadBundledFontBytes(path, bytes.get_allocator().arena());
        ASSERT_TRUE(bytesResult);
        bytes = Move(*bytesResult);
        m_font.setFontBytes(Move(bytes));
        ASSERT_TRUE(m_font.validatePayload());
        const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 1u };
        ASSERT_TRUE(m_text.setFonts(&source, 1u));
        configureSkin();
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
    }

    void configureSkin(const bool includePanel = true, const bool includeButton = true){
        UiSkin::RegionVector regions(m_arena);
        if(includePanel)
            regions.push_back({ Name("panel.normal"), { 0u, 0u, 8u, 8u }, {}, { 4.0f, 6.0f, 8.0f, 10.0f } });
        if(includeButton)
            regions.push_back({ Name("button.normal"), { 8u, 0u, 8u, 8u }, {}, { 1.0f, 2.0f, 3.0f, 4.0f }, 20.0f, 20.0f });
        regions.push_back({ Name("button.hover"), { 16u, 0u, 8u, 8u }, {}, { 7.0f, 1.0f, 2.0f, 3.0f }, 40.0f, 30.0f });
        regions.push_back({ Name("button.pressed"), { 24u, 0u, 8u, 8u }, {}, { 2.0f, 9.0f, 5.0f, 1.0f }, 30.0f, 35.0f });
        regions.push_back({ Name("button.disabled"), { 32u, 0u, 8u, 8u }, {}, { 3.0f, 4.0f, 11.0f, 12.0f }, 70.0f, 50.0f });
        regions.push_back({ Name("checkbox.normal"), { 40u, 0u, 8u, 8u }, {}, {}, 10.0f, 10.0f });
        regions.push_back({ Name("checkbox.hover"), { 48u, 0u, 8u, 8u }, {}, {}, 20.0f, 24.0f });
        regions.push_back({ Name("checkbox.checked"), { 56u, 0u, 8u, 8u }, {}, {}, 28.0f, 30.0f });
        regions.push_back({ Name("checkbox.disabled"), { 64u, 0u, 8u, 8u }, {}, {}, 34.0f, 32.0f });
        regions.push_back({ Name("checkbox.mark"), { 72u, 0u, 8u, 8u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
        regions.push_back({ Name("focus.overlay"), { 80u, 0u, 8u, 8u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 128u, 16u, 1.0f, Move(regions));
    }

    [[nodiscard]] bool begin(const u64 generation, const Rect& bounds = { 0.0f, 0.0f, 300.0f, 200.0f }){
        if(!m_context.beginFrame(generation))
            return false;
        m_paint.begin({ 300.0f, 200.0f, 1.0f, 1.0f }, generation, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        return m_context.beginRoot(m_root) && m_builder.beginPanel("panel", bounds);
    }

    [[nodiscard]] bool finish(){
        return m_builder.endPanel() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetId id(const AStringView stableKey)const{
        return MakeWidgetId(MakeWidgetId(MakeRootId(m_root), "panel"), stableKey);
    }

    [[nodiscard]] const WidgetState* state(const WidgetId widget)const{
        for(const WidgetState& entry : m_context.states().entries()){
            if(entry.id == widget)
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] const HitTarget* target(const WidgetId widget)const{
        for(const HitTarget& entry : m_context.input().targets()){
            if(entry.id == widget)
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] Expected<Rect> regionQuad(const DrawSnapshot& snapshot, const u32 slot)const noexcept{
        const f32 left = static_cast<f32>(slot) / 16.0f;
        const f32 right = static_cast<f32>(slot + 1u) / 16.0f;
        for(const DrawCommand& command : snapshot.commands()){
            if(command.material != PaintMaterial::Skin)
                continue;
            for(u32 index = command.firstIndex; index < command.firstIndex + command.indexCount; index += 6u){
                const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
                const Vertex& last = snapshot.vertices()[snapshot.indices()[index + 2u]];
                if(
                    Abs(first.texCoord.x - left) < 1e-6f && Abs(last.texCoord.x - right) < 1e-6f
                    && Abs(first.texCoord.y) < 1e-6f && Abs(last.texCoord.y - 0.5f) < 1e-6f
                ){
                    return Rect{ first.position.x, first.position.y,
                        last.position.x - first.position.x, last.position.y - first.position.y };
                }
            }
        }
        return MakeUnexpected(Failure{});
    }

    InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_context.input().queue(event));
        return m_context.input().process();
    }

    void click(const Point& point){
        EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Font m_font;
    TextService m_text;
    UiSkin m_skin;
    PaintBuilder m_paint;
    Context m_context;
    Builder m_builder;
    WidgetRoot m_root{ 55u, 3u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiBuilderTests, LabelTextChangesPreserveStableIdAndDeclarationLifetime){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.label("caption", "A"));
    ASSERT_TRUE(finish());
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_EQ(first.generation(), 1u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId caption = id("caption");
    const WidgetState* original = state(caption);
    ASSERT_NE(original, nullptr);
    const u64 lifetime = original->declarationGeneration;
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.label("caption", "A changed caption"));
    ASSERT_TRUE(finish());
    const DrawSnapshot second = m_paint.freeze();
    const WidgetState* updated = state(caption);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->id, caption);
    EXPECT_EQ(updated->declarationGeneration, lifetime);
    EXPECT_GT(second.vertices().size(), first.vertices().size());
    EXPECT_EQ(first.generation(), 1u);
    EXPECT_FALSE(first.glyphPages().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
}

TEST_F(UiBuilderTests, CheckboxTogglesOncePerAcceptedActionAcrossRepeatedBuilds){
    bool checked = false;
    ASSERT_TRUE(begin(1u));
    EXPECT_FALSE(m_builder.checkbox("enabled", "Enabled", checked));
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* control = target(id("enabled"));
    ASSERT_NE(control, nullptr);
    const Point center{ control->rectangle.x + 4.0f, control->rectangle.y + 4.0f };
    click(center);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    EXPECT_FALSE(m_context.input().process().pointerConsumed);
    EXPECT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(begin(2u));
    EXPECT_TRUE(m_builder.checkbox("enabled", "Enabled", checked));
    EXPECT_TRUE(checked);
    EXPECT_TRUE(m_context.input().actions().empty());
    ASSERT_TRUE(finish());
    EXPECT_FALSE(m_context.beginFrame(3u));
    EXPECT_FALSE(m_context.input().process().pointerConsumed);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(begin(3u));
    EXPECT_FALSE(m_builder.checkbox("enabled", "Enabled", checked));
    EXPECT_TRUE(checked);
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiBuilderTests, ButtonChecksCurrentEnabledValueBeforeTakingPendingActivation){
    ASSERT_TRUE(begin(1u));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_FALSE(m_context.failed());
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* control = target(id("apply"));
    ASSERT_NE(control, nullptr);
    const Point point{ control->rectangle.x + 4.0f, control->rectangle.y + 4.0f };
    click(point);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(begin(2u));
    WidgetOptions disabled;
    disabled.enabled = false;
    EXPECT_FALSE(m_builder.button("apply", "Apply", disabled));
    EXPECT_FALSE(m_context.failed());
    EXPECT_TRUE(m_context.input().actions().empty());
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(2u));
    control = target(id("apply"));
    ASSERT_NE(control, nullptr);
    EXPECT_FALSE(control->enabled);
    EXPECT_FALSE(control->focusable);
    EXPECT_FALSE(control->activatable);
    EXPECT_NE(m_context.input().hitTest(point), control->id);
    click(point);
    EXPECT_TRUE(m_context.input().actions().empty());
    ASSERT_TRUE(begin(3u));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    EXPECT_FALSE(m_context.failed());
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_TRUE(target(id("apply"))->enabled);
}

TEST_F(UiBuilderTests, MissingSkinRejectsDeclarationsWithoutPublishingCandidateTargets){
    ASSERT_TRUE(begin(1u));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finish());
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId accepted = id("apply");
    ASSERT_EQ(m_context.input().targets().size(), 2u);
    Builder noSkin(m_arena, m_context, m_paint, m_text);
    ASSERT_TRUE(m_context.beginFrame(2u));
    m_paint.begin({ 300.0f, 200.0f, 1.0f, 1.0f }, 2u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
    ASSERT_TRUE(m_context.beginRoot(m_root));
    EXPECT_FALSE(noSkin.beginPanel("panel", { 100.0f, 100.0f, 100.0f, 100.0f }));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), accepted);
    EXPECT_EQ(first.generation(), 1u);
}

TEST_F(UiBuilderTests, MissingBaseControlRegionRejectsFrameAndKeepsAcceptedHitLayout){
    ASSERT_TRUE(begin(1u));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finish());
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    configureSkin(true, false);
    ASSERT_TRUE(begin(2u));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), id("apply"));
    EXPECT_TRUE(regionQuad(first, 1u));
}

TEST_F(UiBuilderTests, RowUsesSkinMetricsPaddingAndClipsOverflowingControlHits){
    ContainerOptions row;
    row.width = { LayoutSizePolicy::Fixed, 100.0f };
    row.height = { LayoutSizePolicy::Fixed, 70.0f };
    row.padding = { 2.0f, 3.0f, 4.0f, 5.0f };
    row.gap = 6.0f;
    ASSERT_TRUE(begin(1u, { 10.0f, 20.0f, 150.0f, 90.0f }));
    ASSERT_TRUE(m_builder.beginRow("actions", row));
    EXPECT_FALSE(m_builder.button("first", "i"));
    EXPECT_FALSE(m_builder.button("second", "ii"));
    ASSERT_TRUE(m_builder.endContainer());
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId rowId = id("actions");
    const WidgetId firstId = MakeWidgetId(rowId, "first");
    const WidgetId secondId = MakeWidgetId(rowId, "second");
    const HitTarget* secondTarget = target(secondId);
    ASSERT_NE(secondTarget, nullptr);
    EXPECT_FLOAT_EQ(secondTarget->clip.width, 18.0f);
    EXPECT_EQ(m_context.input().hitTest({ 100.0f, 40.0f }), secondId);
    EXPECT_NE(m_context.input().hitTest({ 125.0f, 40.0f }), secondId);
    click({ 125.0f, 40.0f });
    EXPECT_TRUE(m_context.input().actions().empty());
    click({ 100.0f, 40.0f });
    EXPECT_TRUE(m_context.input().consumeActivation(secondId));
    EXPECT_FALSE(m_context.input().consumeActivation(firstId));
}

TEST_F(UiBuilderTests, ScopedKeysRemainDistinctAndDuplicateDeclarationsRejectPublication){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginRow("left"));
    ASSERT_TRUE(m_builder.label("caption", "Left"));
    ASSERT_TRUE(m_builder.endContainer());
    ASSERT_TRUE(m_builder.beginRow("right"));
    ASSERT_TRUE(m_builder.label("caption", "Right"));
    ASSERT_TRUE(m_builder.endContainer());
    ASSERT_TRUE(finish());
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId left = MakeWidgetId(id("left"), "caption");
    const WidgetId right = MakeWidgetId(id("right"), "caption");
    EXPECT_NE(left, right);
    EXPECT_NE(state(left), nullptr);
    EXPECT_NE(state(right), nullptr);
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.label("duplicate", "First"));
    EXPECT_FALSE(m_builder.label("duplicate", "Second"));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(first.generation(), 1u);
}

TEST_F(UiBuilderTests, PreparedFrameTargetsStayInaccessibleUntilExactCommit){
    WidgetOptions fixed;
    fixed.width = { LayoutSizePolicy::Fixed, 20.0f };
    fixed.height = { LayoutSizePolicy::Fixed, 20.0f };
    ASSERT_TRUE(begin(1u, { 0.0f, 0.0f, 100.0f, 100.0f }));
    EXPECT_FALSE(m_builder.button("apply", "", fixed));
    ASSERT_TRUE(finish());
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(begin(2u, { 150.0f, 50.0f, 100.0f, 100.0f }));
    EXPECT_FALSE(m_builder.button("apply", "", fixed));
    ASSERT_TRUE(finish());
    const DrawSnapshot prepared = m_paint.freeze();
    EXPECT_EQ(first.generation(), 1u);
    EXPECT_EQ(prepared.generation(), 2u);
    EXPECT_TRUE(m_context.ready());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), id("apply"));
    EXPECT_FALSE(m_context.input().hitTest({ 160.0f, 60.0f }).valid());
    EXPECT_FALSE(send({ InputEventType::PrimaryDown, { 160.0f, 60.0f } }).pointerConsumed);
    EXPECT_FALSE(send({ InputEventType::PrimaryUp, { 160.0f, 60.0f } }).pointerConsumed);
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.beginFrame(3u));
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().hitTest({ 160.0f, 60.0f }), id("apply"));
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 15.0f }).valid());
}

TEST_F(UiBuilderTests, FrozenPaintRetainsValuesAndFontPagesAfterModelChangeAndBuilderReuse){
    bool checked = true;
    AString<Core::Alloc::GlobalArena> caption("A", m_arena);
    ASSERT_TRUE(begin(1u));
    EXPECT_FALSE(m_builder.checkbox("enabled", "Enabled", checked));
    ASSERT_TRUE(m_builder.label("caption", { caption.data(), caption.size() }));
    ASSERT_TRUE(finish());
    const DrawSnapshot first = m_paint.freeze();
    const auto originalMarker = regionQuad(first, 9u);
    ASSERT_TRUE(originalMarker);
    EXPECT_FLOAT_EQ(originalMarker->width, 20.4f);
    ASSERT_FALSE(first.glyphPages().empty());
    const SharedGlyphPage page = first.glyphPages().front();
    const Sha256Digest pixelHash = ComputeSha256({ page->pixels().data(), page->pixels().size() });
    Core::Alloc::ScratchArena scratch(Name("tests/ui/builder/frozen"));
    Vector<Vertex, Core::Alloc::ScratchArena> saved(scratch);
    saved.assign(first.vertices().begin(), first.vertices().end());
    ASSERT_TRUE(m_context.commitFrame(1u));
    checked = false;
    caption.assign("A longer changed model caption");
    ASSERT_TRUE(begin(2u));
    EXPECT_FALSE(m_builder.checkbox("enabled", "Enabled", checked));
    ASSERT_TRUE(m_builder.label("caption", { caption.data(), caption.size() }));
    ASSERT_TRUE(finish());
    const DrawSnapshot second = m_paint.freeze();
    EXPECT_FALSE(regionQuad(second, 9u));
    const auto retainedMarker = regionQuad(first, 9u);
    ASSERT_TRUE(retainedMarker);
    EXPECT_FLOAT_EQ(retainedMarker->width, originalMarker->width);
    ASSERT_EQ(first.vertices().size(), saved.size());
    EXPECT_EQ(NWB_MEMCMP(first.vertices().data(), saved.data(), saved.size() * sizeof(Vertex)), 0);
    EXPECT_EQ(ComputeSha256({ page->pixels().data(), page->pixels().size() }), pixelHash);
    EXPECT_EQ(page->binding().fontGeneration, 1u);
    EXPECT_EQ(first.generation(), 1u);
    EXPECT_EQ(second.generation(), 2u);
    ASSERT_TRUE(m_context.commitFrame(2u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

