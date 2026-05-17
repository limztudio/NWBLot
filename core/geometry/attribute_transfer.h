// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AttributeTransferSkinInfluence4{
    u16 joint[4] = {};
    f32 weight[4] = {};
};
static_assert(IsStandardLayout_V<AttributeTransferSkinInfluence4>, "AttributeTransferSkinInfluence4 must stay layout-stable");
static_assert(IsTriviallyCopyable_V<AttributeTransferSkinInfluence4>, "AttributeTransferSkinInfluence4 must stay cheap to copy");

struct AttributeTransferSkinBlendSource{
    AttributeTransferSkinInfluence4 influence;
    f32 weight = 0.0f;
};
static_assert(IsStandardLayout_V<AttributeTransferSkinBlendSource>, "AttributeTransferSkinBlendSource must stay layout-stable");
static_assert(IsTriviallyCopyable_V<AttributeTransferSkinBlendSource>, "AttributeTransferSkinBlendSource must stay cheap to copy");

struct AttributeTransferFloat4BlendSource{
    Float4U value;
    f32 weight = 0.0f;
};
static_assert(IsStandardLayout_V<AttributeTransferFloat4BlendSource>, "AttributeTransferFloat4BlendSource must stay layout-stable");
static_assert(IsTriviallyCopyable_V<AttributeTransferFloat4BlendSource>, "AttributeTransferFloat4BlendSource must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidSkinInfluence4(const AttributeTransferSkinInfluence4& influence);

[[nodiscard]] bool BlendSkinInfluence4(
    const AttributeTransferSkinBlendSource* sources,
    usize sourceCount,
    AttributeTransferSkinInfluence4& outInfluence
);

[[nodiscard]] bool BlendFloat4(
    const AttributeTransferFloat4BlendSource* sources,
    usize sourceCount,
    Float4U& outValue
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

