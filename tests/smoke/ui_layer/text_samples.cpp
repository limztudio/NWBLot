// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_samples.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_smoke{
static constexpr StringView s_Korean = "\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9";
static constexpr u64 s_CoverageAtlasIdentity = 0x5549534D4F4B45u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Impl::Ui::SharedGlyphPage MakeCoveragePage(Core::Alloc::GlobalArena& arena){
    const Impl::Ui::GlyphPageBinding binding{
        .font = Core::Assets::AssetRef<Impl::Font>("engine/ui/fonts/default/latin"),
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextSmokeSamples::UiTextSmokeSamples(Core::Alloc::GlobalArena& arena)
    : m_latin(arena)
    , m_korean(arena)
    , m_clipped(arena)
    , m_coverage(__hidden_ui_text_smoke::MakeCoveragePage(arena))
{
    const bool configured = m_latin.setText({ .text = "office ffi e\xCC\x81", .fontSize = 22.0f })
        == Impl::Ui::TextLayoutStatus::Success
        && m_clipped.setText({ .text = "Clipped coverage label", .fontSize = 22.0f }) == Impl::Ui::TextLayoutStatus::Success
        && m_korean.setText({
            .text = __hidden_ui_text_smoke::s_Korean,
            .fontSize = 22.0f,
            .scriptTag = Impl::Ui::TextScriptTag('H', 'a', 'n', 'g'),
            .language = "ko"
        }) == Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured && m_coverage, NWB_TEXT("UI smoke text and coverage fixture must be valid"));
}

bool UiTextSmokeSamples::paint(Impl::UiPaintContext& context){
    Impl::Ui::PaintBuilder& paint = context.paint;
    const f32 width = context.display.logicalWidth;
    const f32 height = context.display.logicalHeight;
    if(
        !m_latin.paint(context.text, paint, { width * 0.04f, height * 0.80f })
        || !m_korean.paint(context.text, paint, { width * 0.04f, height * 0.875f })
    )
        return false;

    paint.pushClip({ width * 0.34f, height * 0.82f, width * 0.21f, height * 0.08f });
    const bool clippedPainted = m_clipped.paint(context.text, paint, { width * 0.34f, height * 0.82f });
    const bool restored = paint.popClip();
    if(!clippedPainted || !restored)
        return false;

    const Impl::Ui::Color tint{ 1.0f, 0.2f, 0.6f, 0.5f };
    for(u32 i = 0u; i < 3u; ++i){
        const Impl::Ui::Rect rectangle{ width * (0.64f + static_cast<f32>(i) * 0.08f), height * 0.84f, width * 0.05f, height * 0.06f };
        const Impl::Ui::Rect uv{ static_cast<f32>(i) / 3.0f, 0.0f, 1.0f / 3.0f, 1.0f };
        if(!paint.drawGlyph(m_coverage, rectangle, uv, tint))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedUiTextSmokeSamples CreateUiTextSmokeSamples(Core::Alloc::GlobalArena& arena){
    return SharedUiTextSmokeSamples(
        NewArenaObject<RefCounter<UiTextSmokeSamples>>(arena, arena),
        ArenaRefDeleter<RefCounter<UiTextSmokeSamples>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

