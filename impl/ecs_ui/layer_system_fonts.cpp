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
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: the font stack requires between one and eight font assets"));
        return false;
    }
    Array<UniquePtr<Font>, 8u> assets;
    Array<UniquePtr<FontAtlas>, 8u> atlasAssets;
    Vector<Ui::FontSource, Core::Alloc::ScratchArena> sources(scratchArena);
    sources.reserve(m_fontRefs.size());
    for(usize index = 0u; index < m_fontRefs.size(); ++index){
        auto font = m_assetManager.loadTypedSync<Font>(
            m_fontRefs[index].font.name(), NWB_TEXT("UiLayerSystem"), "UI font"
        );
        if(!font)
            return false;
        assets[index] = Move(*font);
        const FontAtlas* atlas = nullptr;
        if(m_fontRefs[index].atlas.valid()){
            auto loadedAtlas = m_assetManager.loadTypedSync<FontAtlas>(
                m_fontRefs[index].atlas.name(), NWB_TEXT("UiLayerSystem"), "UI font atlas"
            );
            if(!loadedAtlas)
                NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: optional font atlas unavailable; using native coverage"));
            else{
                atlasAssets[index] = Move(*loadedAtlas);
                atlas = atlasAssets[index].get();
            }
        }
        sources.push_back({ m_fontRefs[index].font, *assets[index], 1u, atlas });
    }
    // TextService owns the exact font bytes and immutable baked atlas versions; loaded asset objects can be released here.
    m_fontsReady = m_text.setFonts(sources.data(), sources.size());
    return m_fontsReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

