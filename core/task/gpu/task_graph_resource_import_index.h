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
    struct NameHashEqual{
        [[nodiscard]] b8 operator()(const NameHash& left, const NameHash& right)const noexcept{
            return left == right;
        }
    };
    template<typename KeyT, typename HashFn, typename EqualFn>
    class FlatIndexTable final : NoCopy{
    public:
        struct Slot{
            KeyT key{};
            u32 value = 0u;
            u32 stamp = 0u;
        };


    public:
        explicit FlatIndexTable(GraphicsArena& arena)noexcept
            : m_arena(arena)
            , m_slots(arena)
        {}


    public:
        [[nodiscard]] b8 active()const noexcept{ return !m_slots.empty(); }

        [[nodiscard]] usize size()const noexcept{ return m_count; }

        [[nodiscard]] u32 find(const KeyT& key)const noexcept;
        void reserveForCount(usize targetCount);
        [[nodiscard]] b8 emplace(const KeyT& key, u32 value);
        void erase(const KeyT& key)noexcept;
        void clear()noexcept;


    private:
        GraphicsArena& m_arena;
        GraphicsVector<Slot> m_slots;
        HashFn m_hash{};
        EqualFn m_equal{};
        u32 m_count = 0u;
        u32 m_stamp = 1u;
    };
    using IdentityIndex = FlatIndexTable<NameHash, Hasher<NameHash>, NameHashEqual>;
    using PointerIndex = FlatIndexTable<ResourcePointerKey, ResourcePointerHasher, GpuTaskGraphDetail::ResourcePointerEqual>;

public:
    struct Publication{
        b8 identity = false;
        b8 pointer = false;
    };

    explicit GpuGraphResourceImportIndex(GraphicsArena& arena)noexcept
        : m_resourceIdentityIndex(arena)
        , m_resourcePointerIndex(arena)
        , m_resourceSetIdentityIndex(arena)
    {}
    [[nodiscard]] u32 findResourceIdentity(Span<const ResourceNode> resources, const Name& identity)const noexcept;
    [[nodiscard]] ResourceImportMatch findPointerMatch(const Span<const ResourceNode> resources, const ResourcePointerKey& pointer)const noexcept{
        return findResourceImportMatch(resources, nullptr, pointer);
    }
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
    static constexpr usize s_MinTableSlotCount = 64u;
    static constexpr u32 s_InvalidImportIndex = Limit<u32>::s_Max;
    IdentityIndex m_resourceIdentityIndex;
    PointerIndex m_resourcePointerIndex;
    IdentityIndex m_resourceSetIdentityIndex;
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

