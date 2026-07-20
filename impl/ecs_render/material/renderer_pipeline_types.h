#pragma once


#include <impl/ecs_render/kernel/components.h>

#include <core/alloc/scratch.h>
#include <core/assets/global.h>
#include <core/graphics/api.h>
#include <impl/assets_material/asset.h>

#include <global/containers.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialPipelinePass{
    enum Enum : u8{
        Opaque,
        CsgReceiverSurface,
        AvboitOccupancy,
        AvboitExtinction,
        AvboitAccumulate,
    };
};

namespace RenderPath{
    enum Enum : u8{
        MeshShader,
        ComputeEmulation,
    };
};

namespace MaterialPipelineCsgMode{
    enum Enum : u8{
        None,
        ClipOnly,
    };
};

[[nodiscard]] NWB_INLINE bool MaterialPipelinePassUsesRendererAvboit(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
    case MaterialPipelinePass::AvboitAccumulate:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] NWB_INLINE bool MaterialPipelinePassUsesRendererCsgShaderVariant(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::Opaque:
    case MaterialPipelinePass::CsgReceiverSurface:
        return true;
    default:
        return MaterialPipelinePassUsesRendererAvboit(pass);
    }
}

[[nodiscard]] NWB_INLINE bool MaterialPipelinePassUsesRendererCsgClip(const MaterialPipelinePass::Enum pass, const bool transparent){
    switch(pass){
    case MaterialPipelinePass::Opaque:
        return !transparent;
    case MaterialPipelinePass::CsgReceiverSurface:
        return true;
    default:
        return transparent && MaterialPipelinePassUsesRendererAvboit(pass);
    }
}

[[nodiscard]] NWB_INLINE bool MaterialPipelinePassUsesRendererCsgReceiverSurface(const MaterialPipelinePass::Enum pass){
    return pass == MaterialPipelinePass::CsgReceiverSurface;
}

[[nodiscard]] NWB_INLINE bool MaterialPipelinePassUsesRendererCsgIntervalSample(const MaterialPipelinePass::Enum pass){
    return pass == MaterialPipelinePass::Opaque || MaterialPipelinePassUsesRendererAvboit(pass);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialTypedByteVector = Vector<u8, Core::Alloc::GlobalArena>;
using MaterialTypedLayoutBlockVector = Vector<MaterialTypedLayoutBlock, Core::Alloc::GlobalArena>;
using MaterialTypedLayoutFieldVector = Vector<MaterialTypedLayoutField, Core::Alloc::GlobalArena>;
using MaterialTypedByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPipelineKey{
    Name material = NAME_NONE;
    Core::FramebufferInfo framebufferInfo;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
    bool twoSided = false;
    MaterialPipelineCsgMode::Enum csgMode = MaterialPipelineCsgMode::None;
    Name csgEvaluatorVariant = NAME_NONE;
};

struct MaterialPipelineCsgBindingUse{
    bool clip = false;
    bool avboitClip = false;
    bool receiverSurface = false;
    bool intervalSample = false;
};

[[nodiscard]] NWB_INLINE MaterialPipelineCsgBindingUse MaterialPipelineResolveCsgBindingUse(
    const MaterialPipelineKey& pipelineKey,
    const MaterialPipelinePass::Enum pass
){
    MaterialPipelineCsgBindingUse result;
    result.clip =
        pipelineKey.csgMode != MaterialPipelineCsgMode::None
        && MaterialPipelinePassUsesRendererCsgShaderVariant(pass)
    ;
    result.avboitClip =
        result.clip
        && MaterialPipelinePassUsesRendererAvboit(pass)
    ;
    result.receiverSurface =
        result.clip
        && MaterialPipelinePassUsesRendererCsgReceiverSurface(pass)
    ;
    result.intervalSample =
        result.clip
        && MaterialPipelinePassUsesRendererCsgIntervalSample(pass)
    ;
    return result;
}
struct MaterialPipelineKeyHasher{
    usize operator()(const MaterialPipelineKey& key)const;
};
struct MaterialPipelineKeyEqualTo{
    bool operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const;
};

struct MaterialSurfaceInfo{
    Name materialName = NAME_NONE;
    Name materialInterface = NAME_NONE;
    Core::GraphicsString shaderVariant;
    Core::Assets::AssetRef<Shader> pixelShader;
    Core::Assets::AssetRef<Shader> meshShader;
    // The cook-generated per-material AVBOIT accumulate pixel shader (transparent-pass twin of pixelShader),
    // valid for a surface-authored transparent material; the transparent draw binds it. It is invalid for an
    // opaque material. A missing shader on a transparent material is a cook/runtime contract failure.
    Core::Assets::AssetRef<Shader> avboitAccumulatePixelShader;
    // The occupancy/extinction twins, bound for those AVBOIT passes so all three read the material's SAME
    // shader-decided surface.renderCoverage. They are valid for a surface-authored transparent material and invalid
    // for an opaque material. A missing shader on a transparent material is a cook/runtime contract failure.
    Core::Assets::AssetRef<Shader> avboitOccupancyPixelShader;
    Core::Assets::AssetRef<Shader> avboitExtinctionPixelShader;
    u64 typedLayoutHash = 0u;
    MaterialTypedLayoutBlockVector typedLayoutBlocks;
    MaterialTypedLayoutFieldVector typedLayoutFields;
    MaterialTypedByteVector constantTypedBytes;
    MaterialTypedByteVector mutableDefaultTypedBytes;
    u32 shadingModelId = 0u;
    u32 shadowTransmittanceModelId = 0u;
    // CSG caps evaluate the cook-generated surface hook with this material's typed constants and mutable instance
    // storage. Explicit opaque stage shaders have no hook, so clipping is deliberately disabled for them.
    bool csgCapSurfaceDispatchAvailable = false;
    bool csgCapSurfaceDispatchUnavailableLogged = false;
    // The dedicated refractive-caster classification flag, copied from the cooked Material. The RT instance
    // occluder record (U0-2) reads it. The refraction VALUES (refractionIor / shadowAbsorptionTint) are shader-side
    // (NwbMeshSurface), not here. Default false (not a refractive caster) -- a material declaring none is unchanged.
    bool transparent = false;
    bool twoSided = false;
    bool refractive = false;

    explicit MaterialSurfaceInfo(Core::Alloc::GlobalArena& arena)
        : shaderVariant(arena)
        , typedLayoutBlocks(arena)
        , typedLayoutFields(arena)
        , constantTypedBytes(arena)
        , mutableDefaultTypedBytes(arena)
    {}
};

struct MaterialPipelineResources{
    Core::GraphicsPipelineHandle emulationPipeline;
    Core::MeshletPipelineHandle meshletPipeline;
    Core::ComputePipelineHandle computePipeline;
    Core::ShaderHandle pixelShader;
    Core::ShaderHandle meshShader;
    Core::ShaderHandle computeShader;
    RenderPath::Enum renderPath = RenderPath::MeshShader;
    bool emulationGraphicsUsesMeshFrameSet = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

