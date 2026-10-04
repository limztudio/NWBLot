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
    if(m_fontRefs.empty() || m_fontRefs.size() > 8u){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("UiLayerSystem: the font stack requires between one and eight font assets"));
        return false;
    }
    Array<UniquePtr<Core::Assets::IAsset>, 8u> assets;
    Array<UniquePtr<Core::Assets::IAsset>, 8u> atlasAssets;
    Vector<Ui::FontSource, Core::Alloc::ScratchArena> sources(scratchArena);
    sources.reserve(m_fontRefs.size());
    for(usize index = 0u; index < m_fontRefs.size(); ++index){
        const Font* font = m_assetManager.loadTypedSync<Font>(
            m_fontRefs[index].font.name(), assets[index], GLOBAL_TEXT("UiLayerSystem"), "UI font"
        );
        if(!font)
            return false;
        const FontAtlas* atlas = nullptr;
        if(m_fontRefs[index].atlas.valid()){
            atlas = m_assetManager.loadTypedSync<FontAtlas>(
                m_fontRefs[index].atlas.name(), atlasAssets[index], GLOBAL_TEXT("UiLayerSystem"), "UI font atlas"
            );
            if(!atlas)
                NWB_LOGGER_WARNING(GLOBAL_TEXT("UiLayerSystem: optional font atlas unavailable; using native coverage"));
        }
        sources.push_back({ m_fontRefs[index].font, *font, 1u, atlas });
    }
    // TextService owns the exact font bytes and immutable baked atlas versions; loaded asset objects can be released here.
    m_fontsReady = m_text.setFonts(sources.data(), sources.size());
    return m_fontsReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

