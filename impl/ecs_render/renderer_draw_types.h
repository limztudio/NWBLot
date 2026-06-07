// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_pipeline_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassDrawItem{
    Name meshKey = NAME_NONE;
    MaterialPipelineKey pipelineKey;
    u32 instanceIndex = 0;
    u32 materialConstantByteOffset = 0u;
    bool meshletConeCullScaleSafe = false;
};

struct MaterialInstanceMutableCacheEntry{
    Name materialName = NAME_NONE;
    Name materialInterface = NAME_NONE;
    u64 typedLayoutHash = 0u;
    u64 revision = 0u;
    MaterialTypedByteVector mutableTypedBytes;

    explicit MaterialInstanceMutableCacheEntry(Core::Alloc::GlobalArena& arena)
        : mutableTypedBytes(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialPassDrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassDrawItems{
    MaterialPassDrawItemVector meshDrawItems;
    MaterialPassDrawItemVector computeDrawItems;

    explicit MaterialPassDrawItems(Core::Alloc::ScratchArena& arena)
        : meshDrawItems(arena)
        , computeDrawItems(arena)
    {}

    [[nodiscard]] bool empty()const noexcept{ return meshDrawItems.empty() && computeDrawItems.empty(); }
    void reserve(const usize capacity){
        meshDrawItems.reserve(capacity);
        computeDrawItems.reserve(capacity);
    }
};

struct MaterialPassDrawItemPartitions{
    MaterialPassDrawItems regular;
    MaterialPassDrawItems csg;
    MaterialPassDrawItems csgReceiverSurface;

    explicit MaterialPassDrawItemPartitions(Core::Alloc::ScratchArena& arena)
        : regular(arena)
        , csg(arena)
        , csgReceiverSurface(arena)
    {}

    [[nodiscard]] bool empty()const noexcept{ return regular.empty() && csg.empty() && csgReceiverSurface.empty(); }
    void reserve(const usize capacity){
        regular.reserve(capacity);
        csg.reserve(capacity);
        csgReceiverSurface.reserve(capacity);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

