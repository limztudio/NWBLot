// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_image_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintImageTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(PaintImageFixture, CropAndNestedClipTrimFullImageGeometryAndUv){
    const SharedImageSource image = makeImage();
    ASSERT_TRUE(image);
    m_builder.pushClip({ 10.0f, 8.0f, 60.0f, 35.0f });
    m_builder.pushClip({ 20.0f, 10.0f, 25.0f, 20.0f });
    ASSERT_TRUE(m_builder.drawImage(
        image, { 0.0f, 0.0f, 80.0f, 40.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }, { 2.0f, 0.5f, 0.25f, 0.5f }
    ));
    ASSERT_TRUE(m_builder.popClip());
    ASSERT_TRUE(m_builder.popClip());
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, 20.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.y, 10.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.x, 45.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.y, 30.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].texCoord.x, 0.25f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].texCoord.y, 0.375f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].texCoord.x, 0.40625f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].texCoord.y, 0.625f);
}

TEST_F(PaintImageFixture, AdjacentBatchingIncludesImmutableImageIndexAndPreservesDeclarationOrder){
    const SharedImageSource first = makeImage("tests/ui/same_image", 37u);
    const SharedImageSource second = makeImage("tests/ui/same_image", 73u);
    ASSERT_TRUE(first && second);
    ASSERT_NE(first->generation(), second->generation());
    const Rect rectangle{ 1.0f, 1.0f, 8.0f, 4.0f };
    ASSERT_TRUE(m_builder.drawImage(first, rectangle));
    ASSERT_TRUE(m_builder.drawImage(first, rectangle, { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 0.5f }));
    ASSERT_TRUE(m_builder.drawImage(second, rectangle));
    ASSERT_TRUE(m_builder.drawImage(second, rectangle));
    ASSERT_TRUE(m_builder.drawImage(first, rectangle));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.textureImages().size(), 2u);
    ASSERT_EQ(snapshot.commands().size(), 3u);
    EXPECT_EQ(snapshot.commands()[0u].firstIndex, 0u);
    EXPECT_EQ(snapshot.commands()[0u].indexCount, 12u);
    EXPECT_EQ(snapshot.commands()[0u].textureImageIndex, 0u);
    EXPECT_EQ(snapshot.commands()[1u].firstIndex, 12u);
    EXPECT_EQ(snapshot.commands()[1u].indexCount, 12u);
    EXPECT_EQ(snapshot.commands()[1u].textureImageIndex, 1u);
    EXPECT_EQ(snapshot.commands()[2u].firstIndex, 24u);
    EXPECT_EQ(snapshot.commands()[2u].indexCount, 6u);
    EXPECT_EQ(snapshot.commands()[2u].textureImageIndex, 0u);
}

TEST_F(PaintImageFixture, ImageCommandsKeepDistinctClipsMaterialsAndOverlayLayers){
    const SharedImageSource image = makeImage();
    const SharedGlyphPage glyph = makeGlyph();
    const SharedSdfAtlasPage sdf = makeSdf();
    ASSERT_TRUE(image && glyph && sdf);
    const Rect rectangle{ 0.0f, 0.0f, 20.0f, 20.0f };
    ASSERT_TRUE(m_builder.drawImage(image, rectangle));
    ASSERT_TRUE(m_builder.drawGlyph(glyph, rectangle, { 0.0f, 0.0f, 1.0f, 1.0f }));
    ASSERT_TRUE(m_builder.drawSdfGlyph(sdf, 3u, rectangle, { 0.0f, 0.0f, 1.0f, 1.0f }));
    m_builder.pushClip({ 5.0f, 5.0f, 8.0f, 8.0f });
    ASSERT_TRUE(m_builder.drawImage(image, rectangle));
    ASSERT_TRUE(m_builder.beginOverlay(2u));
    ASSERT_TRUE(m_builder.drawImage(image, rectangle));
    ASSERT_TRUE(m_builder.endOverlay());
    ASSERT_TRUE(m_builder.drawImage(image, rectangle));
    ASSERT_TRUE(m_builder.popClip());
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 6u);
    EXPECT_EQ(snapshot.commands()[0u].material, PaintMaterial::Image);
    EXPECT_EQ(snapshot.commands()[1u].material, PaintMaterial::Glyph);
    EXPECT_EQ(snapshot.commands()[2u].material, PaintMaterial::SdfGlyph);
    EXPECT_EQ(snapshot.commands()[1u].textureImageIndex, Limit<u32>::s_Max);
    EXPECT_EQ(snapshot.commands()[2u].textureImageIndex, Limit<u32>::s_Max);
    EXPECT_EQ(snapshot.commands()[3u].firstIndex, 18u);
    EXPECT_EQ(snapshot.commands()[4u].firstIndex, 30u);
    EXPECT_EQ(snapshot.commands()[5u].firstIndex, 24u);
    EXPECT_EQ(snapshot.commands()[5u].layer, 2u);
    EXPECT_FLOAT_EQ(snapshot.commands()[3u].clip.x, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[4u].clip.width, 8.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[5u].clip.width, 200.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[12u].position.x, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[16u].position.x, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[20u].position.x, 5.0f);
}

