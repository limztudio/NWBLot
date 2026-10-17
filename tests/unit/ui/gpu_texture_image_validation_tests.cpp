// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_image_fixture.h"

#include <impl/ecs_ui/toolkit/gpu/renderer_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintImageTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GpuTextureImageValidationFixture = PaintImageFixture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_texture_image_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidImageBindings(const DrawSnapshot& snapshot)noexcept{
    return GpuRendererState::ValidateGlyphPages(snapshot) && GpuRendererState::ValidateSdfPages(snapshot) && GpuRendererState::ValidateTextureImages(snapshot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(GpuTextureImageValidationFixture, SameIdentityDistinctGenerationsRemainValidImageBindings){
    auto firstResult = makeImage("tests/ui/versioned_gpu_image", 37u);
    ASSERT_TRUE(firstResult);
    const SharedImageSource first = Move(*firstResult);
    auto secondResult = makeImage("tests/ui/versioned_gpu_image", 73u);
    ASSERT_TRUE(secondResult);
    const SharedImageSource second = Move(*secondResult);
    ASSERT_TRUE(first && second);
    ASSERT_EQ(first->identity(), second->identity());
    ASSERT_NE(first->generation(), second->generation());
    const Rect rectangle{ 2.0f, 3.0f, 16.0f, 8.0f };
    ASSERT_TRUE(m_builder.drawImage(first, rectangle));
    ASSERT_TRUE(m_builder.drawImage(second, rectangle));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.textureImages().size(), 2u);
    ASSERT_EQ(snapshot.commands().size(), 2u);
    EXPECT_TRUE(GpuRendererState::ValidateGlyphPages(snapshot));
    EXPECT_TRUE(GpuRendererState::ValidateSdfPages(snapshot));
    EXPECT_TRUE(GpuRendererState::ValidateTextureImages(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, ImageIndexMustAddressAnOwnedTextureSource){
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    ASSERT_TRUE(image);
    ASSERT_TRUE(m_builder.drawImage(image, { 2.0f, 3.0f, 16.0f, 8.0f }));
    DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 1u);
    DrawCommand& command = const_cast<DrawCommand&>(snapshot.commands()[0u]);
    const Array<u32, 2u> invalidIndices{ 1u, Limit<u32>::s_Max };
    for(const u32 index : invalidIndices){
        command.textureImageIndex = index;
        EXPECT_FALSE(GpuRendererState::ValidateTextureImages(snapshot));
        EXPECT_FALSE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
    }
    command.textureImageIndex = 0u;
    EXPECT_TRUE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, NullTextureSourceRejectsTheSnapshot){
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    ASSERT_TRUE(image);
    ASSERT_TRUE(m_builder.drawImage(image, { 2.0f, 3.0f, 16.0f, 8.0f }));
    DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.textureImages().size(), 1u);
    const_cast<SharedImageSource&>(snapshot.textureImages()[0u]).reset();
    EXPECT_FALSE(GpuRendererState::ValidateTextureImages(snapshot));
    EXPECT_FALSE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, DuplicateSourceGenerationRejectsTheSnapshot){
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    ASSERT_TRUE(image);
    ASSERT_TRUE(m_builder.drawImage(image, { 2.0f, 3.0f, 16.0f, 8.0f }));
    DrawSnapshot snapshot = m_builder.freeze();
    auto& images = const_cast<PaintVector<SharedImageSource>&>(snapshot.textureImages());
    images.push_back(image);
    ASSERT_EQ(images.size(), 2u);
    ASSERT_EQ(images[0u]->generation(), images[1u]->generation());
    EXPECT_FALSE(GpuRendererState::ValidateTextureImages(snapshot));
    EXPECT_FALSE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, TextureBindingCountOverflowRejectsEveryValidator){
    auto firstResult = makeImage();
    ASSERT_TRUE(firstResult);
    const SharedImageSource first = Move(*firstResult);
    ASSERT_TRUE(first);
    ASSERT_TRUE(m_builder.drawImage(first, { 2.0f, 3.0f, 16.0f, 8.0f }));
    DrawSnapshot snapshot = m_builder.freeze();
    auto& images = const_cast<PaintVector<SharedImageSource>&>(snapshot.textureImages());
    for(usize index = 1u; index <= s_PaintMaxImages; ++index){
        auto additionResult = makeImage("tests/ui/overflow_gpu_image", static_cast<u8>(index));
        ASSERT_TRUE(additionResult);
        SharedImageSource addition = Move(*additionResult);
        ASSERT_TRUE(addition);
        images.push_back(Move(addition));
    }
    ASSERT_EQ(images.size(), s_PaintMaxImages + 1u);
    EXPECT_TRUE(snapshot.glyphPages().empty());
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_FALSE(GpuRendererState::ValidateGlyphPages(snapshot));
    EXPECT_FALSE(GpuRendererState::ValidateSdfPages(snapshot));
    EXPECT_FALSE(GpuRendererState::ValidateTextureImages(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, SharedImageBudgetAcceptsMixedSourcesAtCapacity){
    FixedVector<SharedGlyphPage, s_PaintMaxImages - 2u> glyphs;
    for(usize index = 0u; index < glyphs.max_size(); ++index){
        auto glyphCreationResult = makeGlyph(100u + index);
        ASSERT_TRUE(glyphCreationResult);
        SharedGlyphPage glyph = Move(*glyphCreationResult);
        ASSERT_TRUE(glyph);
        glyphs.push_back(Move(glyph));
    }
    auto sdfCreationResult = makeSdf();
    ASSERT_TRUE(sdfCreationResult);
    const SharedSdfAtlasPage sdf = Move(*sdfCreationResult);
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    ASSERT_TRUE(sdf && image);
    ASSERT_TRUE(m_builder.prepareImages(glyphs.data(), glyphs.size(), &sdf, 1u, &image, 1u));
    const Rect rectangle{ 2.0f, 3.0f, 16.0f, 8.0f };
    const Rect uv{ 0.0f, 0.0f, 1.0f, 1.0f };
    ASSERT_TRUE(m_builder.drawGlyph(glyphs[0u], rectangle, uv));
    ASSERT_TRUE(m_builder.drawSdfGlyph(sdf, 2u, rectangle, uv));
    ASSERT_TRUE(m_builder.drawImage(image, rectangle));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.glyphPages().size() + snapshot.sdfPages().size() + snapshot.textureImages().size(), s_PaintMaxImages);
    EXPECT_TRUE(GpuRendererState::ValidateGlyphPages(snapshot));
    EXPECT_TRUE(GpuRendererState::ValidateSdfPages(snapshot));
    EXPECT_TRUE(GpuRendererState::ValidateTextureImages(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, SharedImageBudgetOverflowIncludesEveryImageKind){
    FixedVector<SharedGlyphPage, s_PaintMaxImages - 2u> glyphs;
    for(usize index = 0u; index < glyphs.max_size(); ++index){
        auto glyphCreationResult = makeGlyph(100u + index);
        ASSERT_TRUE(glyphCreationResult);
        SharedGlyphPage glyph = Move(*glyphCreationResult);
        ASSERT_TRUE(glyph);
        glyphs.push_back(Move(glyph));
    }
    auto sdfCreationResult = makeSdf();
    ASSERT_TRUE(sdfCreationResult);
    const SharedSdfAtlasPage sdf = Move(*sdfCreationResult);
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    auto additionResult = makeImage("tests/ui/overflow_gpu_image", 73u);
    ASSERT_TRUE(additionResult);
    const SharedImageSource addition = Move(*additionResult);
    ASSERT_TRUE(sdf && image && addition);
    ASSERT_TRUE(m_builder.prepareImages(glyphs.data(), glyphs.size(), &sdf, 1u, &image, 1u));
    ASSERT_TRUE(m_builder.drawImage(image, { 2.0f, 3.0f, 16.0f, 8.0f }));
    DrawSnapshot snapshot = m_builder.freeze();
    const_cast<PaintVector<SharedImageSource>&>(snapshot.textureImages()).push_back(addition);
    ASSERT_EQ(snapshot.glyphPages().size(), s_PaintMaxImages - 2u);
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    ASSERT_EQ(snapshot.textureImages().size(), 2u);
    EXPECT_FALSE(GpuRendererState::ValidateGlyphPages(snapshot));
    EXPECT_FALSE(GpuRendererState::ValidateSdfPages(snapshot));
    EXPECT_FALSE(GpuRendererState::ValidateTextureImages(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, NonImageCommandsCannotReferenceTextureBindings){
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    auto glyphCreationResult = makeGlyph();
    ASSERT_TRUE(glyphCreationResult);
    const SharedGlyphPage glyph = Move(*glyphCreationResult);
    auto sdfCreationResult = makeSdf();
    ASSERT_TRUE(sdfCreationResult);
    const SharedSdfAtlasPage sdf = Move(*sdfCreationResult);
    ASSERT_TRUE(image && glyph && sdf);
    const Rect rectangle{ 2.0f, 3.0f, 16.0f, 8.0f };
    const Rect uv{ 0.0f, 0.0f, 1.0f, 1.0f };
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), rectangle));
    ASSERT_TRUE(m_builder.drawGlyph(glyph, rectangle, uv));
    ASSERT_TRUE(m_builder.drawSdfGlyph(sdf, 3u, rectangle, uv));
    ASSERT_TRUE(m_builder.drawImage(image, rectangle));
    DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 5u);
    ASSERT_TRUE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
    usize nonImageCount = 0u;
    auto& commands = const_cast<PaintVector<DrawCommand>&>(snapshot.commands());
    for(DrawCommand& command : commands){
        if(command.material == PaintMaterial::Image)
            continue;
        ++nonImageCount;
        command.textureImageIndex = 0u;
        EXPECT_FALSE(GpuRendererState::ValidateTextureImages(snapshot));
        EXPECT_FALSE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
        command.textureImageIndex = Limit<u32>::s_Max;
    }
    EXPECT_EQ(nonImageCount, 4u);
    EXPECT_TRUE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, ImageCommandRejectsGlyphAndSdfMetadata){
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    auto glyphCreationResult = makeGlyph();
    ASSERT_TRUE(glyphCreationResult);
    const SharedGlyphPage glyph = Move(*glyphCreationResult);
    auto sdfCreationResult = makeSdf();
    ASSERT_TRUE(sdfCreationResult);
    const SharedSdfAtlasPage sdf = Move(*sdfCreationResult);
    ASSERT_TRUE(image && glyph && sdf);
    ASSERT_TRUE(m_builder.prepareImages(&glyph, 1u, &sdf, 1u, &image, 1u));
    ASSERT_TRUE(m_builder.drawImage(image, { 2.0f, 3.0f, 16.0f, 8.0f }));
    DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 1u);
    DrawCommand& command = const_cast<DrawCommand&>(snapshot.commands()[0u]);
    const DrawCommand original = command;
    for(u32 contamination = 0u; contamination < 3u; ++contamination){
        command = original;
        if(contamination == 0u)
            command.glyphPageIndex = 0u;
        else if(contamination == 1u)
            command.sdfPageIndex = 0u;
        else
            command.sdfChannel = 1u;
        EXPECT_FALSE(GpuRendererState::ValidateTextureImages(snapshot));
        EXPECT_FALSE(GpuRendererState::ValidateGlyphPages(snapshot));
        EXPECT_FALSE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
    }
    command = original;
    EXPECT_TRUE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
}

TEST_F(GpuTextureImageValidationFixture, UnknownMaterialRejectsCombinedImageValidation){
    auto imageResult = makeImage();
    ASSERT_TRUE(imageResult);
    const SharedImageSource image = Move(*imageResult);
    ASSERT_TRUE(image);
    ASSERT_TRUE(m_builder.drawImage(image, { 2.0f, 3.0f, 16.0f, 8.0f }));
    DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 1u);
    DrawCommand& command = const_cast<DrawCommand&>(snapshot.commands()[0u]);
    command.material = static_cast<PaintMaterial::Enum>(Limit<u8>::s_Max);
    command.textureImageIndex = Limit<u32>::s_Max;
    EXPECT_FALSE(GpuRendererState::ValidateGlyphPages(snapshot));
    EXPECT_FALSE(__hidden_ui_gpu_texture_image_validation_tests::ValidImageBindings(snapshot));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

