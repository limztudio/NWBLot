// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "asset_file_discovery.h"
#include "cook_input_selection.h"
#include "cook_paths.h"
#include <core/alloc/adhd_arena.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetCookWorkspace final : NoCopy{
public:
    explicit AssetCookWorkspace(const Name scope) : arena(scope), scratch(scope){}

    AssetArena arena;
    ScratchArena scratch;
};

template<typename Inputs>
[[nodiscard]] inline AssetVector<AssetString> CopyCookInputStrings(AssetArena& arena, const Inputs& inputs){
    AssetVector<AssetString> result(arena);
    result.reserve(inputs.size());
    for(const auto& input : inputs)
        result.emplace_back(AStringView(input), arena);
    return result;
}

template<typename Root, typename A, typename Inputs, typename OnError>
[[nodiscard]] inline Expected<Vector<Root, ::ArenaBase>> ResolveCookRootEntries(::ArenaBase& output, const ::Path<A>& repo, const Inputs& inputs, const b8 infer, const AssetRootDuplicatePolicy::Enum policy, OnError&& onError){
    AssetArena arena(output.allocationLog());
    ScratchArena scratch(output.allocationLog());
    const auto text = CopyCookInputStrings(arena, inputs);
    const auto resolved = ResolveAssetRoots(Path(arena, repo), text, infer, policy, scratch);
    if(!resolved){
        onError(AStringView(text[resolved.error().sourceIndex]), resolved.error().reason == AssetRootResolutionFailure::InspectInput);
        return MakeUnexpected(Failure{});
    }
    Vector<Root, ::ArenaBase> result(output);
    result.reserve(resolved->size());
    for(const auto& root : *resolved){
        auto& stored = result.emplace_back(output);
        stored.path = ::Path<::ArenaBase>(output, root.path);
        stored.virtualRoot = root.virtualRoot;
    }
    return result;
}

class CookFileCatalog final : NoCopy{
public:
    explicit CookFileCatalog(AssetCookWorkspace& workspace)
        : m_workspace(workspace), paths(workspace.arena), files(workspace.arena){}

    template<typename Arena>
    void setRepoRoot(const ::Path<Arena>& value){ paths.repoRoot = Path(m_workspace.arena, value); }
    template<typename Arena>
    void setOutputDirectory(const ::Path<Arena>& value){ paths.outputDirectory = Path(m_workspace.arena, value); }
    template<typename Arena>
    void setCacheDirectory(const ::Path<Arena>& value){ paths.cacheDirectory = Path(m_workspace.arena, value); }
    template<typename Arena>
    void appendRoot(const ::Path<Arena>& path, const AStringView name){
        paths.assetRoots.emplace_back(Path(m_workspace.arena, path), name);
    }

    [[nodiscard]] b8 discover(const AStringView extension){
        auto result = AssetFileDiscovery::Discover(paths.assetRoots, extension, m_workspace.arena, m_workspace.scratch);
        if(!result)
            return false;
        files = Move(*result);
        return true;
    }

    template<typename Inputs, typename Bytes, typename Order, typename OnError>
    [[nodiscard]] b8 select(const Inputs& inputs, Bytes& selected, Order& sequence, OnError&& onError)const{
        Vector<usize, ScratchArena> order(m_workspace.scratch);
        if(!selectInputs(inputs, selected, &order, onError))
            return false;
        sequence.assign(order.begin(), order.end());
        return true;
    }

    template<typename Inputs, typename Bytes, typename OnError>
    [[nodiscard]] b8 selectMask(const Inputs& inputs, Bytes& selected, OnError&& onError)const{
        return selectInputs(inputs, selected, nullptr, onError);
    }
    void replace(CookFileCatalog& source){
        FOUNDATION_ASSERT(&m_workspace == &source.m_workspace);
        paths = Move(source.paths);
        files = Move(source.files);
    }
    void compact(const Span<const u8> selected){ CookInputSelector::Compact(selected, files); }

private:
    template<typename Inputs, typename OnError>
    [[nodiscard]] b8 selectInputs(const Inputs& inputs, const Span<u8> selected, Vector<usize, ScratchArena>* const order, OnError& onError)const{
        const auto text = CopyCookInputStrings(m_workspace.arena, inputs);
        const auto result = CookInputSelector::Select(paths.repoRoot, paths.assetRoots, files, text, true, m_workspace.scratch, selected, order);
        if(!result)
            onError(result.error());
        return result.has_value();
    }

    AssetCookWorkspace& m_workspace;
public:
    ResolvedCookPaths paths;
    DiscoveredNwbFileVector files;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

