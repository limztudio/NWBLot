// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_fixture_support.h"
#include "widget_id_assertion.h"
#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/images/image_source.h>

#include <global/simdmath.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_texture_image_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

constexpr AStringView s_PanelKey = "panel";
constexpr AStringView s_BeforeKey = "before";
constexpr AStringView s_CopiedImagePath = "tests/ui/copied_builder_image";
constexpr AStringView s_FrozenImagePath = "tests/ui/frozen_builder_image";
constexpr AStringView s_VersionedImagePath = "tests/ui/versioned_builder_image";
constexpr AStringView s_CapacityImagePath = "tests/ui/capacity_image";

struct TextureImagePaintSample{
    Rect bounds;
    Rect uv;
    Color color;
    u32 layer = 0u;
    usize quads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<SharedImageSource> MakeImage(Core::Alloc::GlobalArena& arena, const StringView path = "tests/ui/builder_image", const u8 seed = 37u, const u32 width = 20u, const u32 height = 12u){
    return UiFixtureSupport::MakeSeededImage(arena, path, seed, width, height);
}

[[nodiscard]] static Expected<TextureImagePaintSample> SampleImage(const DrawSnapshot& snapshot, const u64 generation)noexcept{
    TextureImagePaintSample candidate;
    // Lanes are x, y.
    SIMDVectorSingle origin = VectorZero();
    SIMDVectorSingle corner = VectorZero();
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Image || command.textureImageIndex >= snapshot.textureImages().size())
            continue;
        const SharedImageSource& source = snapshot.textureImages()[command.textureImageIndex];
        if(!source || source->generation() != generation)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            const SIMDVectorSingle firstPosition = VectorSet(first.position.x, first.position.y, 0.0f, 0.0f);
            const SIMDVectorSingle oppositePosition = VectorSet(opposite.position.x, opposite.position.y, 0.0f, 0.0f);
            if(candidate.quads == 0u){
                origin = firstPosition;
                corner = oppositePosition;
                const SIMDVectorSingle uvSize = VectorSubtract(VectorSet(opposite.texCoord.x, opposite.texCoord.y, 0.0f, 0.0f), VectorSet(first.texCoord.x, first.texCoord.y, 0.0f, 0.0f));
                candidate.uv = { first.texCoord.x, first.texCoord.y, VectorGetX(uvSize), VectorGetY(uvSize) };
                candidate.color = first.color;
                candidate.layer = command.layer;
            }
            origin = VectorMin(origin, firstPosition);
            corner = VectorMax(corner, oppositePosition);
            ++candidate.quads;
        }
    }
    if(candidate.quads == 0u)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle size = VectorSubtract(corner, origin);
    candidate.bounds = { VectorGetX(origin), VectorGetY(origin), VectorGetX(size), VectorGetY(size) };
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextureImageBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] static ImageOptions Fixed(f32 width = 96.0f, f32 height = 40.0f)noexcept;
    [[nodiscard]] static PopupOptions ParentOptions()noexcept;
    [[nodiscard]] static PopupOptions ChildOptions()noexcept;


protected:
    [[nodiscard]] bool panel(u64 generation);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ImageOptions UiTextureImageBuilderTests::Fixed(const f32 width, const f32 height)noexcept{
    ImageOptions options;
    options.width = { LayoutSizePolicy::Fixed, width };
    options.height = { LayoutSizePolicy::Fixed, height };
    return options;
}

PopupOptions UiTextureImageBuilderTests::ParentOptions()noexcept{
    PopupOptions options;
    options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 300.0f, 280.0f };
    return options;
}

PopupOptions UiTextureImageBuilderTests::ChildOptions()noexcept{
    PopupOptions options;
    options.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 280.0f, 240.0f };
    return options;
}

bool UiTextureImageBuilderTests::panel(const u64 generation){
    return begin(generation) && m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 360.0f, 400.0f });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextureImageBuilderTests, DeclarationRetainsSourceAndOptionsAcrossCallerReplacementAndRelease){
    auto sourceResult = MakeImage(m_arena, s_CopiedImagePath, 37u);
    ASSERT_TRUE(sourceResult);
    SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    const u64 generation = source->generation();
    ImageOptions options = Fixed(80.0f, 32.0f);
    options.tint = { 2.0f, 0.8f, 0.6f, 0.5f };
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.image("image", source, options));
    auto sourceReplacement = MakeImage(m_arena, s_CopiedImagePath, 73u, 9u, 7u);
    ASSERT_TRUE(sourceReplacement);
    source = Move(*sourceReplacement);
    ASSERT_TRUE(source);
    ASSERT_NE(source->generation(), generation);
    source.reset();
    options = Fixed(5.0f, 6.0f);
    options.tint = { 0.0f, 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.textureImages().size(), 1u);
    EXPECT_EQ(snapshot.textureImages()[0u]->generation(), generation);
    EXPECT_EQ(snapshot.textureImages()[0u]->texture().width(), 20u);
    EXPECT_EQ(snapshot.textureImages()[0u]->texture().payloadBytes()[0u], 37u);
    const auto sample = SampleImage(snapshot, generation);
    ASSERT_TRUE(sample);
    EXPECT_FLOAT_EQ(sample->bounds.width, 80.0f);
    EXPECT_FLOAT_EQ(sample->bounds.height, 32.0f);
    EXPECT_FLOAT_EQ(sample->color.r, 1.0f);
    EXPECT_FLOAT_EQ(sample->color.g, 0.4f);
    EXPECT_FLOAT_EQ(sample->color.b, 0.3f);
    EXPECT_FLOAT_EQ(sample->color.a, 0.5f);
}

