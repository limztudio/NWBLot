// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_helpers.h"

#include <core/geometry/frame_math.h>
#include <impl/assets_graphics/deformable_geometry_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformableRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DisplacementResolveFailure{
    enum Enum : u8{
        None,
        Descriptor,
        Scale,
        Amplitude,
    };
};

[[nodiscard]] inline bool ValidateDisplacementTexture(
    const DeformableDisplacement& displacement,
    const DeformableDisplacementTexture* texture){
    if(!DeformableDisplacementModeUsesTexture(displacement.mode))
        return true;

    return
        texture
        && texture->virtualPath() == displacement.texture.name()
        && texture->validatePayload()
    ;
}

[[nodiscard]] inline u32 SampleDisplacementTextureCoordinate(const f32 value, const u32 size){
    if(size <= 1u)
        return 0u;

    const f32 scaled = Saturate(value) * static_cast<f32>(size - 1u);
    const f32 rounded = Floor(scaled + 0.5f);
    return static_cast<u32>(Min(rounded, static_cast<f32>(size - 1u)));
}

[[nodiscard]] inline f32 DisplacementTextureCoordStep(const u32 size){
    return size > 1u ? 1.0f / static_cast<f32>(size - 1u) : 1.0f;
}

[[nodiscard]] inline Float2U DisplacementTextureCoord(const DeformableDisplacement& displacement, const Float2U& uv){
    return Float2U(
        Saturate((uv.x * displacement.uvScale.x) + displacement.uvOffset.x),
        Saturate((uv.y * displacement.uvScale.y) + displacement.uvOffset.y)
    );
}

[[nodiscard]] inline Float4U SampleDisplacementTextureCoord(const DeformableDisplacementTexture& texture, const Float2U& uv){
    const u32 x = SampleDisplacementTextureCoordinate(uv.x, texture.width());
    const u32 y = SampleDisplacementTextureCoordinate(uv.y, texture.height());
    const usize texelIndex = static_cast<usize>(y) * static_cast<usize>(texture.width()) + static_cast<usize>(x);
    return texture.texels()[texelIndex];
}

[[nodiscard]] inline Float4U SampleDisplacementTexture(
    const DeformableDisplacement& displacement,
    const DeformableDisplacementTexture& texture,
    const Float2U& uv){
    return SampleDisplacementTextureCoord(texture, DisplacementTextureCoord(displacement, uv));
}

[[nodiscard]] inline SIMDVector VectorTextureOffsetToFrame(
    const DeformableDisplacement& displacement,
    const u32 mode,
    const Float4U& sample,
    const SIMDVector normal,
    const SIMDVector tangentWithHandedness){
    SIMDVector vectorOffset = VectorMultiply(
        VectorAdd(VectorSetW(LoadFloat(sample), 0.0f), VectorReplicate(displacement.bias)),
        VectorReplicate(displacement.amplitude)
    );
    if(mode != DeformableDisplacementMode::VectorTangentTexture)
        return vectorOffset;

    const SIMDVector tangent = VectorSetW(tangentWithHandedness, 0.0f);
    const SIMDVector bitangent = VectorMultiply(
        Core::Geometry::FrameResolveBitangent(normal, tangent, VectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
        VectorReplicate(Core::Geometry::FrameTangentHandedness(VectorGetW(tangentWithHandedness), 1.0f))
    );
    vectorOffset = VectorMultiplyAdd(
        normal,
        VectorReplicate(VectorGetZ(vectorOffset)),
        VectorMultiplyAdd(
            bitangent,
            VectorReplicate(VectorGetY(vectorOffset)),
            VectorMultiply(tangent, VectorReplicate(VectorGetX(vectorOffset)))
        )
    );
    return vectorOffset;
}

[[nodiscard]] inline bool ResolveEffectiveDisplacement(
    const DeformableDisplacement& sourceDisplacement,
    const DeformableDisplacementComponent* component,
    DeformableDisplacement& outDisplacement,
    DisplacementResolveFailure::Enum& outFailure){
    outDisplacement = sourceDisplacement;
    outFailure = DisplacementResolveFailure::None;

    if(!ValidDeformableDisplacementDescriptor(outDisplacement)){
        outFailure = DisplacementResolveFailure::Descriptor;
        return false;
    }
    if(outDisplacement.mode == DeformableDisplacementMode::None)
        return true;
    if(component && !component->enabled){
        outDisplacement = DeformableDisplacement{};
        return true;
    }

    const f32 scale = component ? component->amplitudeScale : 1.0f;
    if(!IsFinite(scale)){
        outFailure = DisplacementResolveFailure::Scale;
        return false;
    }

    outDisplacement.amplitude *= scale;
    if(!IsFinite(outDisplacement.amplitude)){
        outFailure = DisplacementResolveFailure::Amplitude;
        return false;
    }
    if(!DeformableValidation::ActiveWeight(outDisplacement.amplitude))
        outDisplacement = DeformableDisplacement{};
    return true;
}

[[nodiscard]] inline bool ResolveEffectiveDisplacement(
    const DeformableDisplacement& sourceDisplacement,
    const DeformableDisplacementComponent* component,
    DeformableDisplacement& outDisplacement){
    DisplacementResolveFailure::Enum failure = DisplacementResolveFailure::None;
    return ResolveEffectiveDisplacement(sourceDisplacement, component, outDisplacement, failure);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

