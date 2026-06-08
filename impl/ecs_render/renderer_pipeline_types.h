// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

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

[[nodiscard]] inline bool MaterialPipelinePassUsesRendererAvboit(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
    case MaterialPipelinePass::AvboitAccumulate:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool MaterialPipelinePassUsesRendererCsgShaderVariant(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::Opaque:
    case MaterialPipelinePass::CsgReceiverSurface:
        return true;
    default:
        return MaterialPipelinePassUsesRendererAvboit(pass);
    }
}

[[nodiscard]] inline bool MaterialPipelinePassUsesRendererCsgClip(const MaterialPipelinePass::Enum pass, const bool transparent){
    switch(pass){
    case MaterialPipelinePass::Opaque:
    case MaterialPipelinePass::CsgReceiverSurface:
        return !transparent;
    default:
        return transparent && MaterialPipelinePassUsesRendererAvboit(pass);
    }
}

[[nodiscard]] inline bool MaterialPipelinePassUsesRendererCsgReceiverSurfaceMask(const MaterialPipelinePass::Enum pass){
    return pass == MaterialPipelinePass::CsgReceiverSurface;
}

[[nodiscard]] inline bool MaterialPipelinePassUsesRendererCsgIntervalSample(const MaterialPipelinePass::Enum pass){
    return pass == MaterialPipelinePass::Opaque;
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
    u64 typedLayoutHash = 0u;
    MaterialTypedLayoutBlockVector typedLayoutBlocks;
    MaterialTypedLayoutFieldVector typedLayoutFields;
    MaterialTypedByteVector constantTypedBytes;
    MaterialTypedByteVector mutableDefaultTypedBytes;
    bool transparent = false;
    bool twoSided = false;

    explicit MaterialSurfaceInfo(Core::Alloc::GlobalArena& arena)
        : shaderVariant(arena)
        , typedLayoutBlocks(arena)
        , typedLayoutFields(arena)
        , constantTypedBytes(arena)
        , mutableDefaultTypedBytes(arena)
    {}
};

struct MaterialPipelineResources{
    RenderPath::Enum renderPath = RenderPath::MeshShader;
    Core::GraphicsPipelineHandle emulationPipeline;
    Core::MeshletPipelineHandle meshletPipeline;
    Core::ComputePipelineHandle computePipeline;
    Core::ShaderHandle pixelShader;
    Core::ShaderHandle meshShader;
    Core::ShaderHandle computeShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