TEST_F(PaintImageFixture, FrozenAndMovedSnapshotsRetainExactSourceVersionsAcrossHandleReleaseAndBuilderReuse){
    SharedImageSource original = makeImage("tests/ui/replacement", 37u, 9u, 5u);
    ASSERT_TRUE(original);
    const u64 originalGeneration = original->generation();
    const u8 originalByte = original->texture().payloadBytes()[0u];
    ASSERT_TRUE(m_builder.drawImage(original, { 1.0f, 2.0f, 18.0f, 10.0f }));
    DrawSnapshot frozen = m_builder.freeze();
    DrawSnapshot retained(Move(frozen));
    original.reset();
    beginPaint(m_builder, 2u);
    SharedImageSource replacement = makeImage("tests/ui/replacement", 73u, 4u, 4u);
    ASSERT_TRUE(replacement);
    ASSERT_NE(replacement->generation(), originalGeneration);
    ASSERT_TRUE(m_builder.drawImage(replacement, { 4.0f, 5.0f, 8.0f, 8.0f }));
    const DrawSnapshot next = m_builder.freeze();
    replacement.reset();
    ASSERT_EQ(retained.textureImages().size(), 1u);
    ASSERT_EQ(next.textureImages().size(), 1u);
    EXPECT_EQ(retained.textureImages()[0u]->generation(), originalGeneration);
    EXPECT_EQ(retained.textureImages()[0u]->texture().width(), 9u);
    EXPECT_EQ(retained.textureImages()[0u]->texture().height(), 5u);
    EXPECT_EQ(retained.textureImages()[0u]->texture().mipLevels().size(), 4u);
    EXPECT_EQ(retained.textureImages()[0u]->texture().payloadBytes()[0u], originalByte);
    const u8 replacementByte = next.textureImages()[0u]->texture().payloadBytes()[0u];
    EXPECT_NE(retained.textureImages()[0u]->texture().payloadBytes()[0u], replacementByte);
    EXPECT_NE(retained.textureImages()[0u].get(), next.textureImages()[0u].get());
    EXPECT_FLOAT_EQ(retained.vertices()[0u].position.x, 1.0f);
    EXPECT_FLOAT_EQ(retained.vertices()[2u].texCoord.x, 1.0f);
}

