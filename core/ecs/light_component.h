// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/matrix_math.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LightType{
    enum Enum : u8{
        Directional,
        Point,

        kCount
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(AlignedFloat4Data) LightComponent{
    AlignedFloat3Data color = AlignedFloat3Data(1.0f, 1.0f, 1.0f);
    f32 intensity = 1.0f;
    f32 range = 10.0f;
    LightType::Enum type = LightType::Directional;
};

static_assert(sizeof(LightType::Enum) == sizeof(u8), "LightType must stay compact for ECS storage");
static_assert(IsStandardLayout_V<LightComponent>, "LightComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<LightComponent>, "LightComponent must stay cheap to move in dense ECS storage");
static_assert(
    alignof(LightComponent) >= alignof(AlignedFloat4Data),
    "LightComponent must stay aligned for SIMD component loads"
);
static_assert(
    sizeof(LightComponent) == (sizeof(AlignedFloat4Data) * 2),
    "LightComponent must stay two aligned vectors wide"
);
static_assert(
    (sizeof(LightComponent) % alignof(LightComponent)) == 0,
    "LightComponent array stride must keep every element SIMD-aligned"
);
static_assert((offsetof(LightComponent, color) % alignof(AlignedFloat3Data)) == 0, "LightComponent::color must stay aligned");
static_assert((offsetof(LightComponent, intensity) % alignof(f32)) == 0, "LightComponent::intensity must stay aligned");
static_assert((offsetof(LightComponent, range) % alignof(f32)) == 0, "LightComponent::range must stay aligned");
static_assert((offsetof(LightComponent, type) % alignof(LightType::Enum)) == 0, "LightComponent::type must stay aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

