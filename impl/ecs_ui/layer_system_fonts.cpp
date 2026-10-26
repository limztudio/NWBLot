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
    const usize fontCount = uiSubsystem->basicFonts.size();
    if(fontCount == 0u || fontCount > Ui::s_TextMaxFontSources){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: the font stack requires between one and eight font assets"));
        return false;
    }
    Array<UniquePtr<Font>, Ui::s_TextMaxFontSources> assets;
    Array<UniquePtr<FontAtlas>, Ui::s_TextMaxFontSources> atlasAssets;
    Vector<Ui::FontSource, Core::Alloc::ScratchArena> sources(scratchArena);
    sources.reserve(fontCount);
    for(usize index = 0u; index < fontCount; ++index){
        const Core::Assets::AssetRef<FontAtlas>& atlasRef = uiSubsystem->basicFonts[index];
        auto loadedAtlas = m_assetManager.loadTypedSync<FontAtlas>(atlasRef.name(), s_UiLayerSystemOwnerName);
        if(!loadedAtlas)
            return false;
        atlasAssets[index] = Move(*loadedAtlas);
        const FontAtlas& atlas = *atlasAssets[index];
        const Core::Assets::AssetRef<Font>& fontRef = atlas.payload().font;
        auto font = m_assetManager.loadTypedSync<Font>(fontRef.name(), s_UiLayerSystemOwnerName);
        if(!font)
            return false;
        assets[index] = Move(*font);
        sources.push_back({ fontRef, *assets[index], 1u, &atlas });
    }
    // TextService owns the exact font bytes and immutable baked atlas versions; loaded asset objects can be released here.
    m_fontsReady = m_text.setFonts(scratchArena, sources.data(), sources.size());
    return m_fontsReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