TEST_F(PaintImageFixture, InvalidRectangleUvTintAndNullSourcePreserveEarlierBindingsAndGeometry){
    const SharedImageSource image = makeImage();
    ASSERT_TRUE(image);
    PaintBuilder reference(m_arena);
    beginPaint(reference);
    const Rect rectangle{ 1.0f, 2.0f, 10.0f, 8.0f };
    ASSERT_TRUE(m_builder.drawImage(image, rectangle));
    ASSERT_TRUE(reference.drawImage(image, rectangle));
    const Rect invalidRectangles[]{
        { Limit<f32>::s_QuietNaN, 0.0f, 1.0f, 1.0f },
        { 0.0f, Limit<f32>::s_Infinity, 1.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f, -1.0f },
        { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f },
        { 0.0f, 0.0f, 1.0f, Limit<f32>::s_QuietNaN },
    };
    for(const Rect& invalid : invalidRectangles)
        EXPECT_FALSE(m_builder.drawImage(image, invalid));
    const Rect invalidUvs[]{
        { -0.1f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f, -0.1f },
        { 0.75f, 0.0f, 0.5f, 1.0f },
        { 0.0f, 0.75f, 1.0f, 0.5f },
        { 0.0f, 0.0f, Limit<f32>::s_Infinity, 1.0f },
        { 0.0f, Limit<f32>::s_QuietNaN, 1.0f, 1.0f },
    };
    for(const Rect& invalid : invalidUvs)
        EXPECT_FALSE(m_builder.drawImage(image, rectangle, invalid));
    const Color invalidTints[]{
        { Limit<f32>::s_QuietNaN, 1.0f, 1.0f, 1.0f },
        { 1.0f, Limit<f32>::s_Infinity, 1.0f, 1.0f },
        { 1.0f, 1.0f, Limit<f32>::s_Infinity, 1.0f },
        { 1.0f, 1.0f, 1.0f, -0.1f },
        { 1.0f, 1.0f, 1.0f, 1.1f },
        { 1.0f, 1.0f, 1.0f, Limit<f32>::s_QuietNaN },
    };
    for(const Color& invalid : invalidTints)
        EXPECT_FALSE(m_builder.drawImage(image, rectangle, { 0.0f, 0.0f, 1.0f, 1.0f }, invalid));
    EXPECT_FALSE(m_builder.drawImage({}, rectangle));
    const DrawSnapshot actual = m_builder.freeze();
    const DrawSnapshot expected = reference.freeze();
    ExpectSameImagePaint(actual, expected);
}

TEST_F(PaintImageFixture, InvisibleAndTransparentDrawsRemainValidAtCapacityWithoutAdmittingTheSource){
    const SharedImageSource image = makeImage();
    ASSERT_TRUE(image);
    FixedVector<SharedGlyphPage, s_PaintMaxImages> pages;
    for(usize index = 0u; index < pages.max_size(); ++index){
        SharedGlyphPage page = makeGlyph(100u + index);
        ASSERT_TRUE(page);
        pages.push_back(Move(page));
    }
    PaintBuilder reference(m_arena);
    beginPaint(reference);
    ASSERT_TRUE(m_builder.prepareGlyphPages(pages.data(), pages.size()));
    ASSERT_TRUE(reference.prepareGlyphPages(pages.data(), pages.size()));
    m_builder.fillRect({ 1.0f, 1.0f, 4.0f, 4.0f });
    reference.fillRect({ 1.0f, 1.0f, 4.0f, 4.0f });
    ASSERT_TRUE(m_builder.drawImage(image, { 200.0f, 0.0f, 1.0f, 1.0f }));
    ASSERT_TRUE(m_builder.drawImage(image, { 0.0f, 0.0f, 0.0f, 1.0f }));
    ASSERT_TRUE(m_builder.drawImage(image, { 0.0f, 0.0f, 1.0f, 0.0f }));
    ASSERT_TRUE(m_builder.drawImage(image, { 1.0f, 1.0f, BitCast<f32>(u32{ 1u }), 1.0f }));
    ASSERT_TRUE(m_builder.drawImage(
        image, { 1.0f, 1.0f, 5.0f, 5.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f }
    ));
    m_builder.pushClip({ 250.0f, 0.0f, 1.0f, 1.0f });
    ASSERT_TRUE(m_builder.drawImage(image, { 0.0f, 0.0f, 20.0f, 20.0f }));
    EXPECT_FALSE(m_builder.drawImage({}, { 0.0f, 0.0f, 20.0f, 20.0f }));
    EXPECT_FALSE(m_builder.drawImage(image, { 0.0f, 0.0f, -1.0f, 1.0f }));
    ASSERT_TRUE(m_builder.popClip());
    EXPECT_FALSE(m_builder.drawImage(image, { 1.0f, 1.0f, 4.0f, 4.0f }));
    const DrawSnapshot actual = m_builder.freeze();
    const DrawSnapshot expected = reference.freeze();
    ExpectSameImagePaint(actual, expected);
    EXPECT_TRUE(actual.textureImages().empty());
    EXPECT_EQ(actual.glyphPages().size(), s_PaintMaxImages);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

