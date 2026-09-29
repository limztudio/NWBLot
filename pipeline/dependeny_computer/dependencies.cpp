// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "dependencies.h"

#include "provider_catalog.h"

#include <impl/assets_ui_skin/dependencies.h>
#include <impl/assets_texture/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ComputeSkinDependencies(const PipelineOptions& options,
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString>& outInputs,
    NWB::Core::Alloc::ScratchArena& scratchArena){
    using namespace NWB;
    namespace Assets = Core::Assets;
    Assets::AssetArena& arena = outInputs.get_allocator().arena();
    Assets::AssetVector<Assets::AssetString> candidate(arena);
    candidate.reserve(options.inputs.size());
    for(const auto& input : options.inputs)
        candidate.emplace_back(input, arena);
    if(!options.includeSkinDependencies || options.inputs.empty()){
        outInputs.swap(candidate);
        return true;
    }
    DependencyProviderCatalog catalog(arena);
    Vector<u8, Assets::ScratchArena> selected(scratchArena);
    Vector<usize, Assets::ScratchArena> order(scratchArena);
    if(!catalog.discover(options, scratchArena) || !catalog.selectInputs(options, selected, order, scratchArena))
        return false;
    const auto& files = catalog.files();
    Vector<u8, Assets::ScratchArena> included(selected, scratchArena);
    for(const usize index : order){
        Core::Metascript::Document document(arena);
        if(!catalog.read(index, document, scratchArena))
            return false;
        if(Name(document.assetType()) != Impl::UiSkin::s_AssetTypeName)
            continue;
        Assets::AssetRef<Impl::Texture> texture;
        const auto& file = files[index];
        if(!Impl::ExtractUiSkinTextureDependency(
            file.assetRoot,
            file.virtualRoot.view(),
            file.filePath,
            document,
            texture,
            scratchArena
        ))
            return false;
        usize provider = 0u;
        if(!catalog.resolve(texture.name(), Impl::Texture::s_AssetTypeName, provider, scratchArena))
            return false;
        if(included[provider])
            continue;
        candidate.emplace_back(PathToString(arena, files[provider].filePath.lexically_normal()));
        included[provider] = 1u;
    }
    outInputs.swap(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

