// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font_atlas/asset.h>
#include <impl/ecs_ui/toolkit/text/service.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_quality_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

[[nodiscard]] static f32 SampleDistance(const FontAtlasPayload& payload, const FontAtlasGlyph& glyph, f32 pixelSize, f32 x, f32 y){
    const FontAtlasGroup& group = payload.groups[glyph.group];
    const f32 scale = pixelSize / static_cast<f32>(payload.bakePpem);
    const f32 left = glyph.planeLeft * pixelSize / payload.unitsPerEm;
    const f32 top = glyph.planeTop * pixelSize / payload.unitsPerEm;
    const f32 sampleX = static_cast<f32>(glyph.x) + (x - left) / scale - 0.5f;
    const f32 sampleY = static_cast<f32>(glyph.y) + (y - top) / scale - 0.5f;
    const i32 x0 = static_cast<i32>(Floor(sampleX));
    const i32 y0 = static_cast<i32>(Floor(sampleY));
    const f32 tx = sampleX - static_cast<f32>(x0);
    const f32 ty = sampleY - static_cast<f32>(y0);
    const auto fetch = [&](i32 sx, i32 sy){
        const u32 ix = static_cast<u32>(Clamp(sx, 0, static_cast<i32>(group.width) - 1));
        const u32 iy = static_cast<u32>(Clamp(sy, 0, static_cast<i32>(group.height) - 1));
        return static_cast<f32>(group.pixels[(static_cast<usize>(iy) * group.width + ix) * group.channelCount + glyph.channel]);
    };
    const f32 upper = fetch(x0, y0) * (1.f - tx) + fetch(x0 + 1, y0) * tx;
    const f32 lower = fetch(x0, y0 + 1) * (1.f - tx) + fetch(x0 + 1, y0 + 1) * tx;
    return ((upper * (1.f - ty) + lower * ty) - 128.f) * payload.spreadPixels / 128.f;
}

[[nodiscard]] static bool IsEdge(const PaintVector<u8>& mask, u32 width, u32 height, i32 x, i32 y){
    if(x <= 0 || y <= 0 || x >= static_cast<i32>(width) - 1 || y >= static_cast<i32>(height) - 1)
        return false;
    const usize index = static_cast<usize>(y) * width + static_cast<u32>(x);
    return
        mask[index] != 0u && (mask[index - 1u] == 0u || mask[index + 1u] == 0u
        || mask[index - width] == 0u || mask[index + width] == 0u)
    ;
}

