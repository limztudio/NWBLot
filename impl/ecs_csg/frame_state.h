// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <core/ecs/world.h>


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
    bool generateCaps = false;
    u32 cutterCount = 0u;
};

struct CsgFrameState{
    bool hasAnyWork = false;

    bool hasOpaqueStaticWork = false;
    bool hasOpaqueSkinnedWork = false;
    bool hasTransparentStaticWork = false;
    bool hasTransparentSkinnedWork = false;

    bool hasOpaqueCapWork = false;
    bool hasTransparentCapWork = false;

    u32 receiverCount = 0u;
    u32 cutterCount = 0u;

    [[nodiscard]] bool empty()const noexcept{ return !hasAnyWork; }
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
static_assert(IsStandardLayout_V<CsgFrameBuildDesc>, "CsgFrameBuildDesc must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgFrameBuildDesc>, "CsgFrameBuildDesc must stay cheap to pass by value");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgFrameReceiverLookup final : NoCopy{
private:
    using CutterCountMap = HashMap<Name, u32, Hasher<Name>, EqualTo<Name>, Core::Alloc::ScratchArena>;


public:
    CsgFrameReceiverLookup(Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena);


public:
    [[nodiscard]] bool empty()const noexcept{ return m_cutterCounts.empty(); }
    [[nodiscard]] bool resolveReceiverDrawState(
        Core::ECS::EntityID entity,
        CsgReceiverPass::Enum receiverPass,
        CsgReceiverDrawState& outState
    )const;


private:
    Core::ECS::World& m_world;
    CutterCountMap m_cutterCounts;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] CsgFrameState BuildCsgFrameState(
    Core::ECS::World& world,
    Core::Alloc::ScratchArena& scratchArena,
    const CsgFrameBuildDesc& desc = CsgFrameBuildDesc{}
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

