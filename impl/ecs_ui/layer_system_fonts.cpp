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
    Array<UniquePtr<Core::Assets::IAsset>, 8u> assets;
    Vector<Ui::FontSource, Core::Alloc::ScratchArena> sources(scratchArena);
    sources.reserve(m_fontRefs.size());
    for(usize index = 0u; index < m_fontRefs.size(); ++index){
        const Font* font = m_assetManager.loadTypedSync<Font>(
            m_fontRefs[index].name(), assets[index], MakeNotNull(NWB_TEXT("UiLayerSystem")), MakeNotNull("UI font")
        );
        if(!font)
            return false;
        sources.push_back({ m_fontRefs[index], *font, 1u });
    }
    // TextService owns the exact source bytes/native font versions; loaded asset objects can be released here.
    m_fontsReady = m_text.setFonts(sources.data(), sources.size());
    return m_fontsReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

