// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "dependencies.h"

#include "provider_catalog.h"

#include <impl/assets_ui_skin/dependencies.h>
#include <impl/assets_texture/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString>> ComputeSkinDependencies(const PipelineOptions& options,
    NWB::Core::Assets::AssetArena& arena,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    using namespace NWB;
    namespace Assets = Core::Assets;
    Assets::AssetVector<Assets::AssetString> candidate(arena);
    candidate.reserve(options.inputs.size());
    for(const auto& input : options.inputs)
        candidate.emplace_back(input, arena);
    if(!options.includeSkinDependencies || options.inputs.empty()){
        return candidate;
    }
    DependencyProviderCatalog catalog(arena);
    if(!catalog.discover(options, scratchArena))
        return MakeUnexpected(Failure{});
    const auto selection = catalog.selectInputs(options, scratchArena);
    if(!selection)
        return MakeUnexpected(Failure{});
    const auto& files = catalog.files();
    Vector<u8, Assets::ScratchArena> included(selection->selected, scratchArena);
    for(const usize index : selection->order){
        const auto document = catalog.read(index, scratchArena);
        if(!document)
            return MakeUnexpected(Failure{});
        if(Name(document->assetType()) != Impl::UiSkin::s_AssetTypeName)
            continue;
        const auto& file = files[index];
        const auto texture = Impl::ExtractUiSkinTextureDependency(
            file.assetRoot,
            file.virtualRoot.view(),
            file.filePath,
            *document,
            scratchArena
        );
        if(!texture)
            return MakeUnexpected(Failure{});
        const auto provider = catalog.resolve(texture->name(), Impl::Texture::s_AssetTypeName, scratchArena);
        if(!provider)
            return MakeUnexpected(Failure{});
        if(included[*provider])
            continue;
        candidate.emplace_back(PathToString(arena, files[*provider].filePath.lexicallyNormal()));
        included[*provider] = 1u;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