[[nodiscard]] static f32 EdgeDisplacement(const PaintVector<u8>& from, const PaintVector<u8>& to, u32 width, u32 height){
    f32 maximum = 0.f;
    for(i32 y = 1; y < static_cast<i32>(height) - 1; ++y){
        for(i32 x = 1; x < static_cast<i32>(width) - 1; ++x){
            if(!IsEdge(from, width, height, x, y))
                continue;
            i32 nearestSquared = 32;
            for(i32 dy = -4; dy <= 4; ++dy){
                for(i32 dx = -4; dx <= 4; ++dx){
                    if(IsEdge(to, width, height, x + dx, y + dy))
                        nearestSquared = Min(nearestSquared, dx * dx + dy * dy);
                }
            }
            maximum = Max(maximum, Sqrt(static_cast<f32>(nearestSquared)));
        }
    }
    return maximum;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FontAtlasQuality, DefaultFieldsTrackSupersampledNativeOutlinesAcrossZoomAndDpi){
    Core::Alloc::GlobalArena arena(Name("tests/ui/font_atlas/quality"));
    const NWB::Path assetRoot(arena, NWB_REPO_ROOT "/impl/assets");
    const NWB::Path directory = assetRoot / "ui/fonts/default";
    static constexpr StringView s_Atlases[]{ "latin.atlas", "korean.atlas" };
    static constexpr StringView s_Sources[]{ "latin.font", "korean.font" };
    static constexpr StringView s_Identities[]{ "engine/ui/fonts/default/latin/face", "engine/ui/fonts/default/korean/face" };
    static constexpr StringView s_AtlasIdentities[]{ "engine/ui/fonts/default/latin/atlas", "engine/ui/fonts/default/korean/atlas" };
    static constexpr StringView s_Text[]{ "Aoegq", "\xED\x95\x9C" };
    static constexpr f32 s_Zoom[]{ 0.5f, 0.75f, 1.f, 1.5f, 2.f, 4.f };
    static constexpr f32 s_Dpi[]{ 1.f, 1.5f, 2.f };
    u32 comparisons = 0u;
    f32 worstEdge = 0.f;
    f32 worstMean = 0.f;
    for(usize fixture = 0u; fixture < LengthOf(s_Atlases); ++fixture){
        Font font(arena, Name(s_Identities[fixture]));
        Core::Assets::AssetBytes source(arena);
        ASSERT_TRUE(Tests::ReadBundledFontBytes(directory / s_Sources[fixture], source));
        font.setFontBytes(Move(source));
        ASSERT_TRUE(font.validatePayload());
        Core::Assets::AssetBytes atlasBinary(arena);
        ErrorCode error;
        ASSERT_TRUE(ReadBinaryFile(directory / s_Atlases[fixture], atlasBinary, error));
        FontAtlasPayload decoded(arena);
        ASSERT_TRUE(DeserializeFontAtlasPayload(atlasBinary, decoded));
        decoded.font = Core::Assets::AssetRef<Font>(s_Identities[fixture].data());
        FontAtlas atlas(arena, Name(s_AtlasIdentities[fixture]));
        atlas.setPayload(Move(decoded));
        ASSERT_TRUE(atlas.validatePayload());
        const FontSource fontSource{ Core::Assets::AssetRef<Font>(s_Identities[fixture].data()), font, 1u, &atlas };
        TextService service(arena);
        ASSERT_TRUE(service.setFonts(&fontSource, 1u));
        const FontAtlasPayload& payload = atlas.payload();
        for(const f32 zoom : s_Zoom){
            for(const f32 dpi : s_Dpi){
                const u32 pixels = static_cast<u32>(Ceil(payload.bakePpem * zoom * dpi));
                if(
                    static_cast<f32>(pixels) < payload.bakePpem * s_BakedFontAtlasMinScale
                    || static_cast<f32>(pixels) > payload.bakePpem * s_BakedFontAtlasMaxScale
                )
                    continue;
                ShapeRequest request{ .text = s_Text[fixture], .fontSize = static_cast<f32>(pixels) };
                if(fixture == 1u){
                    request.scriptTag = TextScriptTag('H', 'a', 'n', 'g');
                    request.language = "ko";
                }
                TextLayout layout(arena);
                ASSERT_EQ(service.layout(request, layout), TextLayoutStatus::Success);
                for(const PlacedGlyph& placed : layout.glyphs()){
                    SCOPED_TRACE(fixture);
                    SCOPED_TRACE(placed.glyphId);
                    SCOPED_TRACE(pixels);
                    ASSERT_TRUE(placed.face->bakedAtlas());
                    const FontAtlasGlyph& glyph = payload.glyphs[placed.glyphId];
                    ASSERT_EQ(glyph.drawable, 1u);
                    GlyphBitmap reference(arena);
                    ASSERT_TRUE(placed.face->rasterize(placed.glyphId, pixels * 4u, reference));
                    const i32 left = static_cast<i32>(Floor(glyph.planeLeft * pixels / payload.unitsPerEm)) - 2;
                    const i32 top = static_cast<i32>(Floor(glyph.planeTop * pixels / payload.unitsPerEm)) - 2;
                    const u32 width = static_cast<u32>(Ceil((glyph.planeRight - glyph.planeLeft) * pixels / payload.unitsPerEm)) + 5u;
                    const u32 height = static_cast<u32>(Ceil((glyph.planeBottom - glyph.planeTop) * pixels / payload.unitsPerEm)) + 5u;
                    PaintVector<u8> nativeMask(arena), sdfMask(arena);
                    nativeMask.resize(static_cast<usize>(width) * height);
                    sdfMask.resize(nativeMask.size());
                    f32 absoluteError = 0.f;
                    for(u32 y = 0u; y < height; ++y){
                        for(u32 x = 0u; x < width; ++x){
                            const i32 px = left + static_cast<i32>(x);
                            const i32 py = top + static_cast<i32>(y);
                            u32 sum = 0u;
                            for(i32 sy = 0; sy < 4; ++sy){
                                for(i32 sx = 0; sx < 4; ++sx){
                                    const i32 rx = px * 4 + sx - reference.bearingX;
                                    const i32 ry = py * 4 + sy + reference.bearingY;
                                    if(rx >= 0 && ry >= 0 && rx < static_cast<i32>(reference.width) && ry < static_cast<i32>(reference.height))
                                        sum += reference.pixels[static_cast<usize>(ry) * reference.width + static_cast<u32>(rx)];
                                }
                            }
                            const f32 native = static_cast<f32>(sum) / (16.f * 255.f);
                            const f32 cx = static_cast<f32>(px) + 0.5f;
                            const f32 cy = static_cast<f32>(py) + 0.5f;
                            const f32 distance = SampleDistance(payload, glyph, static_cast<f32>(pixels), cx, cy);
                            const f32 derivativeX = Abs(SampleDistance(payload, glyph, pixels, cx + 0.5f, cy)
                                - SampleDistance(payload, glyph, pixels, cx - 0.5f, cy));
                            const f32 derivativeY = Abs(SampleDistance(payload, glyph, pixels, cx, cy + 0.5f)
                                - SampleDistance(payload, glyph, pixels, cx, cy - 0.5f));
                            const f32 sdf = Clamp(0.5f + distance / Max(derivativeX + derivativeY, 0.001f), 0.f, 1.f);
                            const usize index = static_cast<usize>(y) * width + x;
                            nativeMask[index] = native >= 0.5f ? 1u : 0u;
                            sdfMask[index] = sdf >= 0.5f ? 1u : 0u;
                            absoluteError += Abs(native - sdf);
                        }
                    }
                    const f32 displacement = Max(EdgeDisplacement(nativeMask, sdfMask, width, height),
                        EdgeDisplacement(sdfMask, nativeMask, width, height));
                    const f32 mean = absoluteError / static_cast<f32>(nativeMask.size());
                    worstEdge = Max(worstEdge, displacement);
                    worstMean = Max(worstMean, mean);
                    EXPECT_LE(displacement, 1.f);
                    EXPECT_LE(mean, 0.04f);
                    ++comparisons;
                }
            }
        }
    }
    EXPECT_GE(comparisons, 48u);
    RecordProperty("comparisons", comparisons);
    RecordProperty("maximum_edge_displacement_pixels", StringFormat(arena, "{}", worstEdge).c_str());
    RecordProperty("maximum_mean_coverage_error", StringFormat(arena, "{}", worstMean).c_str());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

