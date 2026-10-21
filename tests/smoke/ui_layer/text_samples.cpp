// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_samples.h"

#include "smoke_geometry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_LatinFontPath = "engine/ui/fonts/default/latin/face";
static constexpr StringView s_Korean = "\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9";
static constexpr u64 s_CoverageAtlasIdentity = 0x5549534D4F4B45u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Impl::Ui::SharedGlyphPage> MakeCoveragePage(Core::Alloc::GlobalArena& arena){
    const Impl::Ui::GlyphPageBinding binding{
        .font = Core::Assets::AssetRef<Impl::Font>(__hidden_ui_text_smoke::s_LatinFontPath),
        .fontGeneration = 1u,
        .atlasIdentity = s_CoverageAtlasIdentity,
        .generation = 1u,
        .index = static_cast<u32>(Impl::Ui::s_GlyphAtlasMaxPages - 1u),
        .width = 12u,
        .height = 4u
    };
    Impl::Ui::GlyphPage::Pixels pixels(arena);
    pixels.resize(48u);
    for(u32 y = 0u; y < binding.height; ++y){
        for(u32 x = 0u; x < binding.width; ++x)
            pixels[y * binding.width + x] = x < 4u ? 0u : (x < 8u ? 128u : 255u);
    }
    return Impl::Ui::CreateGlyphPage(arena, binding, Move(pixels));
}

[[nodiscard]] static Expected<Impl::Ui::SharedSdfAtlasPage> MakeSdfPage(Core::Alloc::GlobalArena& arena, const u32 channelCount){
    const Impl::Ui::SdfAtlasPageBinding binding{
        .font = Core::Assets::AssetRef<Impl::Font>(__hidden_ui_text_smoke::s_LatinFontPath),
        .fontSha256 = {},
        .pixelsSha256 = {},
        .fontGeneration = 1u,
        .atlasIdentity = s_CoverageAtlasIdentity,
        .generation = 1u,
        .index = channelCount - 1u,
        .width = 12u,
        .height = 4u,
        .channelCount = channelCount,
        .spreadPixels = 8u
    };
    Impl::Ui::SdfAtlasPage::Pixels pixels(arena);
    pixels.resize(12u * 4u * channelCount);
    for(u32 y = 0u; y < binding.height; ++y){
        for(u32 x = 0u; x < binding.width; ++x){
            const usize offset = (y * binding.width + x) * channelCount;
            for(u32 channel = 0u; channel < channelCount; ++channel)
                pixels[offset + channel] = channel == 3u ? (x < 4u ? 0u : (x < 8u ? 128u : 255u)) : (channel == 0u ? 0u : (channel == 1u ? 128u : 255u));
        }
    }
    return Impl::Ui::CreateSdfAtlasPage(arena, binding, Move(pixels));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextSmokeSamples::UiTextSmokeSamples(Core::Alloc::GlobalArena& arena)
    : m_latin(arena)
    , m_korean(arena)
    , m_clipped(arena)
{
    auto coverageResult = __hidden_ui_text_smoke::MakeCoveragePage(arena);
    if(coverageResult)
        m_coverage = Move(*coverageResult);
    for(u32 channels = 1u; channels <= 4u; ++channels){
        auto pageResult = __hidden_ui_text_smoke::MakeSdfPage(arena, channels);
        if(pageResult)
            m_sdf[channels - 1u] = Move(*pageResult);
    }
    const bool configured = m_latin.setText({ .text = "office ffi e\xCC\x81 gqyp", .fontSize = 22.0f }) == Impl::Ui::TextLayoutStatus::Success && m_clipped.setText({ .text = "Clipped coverage label", .fontSize = 22.0f }) == Impl::Ui::TextLayoutStatus::Success
        && m_korean.setText({ .text = __hidden_ui_text_smoke::s_Korean, .fontSize = 22.0f, .scriptTag = Impl::Ui::TextScriptTag('H', 'a', 'n', 'g'), .language = "ko" }) == Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured && m_coverage, NWB_TEXT("UI smoke text and coverage fixture must be valid"));
    for(const Impl::Ui::SharedSdfAtlasPage& page : m_sdf)
        NWB_FATAL_ASSERT_MSG(page, NWB_TEXT("UI smoke compact SDF fixtures must be valid"));
}

bool UiTextSmokeSamples::paint(Impl::UiPaintContext& context){
    Impl::Ui::PaintBuilder& paint = context.paint;
    // Lanes are width, height, width and height of the display.
    const SIMDVectorSingle extent = VectorSet(context.display.logicalWidth, context.display.logicalHeight, context.display.logicalWidth, context.display.logicalHeight);
    if(!m_latin.paint(context.text, paint, StoreSmokeRect(VectorMultiply(extent, VectorSet(0.04f, 0.80f, 0.28f, 0.075f)))) || !m_korean.paint(context.text, paint, StoreSmokeRect(VectorMultiply(extent, VectorSet(0.04f, 0.875f, 0.28f, 0.075f)))))
        return false;

    if(!m_clipped.paint(context.text, paint, StoreSmokeRect(VectorMultiply(extent, VectorSet(0.34f, 0.82f, 0.21f, 0.08f)))))
        return false;

    const Impl::Ui::Color tint{ 1.0f, 0.2f, 0.6f, 0.5f };
    for(u32 i = 0u; i < 3u; ++i){
        const SIMDVectorSingle factors = VectorMultiplyAddExpression(VectorSet(static_cast<f32>(i), 0.0f, 0.0f, 0.0f), VectorSet(0.08f, 0.0f, 0.0f, 0.0f), VectorSet(0.64f, 0.84f, 0.05f, 0.06f));
        const Impl::Ui::Rect rectangle = StoreSmokeRect(VectorMultiply(extent, factors));
        const Impl::Ui::Rect uv{ VectorGetX(VectorDivide(VectorReplicate(static_cast<f32>(i)), VectorReplicate(3.0f))), 0.0f, 1.0f / 3.0f, 1.0f };
        if(!paint.drawGlyph(m_coverage, rectangle, uv, tint))
            return false;
    }
    for(u32 channel = 0u; channel < 4u; ++channel){
        const SIMDVectorSingle factors = VectorMultiplyAddExpression(VectorSet(static_cast<f32>(channel), 0.0f, 0.0f, 0.0f), VectorSet(0.08f, 0.0f, 0.0f, 0.0f), VectorSet(0.64f, 0.94f, 0.05f, 0.045f));
        const Impl::Ui::Rect rectangle = StoreSmokeRect(VectorMultiply(extent, factors));
        if(!paint.drawSdfGlyph(m_sdf[channel], channel, rectangle, { 0.f, 0.f, 1.f, 1.f }, { 0.2f, 1.f, 0.4f, 0.5f }))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedUiTextSmokeSamples CreateUiTextSmokeSamples(Core::Alloc::GlobalArena& arena){
    return SharedUiTextSmokeSamples(NewArenaObject<RefCounter<UiTextSmokeSamples>>(arena, arena), ArenaRefDeleter<RefCounter<UiTextSmokeSamples>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