TEST_F(UiTextureImageBuilderTests, FrozenSnapshotsKeepVersionsAcrossBuilderResetAndSourceRelease){
    auto sourceResult = MakeImage(m_arena, s_FrozenImagePath, 37u);
    ASSERT_TRUE(sourceResult);
    SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    const u64 original = source->generation();
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.image("image", source));
    ASSERT_TRUE(finishPanel());
    DrawSnapshot retained = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    source.reset();
    auto sourceReplacement = MakeImage(m_arena, s_FrozenImagePath, 73u, 9u, 7u);
    ASSERT_TRUE(sourceReplacement);
    source = Move(*sourceReplacement);
    ASSERT_TRUE(source);
    const u64 replacement = source->generation();
    ASSERT_TRUE(panel(2u));
    ASSERT_TRUE(m_builder.image("image", source));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot next = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(2u));
    source.reset();
    ASSERT_TRUE(panel(3u));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot empty = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(3u));
    ASSERT_EQ(retained.textureImages().size(), 1u);
    ASSERT_EQ(next.textureImages().size(), 1u);
    EXPECT_TRUE(empty.textureImages().empty());
    EXPECT_EQ(retained.textureImages()[0u]->generation(), original);
    EXPECT_EQ(next.textureImages()[0u]->generation(), replacement);
    EXPECT_NE(original, replacement);
    EXPECT_EQ(retained.textureImages()[0u]->texture().payloadBytes()[0u], 37u);
    EXPECT_EQ(next.textureImages()[0u]->texture().payloadBytes()[0u], 73u);
    const auto oldSample = SampleImage(retained, original);
    ASSERT_TRUE(oldSample);
    const auto nextSample = SampleImage(next, replacement);
    ASSERT_TRUE(nextSample);
    EXPECT_FLOAT_EQ(oldSample->bounds.width, 20.0f);
    EXPECT_FLOAT_EQ(nextSample->bounds.width, 9.0f);
}

TEST_F(UiTextureImageBuilderTests, SamePathVersionsStayDistinctAndRepeatedHandlesKeepOneBinding){
    auto firstResult = MakeImage(m_arena, s_VersionedImagePath, 37u);
    ASSERT_TRUE(firstResult);
    const SharedImageSource first = Move(*firstResult);
    auto secondResult = MakeImage(m_arena, s_VersionedImagePath, 73u, 9u, 7u);
    ASSERT_TRUE(secondResult);
    const SharedImageSource second = Move(*secondResult);
    ASSERT_TRUE(first && second);
    ASSERT_EQ(first->identity(), second->identity());
    ASSERT_NE(first->generation(), second->generation());
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.image("first", first));
    ASSERT_TRUE(m_builder.image("second", second));
    ASSERT_TRUE(m_builder.image("again", first));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.textureImages().size(), 2u);
    EXPECT_EQ(snapshot.textureImages()[0u].get(), first.get());
    EXPECT_EQ(snapshot.textureImages()[1u].get(), second.get());
    FixedVector<u32, 3u> indices;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material == PaintMaterial::Image)
            indices.push_back(command.textureImageIndex);
    }
    ASSERT_EQ(indices.size(), 3u);
    EXPECT_EQ(indices[0u], 0u);
    EXPECT_EQ(indices[1u], 1u);
    EXPECT_EQ(indices[2u], 0u);
}

