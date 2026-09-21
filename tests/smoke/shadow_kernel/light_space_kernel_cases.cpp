// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_kernel_fixture.h"

#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/shadow/constants.h>

#include <global/math/convert.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LightSpaceKernel{


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_kernel_cases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Float3U Minimum(const Float3U& a, const Float3U& b){
    return { Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z) };
}

[[nodiscard]] Float3U Maximum(const Float3U& a, const Float3U& b){
    return { Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z) };
}

[[nodiscard]] u32 AppendTree(Vector<Node, Alloc::ScratchArena>& nodes,
    const Vector<Node, Alloc::ScratchArena>& leaves, const u32 first, const u32 count){
    const u32 index = static_cast<u32>(nodes.size());
    nodes.push_back(Node{});
    if(count == 1u){
        nodes[index] = leaves[first];
        return index;
    }
    const u32 leftCount = count / s_ExpectedDualCount;
    const u32 left = AppendTree(nodes, leaves, first, leftCount);
    const u32 right = AppendTree(nodes, leaves, first + leftCount, count - leftCount);
    nodes[index] = { Minimum(nodes[left].minimum, nodes[right].minimum), left,
        Maximum(nodes[left].maximum, nodes[right].maximum),
        right | ((nodes[left].right | nodes[right].right) & NWB_BVH_TRANSPARENT_SUBTREE_FLAG) };
    return index;
}

[[nodiscard]] Float3U Cross(const Float3U& a, const Float3U& b){
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}


void BuildReceiver(const Case& testCase, Inputs& input, Alloc::ScratchArena& scratchArena){
    const f32 base = testCase.point ? -2.0f : 0.0f;
    const f32 a = testCase.receiverSlopeX;
    const f32 b = testCase.receiverSlopeY;
    const Float2U corners[] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
    for(const auto& corner : corners)
        input.positions.push_back({ corner.x, corner.y, a * corner.x + b * corner.y + base });
    const u32 triangles[] = { 0u, 1u, s_ExpectedDualCount, 0u, s_ExpectedDualCount, 3u };
    const f32 length = Sqrt(a * a + b * b + 1.0f);
    const Float3U geometricNormal{ -a / length, -b / length, 1.0f / length };
    Attribute attribute{};
    for(u32 axis = 0u; axis < 3u; ++axis)
        attribute.normal[axis] = ConvertFloatToHalf(geometricNormal.raw[axis]);
    Vector<Node, Alloc::ScratchArena> leaves(scratchArena);
    for(u32 triangle = 0u; triangle < LengthOf(triangles); triangle += 3u){
        const auto& p0 = input.positions[triangles[triangle]];
        const auto& p1 = input.positions[triangles[triangle + 1u]];
        const auto& p2 = input.positions[triangles[triangle + s_ExpectedDualCount]];
        leaves.push_back({ Minimum(Minimum(p0, p1), p2), NWB_BVH_LEAF_FLAG | triangle / 3u,
            Maximum(Maximum(p0, p1), p2), 1u });
        for(u32 corner = 0u; corner < 3u; ++corner){
            input.indices.push_back(triangles[triangle + corner]);
            input.attributes.push_back(attribute);
        }
    }
    EXPECT_EQ(AppendTree(input.mesh, leaves, 0u, s_ExpectedDualCount), 0u);
    leaves.clear();
    input.localMinimum = input.mesh[0].minimum;
    input.localMaximum = input.mesh[0].maximum;
    for(u32 instance = 0u; instance < testCase.count; ++instance){
        const f32 shift = instance == 0u ? 0.0f : testCase.receiverGap;
        Instance inverse{};
        inverse.primitiveCount = s_ExpectedDualCount;
        inverse.inverseRows[0] = { 1.0f, 0.0f, 0.0f, 0.0f };
        inverse.inverseRows[1] = { 0.0f, 1.0f, 0.0f, 0.0f };
        inverse.inverseRows[2] = { 0.0f, 0.0f, 1.0f, -shift };
        input.instances.push_back(inverse);
        Impl::InstanceGpuData transform{};
        transform.translation.z = shift;
        input.transforms.push_back(transform);
        Material material{};
        material.flags = 0u;
        material.meshInstanceIndex = instance;
        input.materials.push_back(material);
        Float3U minimum = input.localMinimum;
        Float3U maximum = input.localMaximum;
        minimum.z += shift;
        maximum.z += shift;
        leaves.push_back({ minimum, NWB_BVH_LEAF_FLAG | instance, maximum, 1u });
    }
    EXPECT_EQ(AppendTree(input.scene, leaves, 0u, testCase.count), 0u);
    input.receiver = { testCase.receiverExpectation == ReceiverExpectation::Singular ? 0.0f : 0.137f, -0.219f, 0.0f };
    input.receiver.z = a * input.receiver.x + b * input.receiver.y + base;
    for(u32 axis = 0u; axis < 3u; ++axis){
        const f32 encoded = geometricNormal.raw[axis] * 0.5f + 0.5f;
        input.encodedNormal.raw[axis] = testCase.receiverHalf ? ConvertHalfToFloat(ConvertFloatToHalf(encoded)) : encoded;
        if(testCase.receiverHalf)
            input.receiver.raw[axis] = ConvertHalfToFloat(ConvertFloatToHalf(input.receiver.raw[axis]));
        input.normal.raw[axis] = input.encodedNormal.raw[axis] * 2.0f - 1.0f;
    }
    const f32 decodedLength = Sqrt(input.normal.x * input.normal.x + input.normal.y * input.normal.y + input.normal.z * input.normal.z);
    for(u32 axis = 0u; axis < 3u; ++axis)
        input.normal.raw[axis] /= decodedLength;
    input.light.position = { 0.0f, 0.0f, 0.0f, 1.0f };
    input.light.direction = { 0.0f, 0.0f, 1.0f, 0.0f };
    input.light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    input.light.params = { 100.0f, testCase.point ? 1.0f : 0.0f, static_cast<f32>(s_OutputLayer), -1.0f };
    if(testCase.point)
        input.light.size.y = testCase.sourceSize;
    else
        input.light.size.x = testCase.sourceSize;
    input.scene[0].minimum = Minimum(input.scene[0].minimum, { input.receiver.x - 1.0f, input.receiver.y - 1.0f, input.receiver.z - 1.0f });
    input.scene[0].maximum = Maximum(input.scene[0].maximum, { input.receiver.x + 1.0f, input.receiver.y + 1.0f, input.receiver.z + 1.0f });
}

