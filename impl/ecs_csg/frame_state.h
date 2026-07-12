// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <global/core/alloc/scratch.h>
#include <global/core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CsgReceiverKind{
    enum Enum : u8{
        Static,
        Skinned,
    };
};

namespace CsgReceiverPass{
    enum Enum : u8{
        Opaque,
        Transparent,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgReceiverDrawState{
    bool active = false;
    CsgReceiverKind::Enum receiverKind = CsgReceiverKind::Static;
    // Resolved with cutterCount so render-side CSG work can iterate the lookup-owned
    // range directly instead of resolving the receiver component and group again.
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
};

struct CsgFrameState{
    bool hasAnyWork = false;

    bool hasOpaqueStaticWork = false;
    bool hasOpaqueSkinnedWork = false;
    bool hasTransparentStaticWork = false;
    bool hasTransparentSkinnedWork = false;

    u32 receiverCount = 0u;
    u32 cutterCount = 0u;

    [[nodiscard]] bool empty()const noexcept{ return !hasAnyWork; }
};

struct CsgFrameCutterRange{
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
};

using CsgReceiverVisibleCallback = bool(*)(
    Core::ECS::World& world,
    Core::ECS::EntityID entity,
    CsgReceiverKind::Enum receiverKind,
    const CsgReceiverComponent& receiver,
    void* userData
);

struct CsgFrameBuildDesc{
    CsgReceiverVisibleCallback receiverVisible = nullptr;
    void* receiverVisibleUserData = nullptr;

    bool includeOpaquePass = true;
    bool includeTransparentPass = true;
};

static_assert(IsStandardLayout_V<CsgReceiverDrawState>, "CsgReceiverDrawState must stay layout-stable for frame handoff");
static_assert(IsTriviallyCopyable_V<CsgReceiverDrawState>, "CsgReceiverDrawState must stay cheap to pass by value");
static_assert(IsStandardLayout_V<CsgFrameState>, "CsgFrameState must stay layout-stable for frame handoff");
static_assert(IsTriviallyCopyable_V<CsgFrameState>, "CsgFrameState must stay cheap to pass by value");
static_assert(IsStandardLayout_V<CsgFrameCutterRange>, "CsgFrameCutterRange must stay layout-stable for frame handoff");
static_assert(IsTriviallyCopyable_V<CsgFrameCutterRange>, "CsgFrameCutterRange must stay cheap to pass by value");
static_assert(IsStandardLayout_V<CsgFrameBuildDesc>, "CsgFrameBuildDesc must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgFrameBuildDesc>, "CsgFrameBuildDesc must stay cheap to pass by value");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgFrameReceiverLookup final : NoCopy{
private:
    struct CsgFrameCutterRef{
        Core::ECS::EntityID entity;
        const CsgCutterComponent* cutter = nullptr;
    };

    using CutterRangeMap = HashMap<Name, CsgFrameCutterRange, Hasher<Name>, EqualTo<Name>, Core::Alloc::ScratchArena>;
    using CutterWriteCountMap = HashMap<Name, u32, Hasher<Name>, EqualTo<Name>, Core::Alloc::ScratchArena>;
    using CutterRefVector = Vector<CsgFrameCutterRef, Core::Alloc::ScratchArena>;


public:
    CsgFrameReceiverLookup(Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena);


public:
    [[nodiscard]] bool empty()const noexcept{ return m_cutterRanges.empty(); }
    [[nodiscard]] u32 cutterCount()const noexcept{ return static_cast<u32>(m_cutterRefs.size()); }
    [[nodiscard]] bool resolveReceiverDrawState(
        Core::ECS::EntityID entity,
        CsgReceiverPass::Enum receiverPass,
        CsgReceiverDrawState& outState
    )const;
    template<typename CutterHandler>
    void forEachReceiverCutter(const CsgReceiverDrawState& drawState, CutterHandler&& handler)const{
        if(!drawState.active || drawState.cutterCount == 0u)
            return;

        const usize firstCutter = static_cast<usize>(drawState.firstCutter);
        const usize cutterCount = static_cast<usize>(drawState.cutterCount);
        if(firstCutter > m_cutterRefs.size() || cutterCount > m_cutterRefs.size() - firstCutter){
            NWB_ASSERT(false);
            return;
        }

        const usize cutterEnd = firstCutter + cutterCount;
        for(usize cutterIndex = firstCutter; cutterIndex < cutterEnd; ++cutterIndex){
            const CsgFrameCutterRef& cutterRef = m_cutterRefs[cutterIndex];
            NWB_ASSERT(cutterRef.cutter != nullptr);
            handler(cutterRef.entity, *cutterRef.cutter);
        }
    }
    template<typename CutterHandler>
    void forEachReceiverCutter(const Core::ECS::EntityID entity, CutterHandler&& handler)const{
        CsgFrameCutterRange range;
        if(!resolveReceiverCutterRange(entity, range))
            return;

        CsgReceiverDrawState drawState;
        drawState.active = true;
        drawState.firstCutter = range.firstCutter;
        drawState.cutterCount = range.cutterCount;
        forEachReceiverCutter(drawState, handler);
    }


private:
    Core::ECS::World& m_world;
    CutterRangeMap m_cutterRanges;
    CutterRefVector m_cutterRefs;
    [[nodiscard]] bool resolveReceiverCutterRange(Core::ECS::EntityID entity, CsgFrameCutterRange& outRange)const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool HasCsgFrameCandidates(Core::ECS::World& world);
[[nodiscard]] const CsgReceiverComponent* ResolveCsgReceiverComponent(
    Core::ECS::World& world,
    Core::ECS::EntityID entity,
    CsgReceiverKind::Enum& outReceiverKind
);
void AddCsgFrameReceiverWork(
    CsgFrameState& inOutState,
    CsgReceiverKind::Enum receiverKind,
    bool opaqueWork,
    bool transparentWork,
    u32 cutterCount
);
void FinalizeCsgFrameState(CsgFrameState& inOutState);
[[nodiscard]] CsgFrameState BuildCsgFrameState(
    Core::ECS::World& world,
    Core::Alloc::ScratchArena& scratchArena,
    const CsgFrameBuildDesc& desc = CsgFrameBuildDesc{}
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

