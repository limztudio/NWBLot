// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <core/assets/global.h>
#include <core/ecs/system.h>
#include <core/graphics/common.h>
#include <core/graphics/render_pass.h>
#include <impl/assets_material/material_asset.h>
#include <impl/ecs_geometry/runtime_geometry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;
class Geometry;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialPipelinePass{
    enum Enum : u8{
        Opaque,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InstanceGpuData{
    Float4 rotation = Float4(0.f, 0.f, 0.f, 1.f);
    Float4 translation = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 scale = Float4(1.f, 1.f, 1.f, 0.f);
    UInt4 materialParameters = {};
};
static_assert(sizeof(InstanceGpuData) == sizeof(f32) * 16u, "InstanceGpuData layout must match the mesh shaders");
static_assert(alignof(InstanceGpuData) >= alignof(Float4), "InstanceGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
private:
    using MaterialParameterVectorAllocator = Core::Alloc::CustomAllocator<MaterialParameterGpuData>;
    using MaterialParameterVector = Vector<MaterialParameterGpuData, MaterialParameterVectorAllocator>;


private:
    struct MaterialPipelineKey{
        Name material = NAME_NONE;
        Core::FramebufferInfo framebufferInfo;
        MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
    };
    struct MaterialPipelineKeyHasher{
        usize operator()(const MaterialPipelineKey& key)const;
    };
    struct MaterialPipelineKeyEqualTo{
        bool operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const;
    };

    struct GeometryResources{
        Name geometryName = NAME_NONE;
        Core::BufferHandle shaderVertexBuffer;
        Core::BufferHandle shaderIndexBuffer;
        Core::BindingSetHandle meshBindingSet;
        Core::BufferHandle emulationVertexBuffer;
        Core::BindingSetHandle computeBindingSet;
        u32 indexCount = 0;
        u32 triangleCount = 0;
        u32 dispatchGroupCount = 0;
        u32 sourceVertexLayout = 0;
        bool runtimeGeometry = false;
        u64 runtimeGeometryVersion = 0u;

        [[nodiscard]] bool valid()const noexcept{
            return
                geometryName != NAME_NONE
                && shaderVertexBuffer != nullptr
                && shaderIndexBuffer != nullptr
                && indexCount > 0
                && triangleCount > 0
                && dispatchGroupCount > 0
            ;
        }
    };

    struct MaterialSurfaceInfo{
        Name materialName = NAME_NONE;
        AString shaderVariant;
        Core::Assets::AssetRef<Shader> pixelShader;
        Core::Assets::AssetRef<Shader> meshShader;
        MaterialParameterVector parameters;
        f32 alpha = 1.f;
        bool transparent = false;
        bool valid = false;

        explicit MaterialSurfaceInfo(Core::Alloc::CustomArena& arena)
            : parameters(MaterialParameterVectorAllocator(arena))
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

    struct MaterialPassDrawItem{
        Name geometryKey = NAME_NONE;
        MaterialPipelineKey pipelineKey;
        u32 instanceIndex = 0;
        f32 alpha = 1.f;
    };


private:
    using MaterialPassDrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::ScratchAllocator<MaterialPassDrawItem>>;
    using InstanceGpuDataVector = Vector<InstanceGpuData, Core::Alloc::ScratchAllocator<InstanceGpuData>>;
    using MaterialParameterGpuDataVector = Vector<MaterialParameterGpuData, Core::Alloc::ScratchAllocator<MaterialParameterGpuData>>;

    using GeometryResourcesMapAllocator = Core::Alloc::CustomAllocator<Pair<const Name, GeometryResources>>;
    using MaterialSurfaceInfoMapAllocator = Core::Alloc::CustomAllocator<Pair<const Name, MaterialSurfaceInfo>>;
    using MaterialPipelineMapAllocator = Core::Alloc::CustomAllocator<Pair<const MaterialPipelineKey, MaterialPipelineResources>>;
    using LoggedMaterialPathMapAllocator = Core::Alloc::CustomAllocator<Pair<const Name, RenderPath::Enum>>;
public:
    struct AvboitFrameTargets{
        u32 fullWidth = 0;
        u32 fullHeight = 0;
        u32 lowWidth = 0;
        u32 lowHeight = 0;
        u32 virtualSliceCount = 0;
        u32 physicalSliceCount = 0;
        Core::Format::Enum lowRasterFormat = Core::Format::UNKNOWN;
        Core::Format::Enum accumColorFormat = Core::Format::UNKNOWN;
        Core::Format::Enum accumExtinctionFormat = Core::Format::UNKNOWN;
        Core::Format::Enum transmittanceFormat = Core::Format::UNKNOWN;
        Core::TextureHandle lowRasterTarget;
        Core::TextureHandle accumColor;
        Core::TextureHandle accumExtinction;
        Core::TextureHandle transmittanceTexture;
        Core::FramebufferHandle lowFramebuffer;
        Core::FramebufferHandle accumulationFramebuffer;
        Core::BufferHandle coverageBuffer;
        Core::BufferHandle depthWarpBuffer;
        Core::BufferHandle controlBuffer;
        Core::BufferHandle extinctionBuffer;
        Core::BufferHandle extinctionOverflowBuffer;
        Core::BindingSetHandle occupancyBindingSet;
        Core::BindingSetHandle depthWarpBindingSet;
        Core::BindingSetHandle extinctionBindingSet;
        Core::BindingSetHandle integrateBindingSet;
        Core::BindingSetHandle accumulateBindingSet;

        [[nodiscard]] bool valid()const noexcept{
            return
                fullWidth > 0
                && fullHeight > 0
                && lowWidth > 0
                && lowHeight > 0
                && virtualSliceCount > 0
                && physicalSliceCount > 0
                && lowRasterFormat != Core::Format::UNKNOWN
                && accumColorFormat != Core::Format::UNKNOWN
                && accumExtinctionFormat != Core::Format::UNKNOWN
                && transmittanceFormat != Core::Format::UNKNOWN
                && lowRasterTarget != nullptr
                && accumColor != nullptr
                && accumExtinction != nullptr
                && transmittanceTexture != nullptr
                && lowFramebuffer != nullptr
                && accumulationFramebuffer != nullptr
                && coverageBuffer != nullptr
                && depthWarpBuffer != nullptr
                && controlBuffer != nullptr
                && extinctionBuffer != nullptr
                && extinctionOverflowBuffer != nullptr
                && occupancyBindingSet != nullptr
                && depthWarpBindingSet != nullptr
                && extinctionBindingSet != nullptr
                && integrateBindingSet != nullptr
                && accumulateBindingSet != nullptr
            ;
        }
    };

private:
    struct MaterialPassDrawContext{
        Core::ICommandList& commandList;
        Core::IFramebuffer* framebuffer = nullptr;
        MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
        Core::IBindingSet* passBindingSet = nullptr;
        const AvboitFrameTargets* avboitTargets = nullptr;
        const Core::ViewportState& viewportState;
    };

    struct DeferredFrameTargets{
        u32 width = 0;
        u32 height = 0;
        Core::Format::Enum albedoFormat = Core::Format::UNKNOWN;
        Core::Format::Enum normalFormat = Core::Format::UNKNOWN;
        Core::Format::Enum worldPositionFormat = Core::Format::UNKNOWN;
        Core::Format::Enum opaqueColorFormat = Core::Format::UNKNOWN;
        Core::Format::Enum depthFormat = Core::Format::UNKNOWN;
        Core::TextureHandle albedo;
        Core::TextureHandle normal;
        Core::TextureHandle worldPosition;
        Core::TextureHandle opaqueColor;
        Core::TextureHandle depth;
        Core::FramebufferHandle framebuffer;
        Core::FramebufferHandle opaqueLightingFramebuffer;
        Core::BindingSetHandle lightingBindingSet;
        Core::BindingSetHandle compositeBindingSet;
        AvboitFrameTargets avboit;

        [[nodiscard]] bool valid()const noexcept{
            return
                width > 0
                && height > 0
                && albedoFormat != Core::Format::UNKNOWN
                && normalFormat != Core::Format::UNKNOWN
                && worldPositionFormat != Core::Format::UNKNOWN
                && opaqueColorFormat != Core::Format::UNKNOWN
                && depthFormat != Core::Format::UNKNOWN
                && albedo != nullptr
                && normal != nullptr
                && worldPosition != nullptr
                && opaqueColor != nullptr
                && depth != nullptr
                && framebuffer != nullptr
                && opaqueLightingFramebuffer != nullptr
                && lightingBindingSet != nullptr
                && compositeBindingSet != nullptr
                && avboit.valid()
            ;
        }
    };


public:
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;


public:
    RendererSystem(
        Core::Alloc::CustomArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~RendererSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;

    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual void render(Core::IFramebuffer* framebuffer)override;


private:
    [[nodiscard]] bool createGeometryResources(const Core::Assets::AssetRef<Geometry>& geometryAsset, GeometryResources*& outGeometry);
    [[nodiscard]] bool createRuntimeGeometryResources(const RuntimeGeometryDesc& desc, GeometryResources*& outGeometry);
    void pruneRuntimeGeometryResources();
    void destroyGeometryBindingSets();
    [[nodiscard]] bool createMeshBindingSet(GeometryResources& geometry);
    [[nodiscard]] bool createComputeBindingSet(GeometryResources& geometry);

private:
    [[nodiscard]] bool createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo);
    [[nodiscard]] bool createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::IFramebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool hasTransparentRenderers();
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);

private:
    [[nodiscard]] bool createMeshShaderResources();
    [[nodiscard]] bool createComputeEmulationResources();
    [[nodiscard]] bool createEmulationViewResources();
    [[nodiscard]] bool updateMeshViewBuffer(Core::ICommandList& commandList, f32 fallbackAspectRatio);
    void renderMaterialPass(
        Core::ICommandList& commandList,
        Core::IFramebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        Core::IBindingSet* passBindingSet,
        const AvboitFrameTargets* avboitTargets
    );
    void gatherMaterialPassDrawItems(
        Core::IFramebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        MaterialPassDrawItemVector& meshDrawItems,
        MaterialPassDrawItemVector& computeDrawItems,
        InstanceGpuDataVector& instanceData,
        MaterialParameterGpuDataVector& materialParameters
    );
    void setMaterialPassCommonBufferStates(Core::ICommandList& commandList, const GeometryResources& geometry);
    void setMaterialPassDrawPushConstants(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const GeometryResources& geometry
    );
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);

private:
    [[nodiscard]] bool reserveInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] bool reserveMaterialParameterBufferCapacity(usize parameterCount);
    [[nodiscard]] bool uploadInstanceBuffer(Core::ICommandList& commandList, const InstanceGpuDataVector& instanceData);
    [[nodiscard]] bool uploadMaterialParameterBuffer(Core::ICommandList& commandList, const MaterialParameterGpuDataVector& materialParameters);
    [[nodiscard]] bool findMaterialPassDrawItemResources(
        const MaterialPassDrawItem& drawItem,
        GeometryResources*& outGeometry,
        MaterialPipelineResources*& outPipelineResources
    );
    template<typename DrawItemHandler>
    void forEachMaterialPassDrawItemResources(const MaterialPassDrawItemVector& drawItems, DrawItemHandler&& handler){
        for(const MaterialPassDrawItem& drawItem : drawItems){
            GeometryResources* geometry = nullptr;
            MaterialPipelineResources* pipelineResources = nullptr;
            if(!findMaterialPassDrawItemResources(drawItem, geometry, pipelineResources))
                continue;

            handler(drawItem, *geometry, *pipelineResources);
        }
    }

private:
    [[nodiscard]] bool updateSceneShadingBuffer(Core::ICommandList& commandList, f32 fallbackAspectRatio);
    [[nodiscard]] bool createDeferredLightingResources();
    [[nodiscard]] bool createDeferredLightingPipeline(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredLighting(Core::ICommandList& commandList, DeferredFrameTargets& targets);

private:
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline(Core::IFramebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearDeferredTargets(Core::ICommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredComposite(Core::ICommandList& commandList, DeferredFrameTargets& targets, Core::IFramebuffer* presentationFramebuffer);

private:
    [[nodiscard]] bool createAvboitResources();
    [[nodiscard]] bool createAvboitPipelines(AvboitFrameTargets& targets);
    [[nodiscard]] bool createAvboitFrameTargets(
        DeferredFrameTargets& createdTargets,
        Core::Format::Enum lowRasterFormat,
        Core::Format::Enum accumColorFormat,
        Core::Format::Enum accumExtinctionFormat,
        Core::Format::Enum transmittanceFormat
    );
    void clearAvboitTargets(Core::ICommandList& commandList, AvboitFrameTargets& targets);
    void renderAvboitPasses(Core::ICommandList& commandList, DeferredFrameTargets& targets);
    void dispatchAvboitDepthWarp(Core::ICommandList& commandList, AvboitFrameTargets& targets);
    void dispatchAvboitIntegration(Core::ICommandList& commandList, AvboitFrameTargets& targets);


private:
    [[nodiscard]] bool loadDeferredCompositeVertexShader();
    [[nodiscard]] bool loadShader(
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        AStringView variantName,
        Core::ShaderType::Mask shaderType,
        const Name& debugName,
        const Name* archiveStageName = nullptr
    );


private:
    Core::Alloc::CustomArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

private:
    HashMap<Name, GeometryResources, Hasher<Name>, EqualTo<Name>, GeometryResourcesMapAllocator> m_geometryMeshes;


private:
    HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, MaterialSurfaceInfoMapAllocator> m_materialSurfaceInfos;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo, MaterialPipelineMapAllocator> m_materialPipelines;
    HashMap<Name, RenderPath::Enum, Hasher<Name>, EqualTo<Name>, LoggedMaterialPathMapAllocator> m_loggedMaterialPaths;

private:
    Core::BindingLayoutHandle m_meshBindingLayout;
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BindingLayoutHandle m_emulationViewBindingLayout;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialParameterBuffer;
    Core::BufferHandle m_meshViewBuffer;
    Core::BindingSetHandle m_emulationViewBindingSet;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    usize m_instanceBufferCapacity = 0;
    usize m_materialParameterBufferCapacity = 0;

private:
    Core::BindingLayoutHandle m_deferredLightingBindingLayout;
    Core::BufferHandle m_sceneShadingBuffer;
    Core::ShaderHandle m_deferredCompositeVertexShader;
    Core::ShaderHandle m_deferredLightingPixelShader;
    Core::GraphicsPipelineHandle m_deferredLightingPipeline;

private:
    Core::BindingLayoutHandle m_deferredCompositeBindingLayout;
    Core::SamplerHandle m_deferredSampler;
    Core::ShaderHandle m_deferredCompositePixelShader;
    Core::GraphicsPipelineHandle m_deferredCompositePipeline;
    DeferredFrameTargets m_deferredTargets;

private:
    Core::BindingLayoutHandle m_avboitEmptyBindingLayout;
    Core::BindingLayoutHandle m_avboitOccupancyBindingLayout;
    Core::BindingLayoutHandle m_avboitDepthWarpBindingLayout;
    Core::BindingLayoutHandle m_avboitExtinctionBindingLayout;
    Core::BindingLayoutHandle m_avboitIntegrateBindingLayout;
    Core::BindingLayoutHandle m_avboitAccumulateBindingLayout;
    Core::SamplerHandle m_avboitLinearSampler;
    Core::ShaderHandle m_avboitOccupancyPixelShader;
    Core::ShaderHandle m_avboitDepthWarpComputeShader;
    Core::ShaderHandle m_avboitExtinctionPixelShader;
    Core::ShaderHandle m_avboitIntegrateComputeShader;
    Core::ShaderHandle m_avboitAccumulatePixelShader;
    Core::ComputePipelineHandle m_avboitDepthWarpPipeline;
    Core::ComputePipelineHandle m_avboitIntegratePipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