[[nodiscard]] Float3U ReceiverVisibility(const Case& testCase, const Inputs& input,
    const Float3U& direction, const f64 maximum){
    // Solve the authored plane equation in FP64, then test its finite square support. No depth-map threshold is used.
    const f64 a = testCase.receiverSlopeX;
    const f64 b = testCase.receiverSlopeY;
    const f64 base = testCase.point ? -2.0 : 0.0;
    const f64 x = static_cast<f64>(input.receiver.x) + static_cast<f64>(input.normal.x) * NWB_SHADOW_RAY_MIN_DISTANCE;
    const f64 y = static_cast<f64>(input.receiver.y) + static_cast<f64>(input.normal.y) * NWB_SHADOW_RAY_MIN_DISTANCE;
    const f64 z = static_cast<f64>(input.receiver.z) + static_cast<f64>(input.normal.z) * NWB_SHADOW_RAY_MIN_DISTANCE;
    const f64 denominator = static_cast<f64>(direction.z) - a * direction.x - b * direction.y;
    if(Abs(denominator) < 1e-14)
        return { 1.0f, 1.0f, 1.0f };
    for(u32 instance = 0u; instance < testCase.count; ++instance){
        const f64 shift = instance == 0u ? 0.0 : testCase.receiverGap;
        const f64 distance = (a * x + b * y + base + shift - z) / denominator;
        if(distance <= NWB_SHADOW_RAY_MIN_DISTANCE || distance >= maximum)
            continue;
        const f64 hitX = x + static_cast<f64>(direction.x) * distance;
        const f64 hitY = y + static_cast<f64>(direction.y) * distance;
        if(Abs(hitX) < 1.0 && Abs(hitY) < 1.0)
            return { 0.0f, 0.0f, 0.0f };
    }
    return { 1.0f, 1.0f, 1.0f };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Inputs::Inputs(Alloc::ScratchArena& arena)
    : positions(arena)
    , indices(arena)
    , attributes(arena)
    , mesh(arena)
    , scene(arena)
    , instances(arena)
    , transforms(arena)
    , materials(arena)
{}

void BuildInputs(const Case& testCase, Inputs& input, Alloc::ScratchArena& scratchArena){
    using namespace __hidden_light_space_kernel_cases;
    if(testCase.receiverExpectation != ReceiverExpectation::None){
        BuildReceiver(testCase, input, scratchArena);
        return;
    }
    const u32 axis = testCase.face / s_ExpectedDualCount;
    const f32 sign = (testCase.face & 1u) == 0u ? 1.0f : -1.0f;
    input.localMinimum = { -1.0f, -1.0f, -1.0f };
    input.localMaximum = { 1.0f, 1.0f, 1.0f };
    input.localMinimum.raw[axis] = sign > 0.0f ? 1.0f : -2.0f;
    input.localMaximum.raw[axis] = sign > 0.0f ? 2.0f : -1.0f;
    if(testCase.seam != 0){
        input.localMinimum = { 1.0f, -1.0f, 1.0f };
        input.localMaximum = { 2.0f, 1.0f, 2.0f };
        input.receiver = { 4.0f, 0.0f, 4.0f + static_cast<f32>(testCase.seam) * 0.02f };
    }
    else
        input.receiver.raw[axis] = testCase.point ? sign * 4.0f : 0.0f;
    if(testCase.inside)
        input.receiver.raw[axis] = sign * 1.5f;
    if(testCase.nearClip){
        input.localMinimum.raw[axis] = -0.0001f;
        input.localMaximum.raw[axis] = 1.0f;
    }
    input.normal.raw[axis] = testCase.point ? -sign : sign;
    if(testCase.seam != 0){
        const f32 length = Sqrt(input.receiver.x * input.receiver.x + input.receiver.z * input.receiver.z);
        input.normal = { -input.receiver.x / length, 0.0f, -input.receiver.z / length };
    }
    if(testCase.asymmetric){
        input.receiver.y = 2.0f;
        input.localMinimum.y = testCase.point ? 0.25f : 1.0f;
        input.localMaximum.y = testCase.point ? 1.25f : 3.0f;
        if(testCase.point){
            const f32 length = Sqrt(input.receiver.y * input.receiver.y + input.receiver.z * input.receiver.z);
            input.normal = { 0.0f, -input.receiver.y / length, -input.receiver.z / length };
        }
    }
    if(testCase.sourceSize > 0.0f){
        if(testCase.receiverX != 0.0f)
            input.receiver.x = testCase.receiverX;
        if(testCase.point){
            const f32 length = Sqrt(input.receiver.x * input.receiver.x + input.receiver.y * input.receiver.y
                + input.receiver.z * input.receiver.z);
            input.normal = { -input.receiver.x / length, -input.receiver.y / length, -input.receiver.z / length };
            input.light.size.y = testCase.sourceSize;
        }
        else
            input.light.size.x = testCase.sourceSize;
    }
    input.light.position = { 0.0f, 0.0f, 0.0f, 1.0f };
    input.light.direction = { input.normal.x, input.normal.y, input.normal.z, 0.0f };
    input.light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    input.light.params = { 100.0f, testCase.point ? 1.0f : 0.0f, static_cast<f32>(s_OutputLayer), -1.0f };
    constexpr u32 triangles[] = {
        0u, s_ExpectedDualCount, 1u, 0u, 3u, s_ExpectedDualCount, 4u, 5u, 6u, 4u, 6u, 7u,
        0u, 1u, 5u, 0u, 5u, 4u, 3u, 7u, 6u, 3u, 6u, s_ExpectedDualCount,
        0u, 4u, 7u, 0u, 7u, 3u, 1u, s_ExpectedDualCount, 6u, 1u, 6u, 5u,
    };
    for(u32 corner = 0u; corner < 8u; ++corner){
        // Ordering matches the standard closed-box triangle list above.
        const u32 x = corner == 1u || corner == s_ExpectedDualCount || corner == 5u || corner == 6u ? 1u : 0u;
        const u32 y = corner == s_ExpectedDualCount || corner == 3u || corner == 6u || corner == 7u ? 1u : 0u;
        const u32 z = corner >= 4u ? 1u : 0u;
        input.positions.push_back({ x ? input.localMaximum.x : input.localMinimum.x,
            y ? input.localMaximum.y : input.localMinimum.y, z ? input.localMaximum.z : input.localMinimum.z });
    }
    Vector<Node, Alloc::ScratchArena> leaves(scratchArena);
    for(u32 triangle = 0u; triangle < LengthOf(triangles); triangle += 3u){
        const Float3U& a = input.positions[triangles[triangle]];
        const Float3U& b = input.positions[triangles[triangle + 1u]];
        const Float3U& c = input.positions[triangles[triangle + s_ExpectedDualCount]];
        const Float3U edge1{ b.x - a.x, b.y - a.y, b.z - a.z };
        const Float3U edge2{ c.x - a.x, c.y - a.y, c.z - a.z };
        Float3U normal = Cross(edge1, edge2);
        const f32 length = Sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        Attribute attribute{};
        for(u32 component = 0u; component < 3u; ++component)
            attribute.normal[component] = ConvertFloatToHalf(normal.raw[component] / length);
        leaves.push_back({ Minimum(Minimum(a, b), c), NWB_BVH_LEAF_FLAG | triangle / 3u,
            Maximum(Maximum(a, b), c), 1u });
        for(u32 corner = 0u; corner < 3u; ++corner){
            input.indices.push_back(triangles[triangle + corner]);
            input.attributes.push_back(attribute);
        }
    }
    EXPECT_EQ(AppendTree(input.mesh, leaves, 0u, static_cast<u32>(leaves.size())), 0u);
    leaves.clear();
    for(u32 instance = 0u; instance < testCase.count; ++instance){
        Instance inverse{};
        Impl::InstanceGpuData transform{};
        Float3U translation{};
        Float3U minimum = input.localMinimum;
        Float3U maximum = input.localMaximum;
        for(u32 component = 0u; component < 3u; ++component){
            const f32 scale = component == axis ? testCase.thickness *
                (testCase.varyingThickness ? 1.0f + 0.25f * static_cast<f32>(instance) : 1.0f) : 1.0f;
            const f32 shift = component == axis ? sign * (1.0f - scale) : 0.0f;
            inverse.inverseRows[component].raw[component] = 1.0f / scale;
            inverse.inverseRows[component].w = -shift / scale;
            transform.scale.raw[component] = scale;
            translation.raw[component] = shift;
            minimum.raw[component] = minimum.raw[component] * scale + shift;
            maximum.raw[component] = maximum.raw[component] * scale + shift;
        }
        transform.translation = Float3UInt(translation.x, translation.y, translation.z, 0u);
        input.instances.push_back(inverse);
        input.transforms.push_back(transform);
        Material material{};
        material.flags = testCase.opaque ? 0u : NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT;
        material.meshInstanceIndex = instance;
        material.modelId = instance & 1u;
        input.materials.push_back(material);
        leaves.push_back({ minimum, NWB_BVH_LEAF_FLAG | instance, maximum,
            1u | (testCase.opaque ? 0u : NWB_BVH_TRANSPARENT_SUBTREE_FLAG) });
    }
    if(testCase.count != 0u){
        EXPECT_EQ(AppendTree(input.scene, leaves, 0u, testCase.count), 0u);
    }
    else{
        input.scene.push_back(Node{});
        input.instances.push_back(Instance{});
        input.transforms.push_back(Impl::InstanceGpuData{});
        input.materials.push_back(Material{});
    }
    // Loose but valid scene bounds include the receiver so the actual view producer can fit and resolve this synthetic surface.
    input.scene[0].minimum = Minimum(input.scene[0].minimum, { input.receiver.x - 1.0f, input.receiver.y - 1.0f, input.receiver.z - 1.0f });
    input.scene[0].maximum = Maximum(input.scene[0].maximum, { input.receiver.x + 1.0f, input.receiver.y + 1.0f, input.receiver.z + 1.0f });
    if(testCase.asymmetric){
        input.scene[0].minimum.y = -4.0f;
        input.scene[0].maximum.y = 4.0f;
    }
    if(testCase.invalidFit){
        input.scene[0].minimum = { -Limit<f32>::s_Max, -Limit<f32>::s_Max, -Limit<f32>::s_Max };
        input.scene[0].maximum = { Limit<f32>::s_Max, Limit<f32>::s_Max, Limit<f32>::s_Max };
    }
}

// Independent double slab intersections and per-instance Beer/Fresnel; neither raster records nor production accumulator are used.
Float3U Expected(const Case& testCase, const Inputs& input, const bool software){
    const f64 maximum = testCase.point ? Sqrt(static_cast<f64>(input.receiver.x) * input.receiver.x
        + static_cast<f64>(input.receiver.y) * input.receiver.y + static_cast<f64>(input.receiver.z) * input.receiver.z)
        - NWB_SHADOW_RAY_END_DISTANCE_BIAS : NWB_SHADOW_DIRECTIONAL_RAY_MAX_DISTANCE - NWB_SHADOW_RAY_END_DISTANCE_BIAS;
    Float3U direction = input.normal;
    if(testCase.receiverExpectation != ReceiverExpectation::None){
        if(testCase.point){
            const f32 length = Sqrt(input.receiver.x * input.receiver.x + input.receiver.y * input.receiver.y
                + input.receiver.z * input.receiver.z);
            direction = { -input.receiver.x / length, -input.receiver.y / length, -input.receiver.z / length };
        }
        else
            direction = { 0.0f, 0.0f, 1.0f };
    }
    return ExpectedRay(testCase, input, direction, maximum, software);
}

Float3U ExpectedRay(const Case& testCase, const Inputs& input, const Float3U& worldDirection,
    const f64 maximum, const bool software){
    if(testCase.receiverExpectation != ReceiverExpectation::None)
        return __hidden_light_space_kernel_cases::ReceiverVisibility(testCase, input, worldDirection, maximum);
    f64 result[3] = { 1.0, 1.0, 1.0 };
    for(u32 instance = 0u; instance < testCase.count; ++instance){
        f64 entry = -1e30;
        f64 exit = 1e30;
        f64 entryCosine = 0.0;
        f64 exitCosine = 0.0;
        bool intersects = true;
        const Float3U translation{ input.transforms[instance].translation.x, input.transforms[instance].translation.y,
            input.transforms[instance].translation.z };
        for(u32 axis = 0u; axis < 3u; ++axis){
            const f64 scale = input.transforms[instance].scale.raw[axis];
            const f64 origin = static_cast<f64>(input.receiver.raw[axis])
                + static_cast<f64>(input.normal.raw[axis]) * NWB_SHADOW_RAY_MIN_DISTANCE;
            const f64 direction = worldDirection.raw[axis];
            const f64 lower = static_cast<f64>(input.localMinimum.raw[axis]) * scale + translation.raw[axis];
            const f64 upper = static_cast<f64>(input.localMaximum.raw[axis]) * scale + translation.raw[axis];
            if(direction == 0.0){
                intersects = intersects && origin >= lower && origin <= upper;
                continue;
            }
            const f64 first = (lower - origin) / direction;
            const f64 second = (upper - origin) / direction;
            const f64 nearDistance = Min(first, second);
            const f64 farDistance = Max(first, second);
            if(nearDistance > entry){
                entry = nearDistance;
                entryCosine = Abs(direction);
            }
            if(farDistance < exit){
                exit = farDistance;
                exitCosine = Abs(direction);
            }
        }
        const bool entering = entry > NWB_SHADOW_RAY_MIN_DISTANCE && entry < maximum;
        const bool leaving = exit > NWB_SHADOW_RAY_MIN_DISTANCE && exit < maximum;
        if(!intersects || entry >= exit || (!entering && !leaving))
            continue;
        if(testCase.opaque)
            return { 0.0f, 0.0f, 0.0f };
        const f64 chord = entering && leaving ? exit - entry
            : testCase.closed && !software && leaving ? exit : 0.0;
        const f64 normalized = Min(chord / 0.002, 1.0);
        const f64 fade = normalized * normalized * (3.0 - 2.0 * normalized);
        const f64 entryT = 0.96 * (1.0 - Pow(1.0 - entryCosine, 5.0));
        const f64 exitT = 0.96 * (1.0 - Pow(1.0 - exitCosine, 5.0));
        const f64 product = (entering ? entryT : 1.0) * (leaving ? exitT : 1.0);
        const f64 interfaces = 1.0 + (product - 1.0) * fade;
        const f64 tint[] = { (instance & 1u) ? 0.75 : 0.5, (instance & 1u) ? 0.5 : 0.75, 1.0 };
        for(u32 channel = 0u; channel < 3u; ++channel)
            result[channel] *= interfaces * Pow(tint[channel], chord);
    }
    return { static_cast<f32>(result[0]), static_cast<f32>(result[1]), static_cast<f32>(result[2]) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

