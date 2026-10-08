// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../command_line.h"

#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DependencyInputSelection{
    Vector<u8, NWB::Core::Alloc::ScratchArena> selected;
    Vector<usize, NWB::Core::Alloc::ScratchArena> order;

    explicit DependencyInputSelection(NWB::Core::Alloc::ScratchArena& arena)
        : selected(arena)
        , order(arena)
    {}
};

// First discovery retains the same physical provider and root ownership as AssetBuilder.
class DependencyProviderCatalog final : NoCopy{
public:
    explicit DependencyProviderCatalog(NWB::Core::Assets::AssetArena& arena);
    DependencyProviderCatalog(DependencyProviderCatalog&&) = delete;


public:
    [[nodiscard]] bool discover(const PipelineOptions& options, NWB::Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<DependencyInputSelection> selectInputs(
        const PipelineOptions& options,
        NWB::Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] Expected<NWB::Core::Metascript::Document> read(
        usize index,
        NWB::Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] Expected<usize> resolve(
        const Name& virtualPath,
        const Name& assetType,
        NWB::Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] const NWB::Core::Assets::DiscoveredNwbFileVector& files()const noexcept{ return m_files; }


private:
    NWB::Core::Assets::AssetArena& m_arena;
    NWB::Path m_repoRoot;
    NWB::Core::Assets::CookVector<NWB::Core::Assets::ResolvedAssetRoot> m_roots;
    NWB::Core::Assets::DiscoveredNwbFileVector m_files;
    NWB::Core::Assets::AssetVector<Name> m_virtualPaths;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

