// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_image_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint_image_admission_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPaintImageTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(PaintImageFixture, ThreeKindsShareSixtyFourSlotsWhileCopiesDeduplicateAndOverflowIsAtomic){
    FixedVector<SharedGlyphPage, s_PaintMaxImages - 2u> glyphs;
    for(usize index = 0u; index < glyphs.max_size(); ++index){
        SharedGlyphPage page = makeGlyph(100u + index);
        ASSERT_TRUE(page);
        glyphs.push_back(Move(page));
    }
    const SharedSdfAtlasPage sdf = makeSdf();
    const SharedImageSource image = makeImage();
    const SharedImageSource overflow = makeImage("tests/ui/image", 81u);
    ASSERT_TRUE(sdf && image && overflow);
    PaintBuilder reference(m_arena);
    beginPaint(reference);
    const Array<PaintBuilder*, 2u> builders{ &m_builder, &reference };
    for(PaintBuilder* builder : builders){
        ASSERT_TRUE(builder->prepareImages(glyphs.data(), glyphs.size(), &sdf, 1u, &image, 1u));
        builder->fillRect({ 2.0f, 3.0f, 4.0f, 5.0f }, { 0.8f, 0.4f, 0.2f, 0.5f });
        ASSERT_TRUE(builder->drawGlyph(glyphs[0u], { 12.0f, 3.0f, 4.0f, 5.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }));
        ASSERT_TRUE(builder->drawSdfGlyph(sdf, 3u, { 22.0f, 3.0f, 4.0f, 5.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
        ASSERT_TRUE(builder->drawImage(image, { 32.0f, 3.0f, 8.0f, 4.0f }));
    }
    const DrawSnapshot expected = reference.freeze();
    const SharedImageSource imageCopy = image;
    const Array<SharedGlyphPage, 3u> glyphCopies{ glyphs[0u], glyphs[1u], glyphs[0u] };
    const Array<SharedSdfAtlasPage, 2u> sdfCopies{ sdf, sdf };
    const Array<SharedImageSource, 3u> imageCopies{ image, imageCopy, image };
    ASSERT_TRUE(m_builder.prepareImages(
        glyphCopies.data(), glyphCopies.size(), sdfCopies.data(), sdfCopies.size(), imageCopies.data(), imageCopies.size()
    ));
    EXPECT_FALSE(m_builder.prepareTextureImages(&overflow, 1u));
    EXPECT_FALSE(m_builder.drawImage(overflow, { 48.0f, 3.0f, 8.0f, 4.0f }));
    const DrawSnapshot actual = m_builder.freeze();
    ASSERT_EQ(actual.glyphPages().size(), 62u);
    ASSERT_EQ(actual.sdfPages().size(), 1u);
    ASSERT_EQ(actual.textureImages().size(), 1u);
    EXPECT_EQ(actual.glyphPages().size() + actual.sdfPages().size() + actual.textureImages().size(), 64u);
    ExpectSameImagePaint(actual, expected);
}

TEST_F(PaintImageFixture, MixedOverflowPreservesCoverageGenerationSdfImageVersionsAndAllPriorGeometry){
    FixedVector<SharedGlyphPage, s_PaintMaxImages - 2u> glyphs;
    for(usize index = 0u; index < glyphs.max_size(); ++index){
        SharedGlyphPage page = makeGlyph(100u + index);
        ASSERT_TRUE(page);
        glyphs.push_back(Move(page));
    }
    const SharedSdfAtlasPage sdf = makeSdf();
    const SharedImageSource image = makeImage();
    const SharedGlyphPage upgrade = makeGlyph(100u, 2u);
    const SharedSdfAtlasPage newSdf = makeSdf(99u);
    const SharedImageSource newImage = makeImage("tests/ui/image", 81u);
    ASSERT_TRUE(sdf && image && upgrade && newSdf && newImage);
    PaintBuilder reference(m_arena);
    beginPaint(reference);
    const Array<PaintBuilder*, 2u> builders{ &m_builder, &reference };
    for(PaintBuilder* builder : builders){
        ASSERT_TRUE(builder->prepareImages(glyphs.data(), glyphs.size(), &sdf, 1u, &image, 1u));
        builder->fillRect({ 2.0f, 3.0f, 5.0f, 7.0f }, { 0.6f, 0.3f, 0.2f, 0.5f });
        ASSERT_TRUE(builder->drawGlyph(glyphs[0u], { 12.0f, 13.0f, 5.0f, 7.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }));
        ASSERT_TRUE(builder->beginOverlay(2u));
        builder->pushClip({ 30.0f, 10.0f, 40.0f, 20.0f });
        ASSERT_TRUE(builder->drawSdfGlyph(sdf, 2u, { 32.0f, 12.0f, 8.0f, 8.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
        ASSERT_TRUE(builder->drawImage(image, { 44.0f, 12.0f, 12.0f, 8.0f }, { 0.25f, 0.25f, 0.5f, 0.5f }));
        ASSERT_TRUE(builder->popClip());
        ASSERT_TRUE(builder->endOverlay());
    }
    const DrawSnapshot expected = reference.freeze();
    EXPECT_FALSE(m_builder.prepareImages(&upgrade, 1u, &newSdf, 1u, &newImage, 1u));
    const DrawSnapshot actual = m_builder.freeze();
    ExpectSameImagePaint(actual, expected);
    ASSERT_EQ(actual.glyphPages().size(), 62u);
    EXPECT_EQ(actual.glyphPages()[0u].get(), glyphs[0u].get());
    EXPECT_EQ(actual.glyphPages()[0u]->binding().generation, 1u);
    EXPECT_EQ(actual.glyphPages()[0u]->pixels()[32u], 0u);
    ASSERT_EQ(actual.sdfPages().size(), 1u);
    EXPECT_EQ(actual.sdfPages()[0u].get(), sdf.get());
    ASSERT_EQ(actual.textureImages().size(), 1u);
    EXPECT_EQ(actual.textureImages()[0u].get(), image.get());
    EXPECT_EQ(actual.textureImages()[0u]->generation(), image->generation());
    EXPECT_EQ(actual.textureImages()[0u]->texture().payloadBytes()[0u], 37u);
}

TEST_F(PaintImageFixture, NullThirdKindRejectsEarlierCandidatesAtomicallyAndAValidRetryStillSucceeds){
    const SharedGlyphPage glyph = makeGlyph();
    const SharedGlyphPage upgrade = makeGlyph(11u, 2u);
    const SharedSdfAtlasPage sdf = makeSdf();
    const SharedImageSource image = makeImage();
    const SharedImageSource addition = makeImage("tests/ui/added_image", 81u);
    ASSERT_TRUE(glyph && upgrade && sdf && image && addition);
    const Array<SharedImageSource, 2u> invalidImages{ addition, SharedImageSource{} };
    PaintBuilder reference(m_arena);
    const Array<PaintBuilder*, 2u> builders{ &m_builder, &reference };
    for(u64 pass = 0u; pass < 2u; ++pass){
        beginPaint(m_builder, pass + 1u);
        beginPaint(reference, pass + 1u);
        for(PaintBuilder* builder : builders){
            ASSERT_TRUE(builder->prepareImages(&glyph, 1u, nullptr, 0u, &image, 1u));
            ASSERT_TRUE(builder->drawGlyph(glyph, { 2.0f, 3.0f, 4.0f, 5.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }));
            ASSERT_TRUE(builder->drawImage(image, { 12.0f, 3.0f, 8.0f, 4.0f }));
        }
        EXPECT_FALSE(m_builder.prepareImages(&upgrade, 1u, &sdf, 1u, invalidImages.data(), invalidImages.size()));
        // The first pass freezes rejection; the second retries in the same recording before either builder freezes.
        if(pass != 0u){
            for(PaintBuilder* builder : builders){
                ASSERT_TRUE(builder->prepareImages(&upgrade, 1u, &sdf, 1u, &addition, 1u));
                ASSERT_TRUE(builder->drawSdfGlyph(sdf, 1u, { 22.0f, 3.0f, 4.0f, 5.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
                ASSERT_TRUE(builder->drawImage(addition, { 32.0f, 3.0f, 8.0f, 4.0f }));
            }
        }
        const DrawSnapshot expected = reference.freeze();
        const DrawSnapshot actual = m_builder.freeze();
        ExpectSameImagePaint(actual, expected);
        ASSERT_EQ(actual.glyphPages().size(), 1u);
        EXPECT_EQ(actual.glyphPages()[0u].get(), pass == 0u ? glyph.get() : upgrade.get());
        EXPECT_EQ(actual.glyphPages()[0u]->binding().generation, pass == 0u ? 1u : 2u);
        EXPECT_EQ(actual.sdfPages().size(), pass == 0u ? 0u : 1u);
        EXPECT_EQ(actual.textureImages().size(), pass == 0u ? 1u : 2u);
    }
}

TEST_F(PaintImageFixture, SamePathFreshVersionsCoexistWhileCopiedHandlesUseOneSlotPerVersion){
    const SharedGlyphPage glyph = makeGlyph();
    const SharedSdfAtlasPage sdf = makeSdf();
    SharedImageSource first = makeImage("tests/ui/versioned_image", 37u);
    SharedImageSource second = makeImage("tests/ui/versioned_image", 81u);
    ASSERT_TRUE(glyph && sdf && first && second);
    ASSERT_EQ(first->identity(), second->identity());
    ASSERT_NE(first->generation(), second->generation());
    ASSERT_NE(first.get(), second.get());
    SharedImageSource copy = first;
    const u64 firstGeneration = first->generation();
    const u64 secondGeneration = second->generation();
    const auto* firstPointer = first.get();
    const auto* secondPointer = second.get();
    Array<SharedImageSource, 4u> candidates{ first, copy, second, first };
    ASSERT_TRUE(m_builder.prepareImages(&glyph, 1u, &sdf, 1u, candidates.data(), candidates.size()));
    ASSERT_TRUE(m_builder.prepareTextureImages(&copy, 1u));
    for(SharedImageSource& candidate : candidates)
        candidate.reset();
    first.reset();
    second.reset();
    copy.reset();
    const DrawSnapshot actual = m_builder.freeze();
    ASSERT_EQ(actual.glyphPages().size(), 1u);
    ASSERT_EQ(actual.sdfPages().size(), 1u);
    ASSERT_EQ(actual.textureImages().size(), 2u);
    EXPECT_EQ(actual.glyphPages()[0u].get(), glyph.get());
    EXPECT_EQ(actual.sdfPages()[0u].get(), sdf.get());
    EXPECT_EQ(actual.textureImages()[0u].get(), firstPointer);
    EXPECT_EQ(actual.textureImages()[1u].get(), secondPointer);
    EXPECT_EQ(actual.textureImages()[0u]->generation(), firstGeneration);
    EXPECT_EQ(actual.textureImages()[1u]->generation(), secondGeneration);
    EXPECT_EQ(actual.textureImages()[0u]->identity(), actual.textureImages()[1u]->identity());
    EXPECT_EQ(actual.textureImages()[0u]->texture().payloadBytes()[0u], 37u);
    EXPECT_EQ(actual.textureImages()[1u]->texture().payloadBytes()[0u], 81u);
    EXPECT_TRUE(actual.vertices().empty());
    EXPECT_TRUE(actual.indices().empty());
    EXPECT_TRUE(actual.commands().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

