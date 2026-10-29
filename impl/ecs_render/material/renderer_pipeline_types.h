// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/components.adhd.h>
#include <impl/ecs_render/material/pipeline_types.adhd.h>
#include <impl/assets_material/asset.h>

#include <core/alloc/scratch.h>
#include <core/assets/global.h>
#include <core/graphics/api.h>

#include <global/containers.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PixelShader;
class MeshShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialTypedByteVector = Vector<u8, Core::Alloc::GlobalArena>;
using MaterialTypedLayoutBlockVector = Vector<MaterialTypedLayoutBlock, Core::Alloc::GlobalArena>;
using MaterialTypedLayoutFieldVector = Vector<MaterialTypedLayoutField, Core::Alloc::GlobalArena>;
using MaterialResourceReferenceVector = Vector<MaterialResourceReference, Core::Alloc::GlobalArena>;
using MaterialTypedByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPipelineKey{
    Name material = s_NameNone;
    Name csgEvaluatorVariant = s_NameNone;
    u64 csgShapeRegistryRevision = 0u;
    Core::FramebufferInfo framebufferInfo;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
    b8 twoSided = false;
    MaterialPipelineCsgMode::Enum csgMode = MaterialPipelineCsgMode::None;
};

[[nodiscard]] NWB_INLINE MaterialPipelineCsgBindingUse MaterialPipelineResolveCsgBindingUse(const MaterialPipelineKey& pipelineKey, const MaterialPipelinePass::Enum pass)noexcept{
    MaterialPipelineCsgBindingUse result;
    result.clip = pipelineKey.csgMode != MaterialPipelineCsgMode::None && MaterialPipelinePassUsesRendererCsgShaderVariant(pass);
    result.avboitClip = result.clip && MaterialPipelinePassUsesRendererAvboit(pass);
    return result;
}
struct MaterialPipelineKeyHasher{
    usize operator()(const MaterialPipelineKey& key)const noexcept;
};
struct MaterialPipelineKeyEqualTo{
    b8 operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const noexcept;
};

struct MaterialSurfaceInfo{
    Name materialName = s_NameNone;
    Name materialInterface = s_NameNone;
    Core::GraphicsString shaderVariant;
    Core::Assets::AssetRef<PixelShader> pixelShader;
    Core::Assets::AssetRef<MeshShader> meshShader;
    // Transparent surface materials require this cook-generated AVBOIT accumulation shader; opaque materials carry none.
    Core::Assets::AssetRef<PixelShader> avboitAccumulatePixelShader;
    // Occupancy/extinction must evaluate the same surface.renderCoverage and follow the same presence contract.
    Core::Assets::AssetRef<PixelShader> avboitOccupancyPixelShader;
    Core::Assets::AssetRef<PixelShader> avboitExtinctionPixelShader;
    u64 typedLayoutHash = 0u;
    MaterialTypedLayoutBlockVector typedLayoutBlocks;
    MaterialTypedLayoutFieldVector typedLayoutFields;
    MaterialResourceReferenceVector resourceReferences;
    // The cooked constants have zero-filled resource words. The renderer patches a working copy and restores from this copy on invalidation.
    MaterialTypedByteVector unpatchedConstantTypedBytes;
    MaterialTypedByteVector constantTypedBytes;
    MaterialTypedByteVector mutableDefaultTypedBytes;
    u32 shadingModelId = 0u;
    u32 surfaceDispatchId = 0u;
    // CSG caps evaluate the generated surface hook with this material's constants. Opaque stage shaders have no hook.
    b8 csgCapSurfaceDispatchAvailable = false;
    b8 csgCapSurfaceDispatchUnavailableLogged = false;
    b8 resourceReferencesResolved = false;
    // Refractive classifies RT casters; NwbMeshSurface owns IOR and shadow absorption values.
    b8 transparent = false;
    b8 twoSided = false;
    b8 refractive = false;


public:
    explicit MaterialSurfaceInfo(Core::Alloc::GlobalArena& arena)noexcept
        : shaderVariant(arena)
        , typedLayoutBlocks(arena)
        , typedLayoutFields(arena)
        , resourceReferences(arena)
        , unpatchedConstantTypedBytes(arena)
        , constantTypedBytes(arena)
        , mutableDefaultTypedBytes(arena)
    {}
};

struct MaterialPipelineResources{
    Core::GraphicsPipelineHandle indexedPipeline;
    Core::GraphicsPipelineHandle emulationPipeline;
    Core::MeshletPipelineHandle meshletPipeline;
    Core::ComputePipelineHandle computePipeline;
    Core::ComputePipelineHandle objectGeometryDecodePipeline;
    Core::ShaderHandle objectGeometryVertexShader;
    Core::ShaderHandle pixelShader;
    Core::ShaderHandle meshShader;
    Core::ShaderHandle computeShader;
    RenderPath::Enum renderPath = RenderPath::MeshShader;
    b8 sharedGeometryComputeProgram = false;
    b8 indexedGeometryOutput = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

