// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph_nodes.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


class GpuGraphResourceImportIndex final : NoCopy{
private:
    using ResourcePointerKey = GpuTaskGraphDetail::ResourcePointerKey;
    using ResourceImportMatch = GpuTaskGraphDetail::ResourceImportMatch;
    using ResourceNode = GpuTaskGraphDetail::GpuGraphResourceNode;
    using ResourceSetNode = GpuTaskGraphDetail::GpuGraphResourceSetNode;
    struct ResourcePointerHasher{
        [[nodiscard]] usize operator()(const ResourcePointerKey& key)const noexcept{
            usize hash = Hasher<const void*>{}(key.pointer);
            HashCombine(hash, static_cast<u8>(key.type));
            return hash;
        }
    };
    using IdentityIndex = HashMap<NameHash, u32, GraphicsArena>;
    using PointerIndex = HashMap<ResourcePointerKey, u32, GraphicsArena, ResourcePointerHasher, GpuTaskGraphDetail::ResourcePointerEqual>;

public:
    struct Publication{
        b8 identity = false;
        b8 pointer = false;
    };

    explicit GpuGraphResourceImportIndex(GraphicsArena& arena)noexcept : m_arena(arena){}
    [[nodiscard]] u32 findResourceIdentity(Span<const ResourceNode> resources, const Name& identity)const noexcept;
    [[nodiscard]] ResourceImportMatch findResourceImportMatch(Span<const ResourceNode> resources, const NameHash* identity, const ResourcePointerKey& pointer)const noexcept;
    [[nodiscard]] u32 findResourceSetIdentity(Span<const ResourceSetNode> sets, const Name& identity)const noexcept;
    void prepareResourceIndexes(Span<const ResourceNode> resources, const ResourcePointerKey& pendingPointer);
    void prepareResourceSetIndex(Span<const ResourceSetNode> sets);
    [[nodiscard]] b8 publishResource(const NameHash& identity, const ResourcePointerKey& pointer, u32 index, Publication& publication);
    void rollbackResource(const NameHash& identity, const ResourcePointerKey& pointer, const Publication& publication)noexcept;
    [[nodiscard]] b8 publishResourceSet(const NameHash& identity, u32 index, b8& inserted);
    void rollbackResourceSet(const NameHash& identity, b8 inserted)noexcept;
    void clearResources();
    void clearResourceSets();

private:
    [[nodiscard]] static ResourcePointerKey MakeResourcePointerKey(const ResourceNode& resource)noexcept;
    static constexpr usize s_InlineImportIndexCount = 32u;
    static constexpr usize s_InitialPointerIndexBucketCount = 2u;
    static constexpr u32 s_InvalidImportIndex = Limit<u32>::s_Max;
    GraphicsArena& m_arena;
    Optional<IdentityIndex> m_resourceIdentityIndex;
    Optional<PointerIndex> m_resourcePointerIndex;
    Optional<IdentityIndex> m_resourceSetIdentityIndex;
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
