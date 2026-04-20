// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


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
    // x/y/z = color, w = intensity.
    AlignedFloat4Data colorIntensity = AlignedFloat4Data(1.0f, 1.0f, 1.0f, 1.0f);
    f32 range = 10.0f;
    LightType::Enum type = LightType::Directional;

    [[nodiscard]] AlignedFloat3Data color()const{ return AlignedFloat3Data(colorIntensity.x, colorIntensity.y, colorIntensity.z); }
    [[nodiscard]] f32 intensity()const{ return colorIntensity.w; }

    void setColor(const AlignedFloat3Data& value){
        colorIntensity.x = value.x;
        colorIntensity.y = value.y;
        colorIntensity.z = value.z;
    }
    void setIntensity(const f32 value){ colorIntensity.w = value; }
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
static_assert(
    (offsetof(LightComponent, colorIntensity) % alignof(AlignedFloat4Data)) == 0,
    "LightComponent::colorIntensity must stay aligned"
);
static_assert((offsetof(LightComponent, range) % alignof(f32)) == 0, "LightComponent::range must stay aligned");
static_assert((offsetof(LightComponent, type) % alignof(LightType::Enum)) == 0, "LightComponent::type must stay aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

