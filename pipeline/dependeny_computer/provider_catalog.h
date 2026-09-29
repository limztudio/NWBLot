// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../command_line.h"

#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// First discovery retains the same physical provider and root ownership as AssetBuilder.
class DependencyProviderCatalog final : NoCopy{
public:
    explicit DependencyProviderCatalog(NWB::Core::Assets::AssetArena& arena);
    DependencyProviderCatalog(DependencyProviderCatalog&&) = delete;


public:
    [[nodiscard]] bool discover(const PipelineOptions& options, NWB::Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool selectInputs(
        const PipelineOptions& options,
        Vector<u8, NWB::Core::Alloc::ScratchArena>& outSelected,
        Vector<usize, NWB::Core::Alloc::ScratchArena>& outOrder,
        NWB::Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] bool read(
        usize index,
        NWB::Core::Metascript::Document& outDocument,
        NWB::Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] bool resolve(
        const Name& virtualPath,
        const Name& assetType,
        usize& outIndex,
        NWB::Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] const NWB::Core::Assets::DiscoveredNwbFileVector& files()const{ return m_files; }


private:
    NWB::Core::Assets::AssetArena& m_arena;
    NWB::Path m_repoRoot;
    NWB::Core::Assets::CookVector<NWB::Core::Assets::ResolvedAssetRoot> m_roots;
    NWB::Core::Assets::DiscoveredNwbFileVector m_files;
    NWB::Core::Assets::AssetVector<Name> m_virtualPaths;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