TEST_F(UiTextureImageBuilderTests, ExternalClipTrimsFullImageUvsAndRestoresThePaintStack){
    auto sourceResult = MakeImage(m_arena);
    ASSERT_TRUE(sourceResult);
    const SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.image("image", source, Fixed()));
    m_paint.pushClip({ 20.0f, 0.0f, 20.0f, 600.0f });
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_paint.popClip());
    m_paint.fillRect({ 60.0f, 80.0f, 8.0f, 8.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
    const DrawSnapshot snapshot = m_paint.freeze();
    const auto sample = SampleImage(snapshot, source->generation());
    ASSERT_TRUE(sample);
    EXPECT_FLOAT_EQ(sample->bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(sample->bounds.width, 20.0f);
    EXPECT_NEAR(sample->uv.x, 7.0f / 96.0f, 0.000001f);
    EXPECT_NEAR(VectorGetX(VectorAdd(VectorReplicate(sample->uv.x), VectorReplicate(sample->uv.width))), 27.0f / 96.0f, 0.000001f);
    bool sentinel = false;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Solid)
            continue;
        const Vertex& vertex = snapshot.vertices()[snapshot.indices()[command.firstIndex]];
        sentinel |= vertex.position.x == 60.0f && vertex.position.y == 80.0f;
    }
    EXPECT_TRUE(sentinel);
}

TEST_F(UiTextureImageBuilderTests, TransparentAndZeroAxisImagesAreValidWithoutBindingsOrQuads){
    auto sourceResult = MakeImage(m_arena);
    ASSERT_TRUE(sourceResult);
    const SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    ASSERT_TRUE(panel(1u));
    ImageOptions transparent = Fixed();
    transparent.tint.a = 0.0f;
    ASSERT_TRUE(m_builder.image("transparent", source, transparent));
    ASSERT_TRUE(m_builder.image("zero_width", source, Fixed(0.0f, 20.0f)));
    ASSERT_TRUE(m_builder.image("zero_height", source, Fixed(20.0f, 0.0f)));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_TRUE(snapshot.textureImages().empty());
    EXPECT_FALSE(SampleImage(snapshot, source->generation()));
}

TEST_F(UiTextureImageBuilderTests, ActiveNullSourceRejectsEvenTransparentPolicyWithoutReplacingAcceptedInput){
    auto sourceResult = MakeImage(m_arena);
    ASSERT_TRUE(sourceResult);
    const SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    ASSERT_TRUE(panel(1u));
    EXPECT_FALSE(m_builder.button(s_BeforeKey, "Before"));
    ASSERT_TRUE(m_builder.image("image", source));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId before = id(s_BeforeKey, s_PanelKey);
    ASSERT_NE(target(before), nullptr);
    const HitTarget accepted = *target(before);
    const usize count = m_context.input().targets().size();
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Tab }).commandConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Tab }).commandConsumed);
    ASSERT_EQ(m_context.input().focus(), before);
    ASSERT_TRUE(panel(2u));
    ImageOptions options = Fixed(0.0f, 0.0f);
    options.tint.a = 0.0f;
    EXPECT_FALSE(m_builder.image("image", SharedImageSource{}, options));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    ASSERT_NE(target(before), nullptr);
    EXPECT_EQ(target(before)->declarationGeneration, accepted.declarationGeneration);
    EXPECT_FLOAT_EQ(target(before)->rectangle.x, accepted.rectangle.x);
    EXPECT_EQ(m_context.input().targets().size(), count);
    EXPECT_EQ(m_context.input().focus(), before);
}

TEST_F(UiTextureImageBuilderTests, InvalidOptionsDoNotReplaceTheAcceptedInputFrame){
    auto sourceResult = MakeImage(m_arena);
    ASSERT_TRUE(sourceResult);
    const SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    ASSERT_TRUE(panel(1u));
    EXPECT_FALSE(m_builder.button(s_BeforeKey, "Before"));
    ASSERT_TRUE(m_builder.image("image", source));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId before = id(s_BeforeKey, s_PanelKey);
    ASSERT_NE(target(before), nullptr);
    const HitTarget accepted = *target(before);
    const usize count = m_context.input().targets().size();
    ASSERT_TRUE(panel(2u));
    ImageOptions options = Fixed();
    options.tint.r = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(m_builder.image("image", source, options));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    ASSERT_NE(target(before), nullptr);
    EXPECT_EQ(target(before)->declarationGeneration, accepted.declarationGeneration);
    EXPECT_FLOAT_EQ(target(before)->rectangle.width, accepted.rectangle.width);
    EXPECT_EQ(m_context.input().targets().size(), count);
}

