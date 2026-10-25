// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiLayerSystem::loadFonts(Core::Alloc::ScratchArena& scratchArena){
    if(m_fontsReady)
        return true;
    const UiSubsystem* const uiSubsystem = m_activeSceneSubsystems ? m_activeSceneSubsystems->findSubsystem<UiSubsystem>() : nullptr;
    if(!uiSubsystem)
        return false;
    const UiFontBinding defaultFonts[] = {
        { uiSubsystem->defaultLatinFace, uiSubsystem->defaultLatinAtlas },
        { uiSubsystem->defaultKoreanFace, uiSubsystem->defaultKoreanAtlas },
    };
    const Span<const UiFontBinding> requestedFonts = m_fontRefs.empty() ? Span<const UiFontBinding>(defaultFonts) : Span<const UiFontBinding>(m_fontRefs);
    if(requestedFonts.empty() || requestedFonts.size() > Ui::s_TextMaxFontSources){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: the font stack requires between one and eight font assets"));
        return false;
    }
    Array<UniquePtr<Font>, Ui::s_TextMaxFontSources> assets;
    Array<UniquePtr<FontAtlas>, Ui::s_TextMaxFontSources> atlasAssets;
    Vector<Ui::FontSource, Core::Alloc::ScratchArena> sources(scratchArena);
    sources.reserve(requestedFonts.size());
    for(usize index = 0u; index < requestedFonts.size(); ++index){
        const Core::Assets::AssetRef<Font> resolvedFontRef = resolveFont(requestedFonts[index].font);
        auto font = m_assetManager.loadTypedSync<Font>(resolvedFontRef.name(), s_UiLayerSystemOwnerName, "UI font");
        if(!font)
            return false;
        assets[index] = Move(*font);
        const FontAtlas* atlas = nullptr;
        if(requestedFonts[index].atlas.valid()){
            const Core::Assets::AssetRef<FontAtlas> resolvedAtlasRef = resolveFontAtlas(requestedFonts[index].atlas);
            auto loadedAtlas = m_assetManager.loadTypedSync<FontAtlas>(resolvedAtlasRef.name(), s_UiLayerSystemOwnerName, "UI font atlas");
            if(!loadedAtlas)
                NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: optional font atlas unavailable; using native coverage"));
            else{
                atlasAssets[index] = Move(*loadedAtlas);
                atlas = atlasAssets[index].get();
            }
        }
        sources.push_back({ resolvedFontRef, *assets[index], 1u, atlas });
    }
    // TextService owns the exact font bytes and immutable baked atlas versions; loaded asset objects can be released here.
    m_fontsReady = m_text.setFonts(scratchArena, sources.data(), sources.size());
    return m_fontsReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

