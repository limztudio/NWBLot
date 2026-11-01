// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "../command_line.h"

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DependencyInputSelection{
    Vector<u8, Core::Alloc::ScratchArena> selected;
    Vector<usize, Core::Alloc::ScratchArena> order;


public:
    explicit DependencyInputSelection(Core::Alloc::ScratchArena& arena)noexcept
        : selected(arena)
        , order(arena)
    {}
};

// First discovery retains the same physical provider and root ownership as AssetBuilder.
class DependencyProviderCatalog final : NoCopy{
    friend Expected<Core::Assets::AssetVector<Core::Assets::AssetString>> ComputeAssetDependencies(const PipelineOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);


public:
    explicit DependencyProviderCatalog(Core::Assets::AssetArena& arena)noexcept
        : m_arena(arena)
        , m_repoRoot(arena)
        , m_roots(arena)
        , m_files(arena)
        , m_virtualPaths(arena)
    {}
    DependencyProviderCatalog(DependencyProviderCatalog&&) = delete;


public:
    [[nodiscard]] b8 discover(const PipelineOptions& options, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<DependencyInputSelection> selectInputs(
        const PipelineOptions& options,
        const Core::Assets::CookDependencyRegistry& dependencyRegistry,
        Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] Expected<Core::Metascript::Document> read(usize index, Core::Alloc::ScratchArena& scratchArena)const;
    [[nodiscard]] Expected<usize> resolve(
        const Name& virtualPath,
        const Name& assetType,
        AStringView virtualPathTextHint,
        Core::Alloc::ScratchArena& scratchArena,
        const Core::Assets::CookDependencyRegistry* dependencyRegistry = nullptr
    )const;


private:
    Core::Assets::AssetArena& m_arena;
    Path m_repoRoot;
    Core::Assets::CookVector<Core::Assets::ResolvedAssetRoot> m_roots;
    Core::Assets::DiscoveredNwbFileVector m_files;
    Core::Assets::AssetVector<Name> m_virtualPaths;
};

[[nodiscard]] Expected<Optional<Core::Assets::ExpandedAssetMetadataVector>> TryExpandBunchOrCompositeDocument(
    const Core::Assets::DiscoveredNwbFile& file,
    const Core::Metascript::Document& document,
    Core::Alloc::ScratchArena& scratchArena
);

// Preserves explicit inputs when provided, or discovers the project start scene and resolves transitive asset dependencies.
[[nodiscard]] Expected<Core::Assets::AssetVector<Core::Assets::AssetString>> ComputeAssetDependencies(const PipelineOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