TEST_F(UiTextureImageBuilderTests, EndedNestedChildRetainsSourceAndOptionsUntilTheOuterPopupPaints){
    PopupState parent;
    PopupState child;
    parent.open();
    child.open();
    auto sourceResult = MakeImage(m_arena, "tests/ui/nested_builder_image", 37u);
    ASSERT_TRUE(sourceResult);
    SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    const u64 generation = source->generation();
    ImageOptions options = Fixed(80.0f, 32.0f);
    options.tint = { 0.8f, 1.0f, 0.6f, 0.5f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("parent", parent, ParentOptions()));
    ASSERT_TRUE(m_builder.beginPopup("child", child, ChildOptions()));
    ASSERT_TRUE(m_builder.image("image", source, options));
    ASSERT_TRUE(m_builder.endPopup());
    source.reset();
    options = Fixed(0.0f, 0.0f);
    options.tint.a = 0.0f;
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.textureImages().size(), 1u);
    EXPECT_EQ(snapshot.textureImages()[0u]->generation(), generation);
    EXPECT_EQ(snapshot.textureImages()[0u]->texture().payloadBytes()[0u], 37u);
    const auto sample = SampleImage(snapshot, generation);
    ASSERT_TRUE(sample);
    EXPECT_FLOAT_EQ(sample->bounds.width, 80.0f);
    EXPECT_FLOAT_EQ(sample->bounds.height, 32.0f);
    EXPECT_FLOAT_EQ(sample->color.r, 0.4f);
    EXPECT_FLOAT_EQ(sample->color.a, 0.5f);
    EXPECT_EQ(sample->layer, 2u);
    EXPECT_EQ(m_context.input().popupCount(), 2u);
}

TEST_F(UiTextureImageBuilderTests, AncestorClosureSuppressesEndedChildImageAndAnnotation){
    PopupState parent;
    PopupState child;
    TooltipState tooltip;
    parent.open();
    child.open();
    auto sourceResult = MakeImage(m_arena);
    ASSERT_TRUE(sourceResult);
    SharedImageSource source = Move(*sourceResult);
    ASSERT_TRUE(source);
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("parent", parent, ParentOptions()));
    ASSERT_TRUE(m_builder.beginPopup("child", child, ChildOptions()));
    ASSERT_TRUE(m_builder.image("image", source, Fixed()));
    ASSERT_TRUE(m_builder.tooltip("hint", "image", "Child image", tooltip));
    ASSERT_TRUE(m_builder.endPopup());
    source.reset();
    parent.close();
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_TRUE(snapshot.textureImages().empty());
    EXPECT_EQ(m_context.input().popupCount(), 0u);
    const WidgetId parentId = UiTestSupport::RequireWidgetId(MakeWidgetId(UiTestSupport::RequireWidgetId(MakeRootId(m_root)), "parent"));
    EXPECT_EQ(target(UiTestSupport::RequireWidgetId(MakeWidgetId(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId, "child")), "image"))), nullptr);
}

TEST_F(UiTextureImageBuilderTests, VisibleImageAtFullBindingCapacityRejectsDeferredPaint){
    FixedVector<SharedImageSource, s_PaintMaxImages> images;
    for(usize index = 0u; index < images.max_size(); ++index){
        auto imageResult = MakeImage(m_arena, s_CapacityImagePath, static_cast<u8>(index), 4u, 4u);
        ASSERT_TRUE(imageResult);
        SharedImageSource image = Move(*imageResult);
        ASSERT_TRUE(image);
        images.push_back(Move(image));
    }
    auto additionResult = MakeImage(m_arena);
    ASSERT_TRUE(additionResult);
    const SharedImageSource addition = Move(*additionResult);
    ASSERT_TRUE(addition);
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_paint.prepareTextureImages(images.data(), images.size()));
    ASSERT_TRUE(m_builder.image("image", addition, Fixed()));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(1u));
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(snapshot.textureImages().size(), s_PaintMaxImages);
    EXPECT_FALSE(SampleImage(snapshot, addition->generation()));
}

TEST_F(UiTextureImageBuilderTests, FullyClippedImageAtFullBindingCapacityConsumesNoAdditionalSlot){
    FixedVector<SharedImageSource, s_PaintMaxImages> images;
    for(usize index = 0u; index < images.max_size(); ++index){
        auto imageResult = MakeImage(m_arena, s_CapacityImagePath, static_cast<u8>(index), 4u, 4u);
        ASSERT_TRUE(imageResult);
        SharedImageSource image = Move(*imageResult);
        ASSERT_TRUE(image);
        images.push_back(Move(image));
    }
    auto additionResult = MakeImage(m_arena);
    ASSERT_TRUE(additionResult);
    const SharedImageSource addition = Move(*additionResult);
    ASSERT_TRUE(addition);
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_paint.prepareTextureImages(images.data(), images.size()));
    ASSERT_TRUE(m_builder.image("image", addition, Fixed()));
    m_paint.pushClip({ 0.0f, 0.0f, 1.0f, 1.0f });
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_paint.popClip());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(snapshot.textureImages().size(), s_PaintMaxImages);
    EXPECT_FALSE(SampleImage(snapshot, addition->generation()));
    EXPECT_FALSE(m_context.failed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

