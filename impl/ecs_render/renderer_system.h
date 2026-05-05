// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <core/ecs/system.h>
#include <core/assets/asset_manager.h>
#include <core/graphics/graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialPipelinePass{
    enum Enum : u8{
        Opaque,
        WireframeOverlay,
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

namespace MeshSourceLayout{
    enum Enum : u32{
        GeometryVertex = 0u,
        DeformableVertex = 1u,
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


namespace MaterialParameterValueType{
    enum Enum : u32{
        None = 0,
        Float = 1,
        Int = 2,
        UInt = 3,
        Bool = 4,
    };
};

struct MaterialParameterGpuData{
    UInt4 meta = {};
    UInt4 data = {};
};
static_assert(sizeof(MaterialParameterGpuData) == sizeof(u32) * 8u, "MaterialParameterGpuData layout must match the mesh shaders");
static_assert(alignof(MaterialParameterGpuData) >= alignof(UInt4), "MaterialParameterGpuData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialParameterGpuData>, "MaterialParameterGpuData must stay cheap to upload");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RuntimeGeometryDesc{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    Core::Assets::AssetRef<Material> material;
    Name geometryKey = NAME_NONE;
    Core::BufferHandle shaderVertexBuffer;
    Core::BufferHandle shaderIndexBuffer;
    u32 indexCount = 0u;
    u32 sourceVertexLayout = MeshSourceLayout::GeometryVertex;
    u64 version = 0u;
    bool visible = true;

    [[nodiscard]] bool valid()const noexcept{
        return
            visible
            && entity.valid()
            && material.valid()
            && geometryKey != NAME_NONE
            && shaderVertexBuffer != nullptr
            && shaderIndexBuffer != nullptr
            && indexCount > 0u
            && (indexCount % 3u) == 0u
        ;
    }
};
using RuntimeGeometryVisitor = Function<void(const RuntimeGeometryDesc&)>;

class IRuntimeGeometryProvider{
public:
    virtual ~IRuntimeGeometryProvider() = default;

public:
    [[nodiscard]] virtual usize runtimeGeometryCandidateCount() = 0;
    virtual void forEachRuntimeGeometry(const RuntimeGeometryVisitor& visitor) = 0;
    [[nodiscard]] virtual bool containsRuntimeGeometry(const Name& geometryKey, u64 version) = 0;
};


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
    using RuntimeGeometryProviderAllocator = Core::Alloc::CustomAllocator<IRuntimeGeometryProvider*>;


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
        Core::Format::Enum depthFormat = Core::Format::UNKNOWN;
        Core::TextureHandle albedo;
        Core::TextureHandle depth;
        Core::FramebufferHandle framebuffer;
        Core::BindingSetHandle compositeBindingSet;
        AvboitFrameTargets avboit;

        [[nodiscard]] bool valid()const noexcept{
            return
                width > 0
                && height > 0
                && albedoFormat != Core::Format::UNKNOWN
                && depthFormat != Core::Format::UNKNOWN
                && albedo != nullptr
                && depth != nullptr
                && framebuffer != nullptr
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

    virtual void render(Core::IFramebuffer* framebuffer)override;
    virtual void backBufferResizing()override;
    virtual void backBufferResized(u32 width, u32 height, u32 sampleCount)override;


public:
    void registerRuntimeGeometryProvider(IRuntimeGeometryProvider& provider);
    void unregisterRuntimeGeometryProvider(IRuntimeGeometryProvider& provider);

public:
    void setWireframeOverlayEnabled(const bool enabled){ m_wireframeOverlayEnabled = enabled; }
    [[nodiscard]] bool wireframeOverlayEnabled()const{ return m_wireframeOverlayEnabled; }


private:
    [[nodiscard]] bool ensureGeometryLoaded(const Core::Assets::AssetRef<Geometry>& geometryAsset, GeometryResources*& outGeometry);
    [[nodiscard]] bool ensureRuntimeGeometryResources(const RuntimeGeometryDesc& desc, GeometryResources*& outGeometry);
    void pruneRuntimeGeometryResources();
    [[nodiscard]] bool ensureMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo);
    [[nodiscard]] bool ensureMeshShaderResources();
    [[nodiscard]] bool ensureComputeEmulationResources();
    [[nodiscard]] bool ensureMeshBindingSet(GeometryResources& geometry);
    [[nodiscard]] bool ensureComputeBindingSet(GeometryResources& geometry);
    [[nodiscard]] bool ensureDeferredFrameTargets(Core::IFramebuffer* presentationFramebuffer, DeferredFrameTargets*& outTargets);
    [[nodiscard]] bool ensureDeferredCompositeResources();
    [[nodiscard]] bool ensureDeferredCompositePipeline(Core::IFramebuffer* presentationFramebuffer);
    [[nodiscard]] bool ensureAvboitResources();
    [[nodiscard]] bool ensureAvboitPipelines(AvboitFrameTargets& targets);
    [[nodiscard]] bool ensureRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::IFramebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool hasTransparentRenderers();
    void resetDeferredFrameTargets(){ m_deferredTargets = DeferredFrameTargets{}; }
    void clearDeferredTargets(Core::ICommandList& commandList, DeferredFrameTargets& targets);
    void clearAvboitTargets(Core::ICommandList& commandList, AvboitFrameTargets& targets);
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
    [[nodiscard]] bool ensureInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] bool ensureMaterialParameterBufferCapacity(usize parameterCount);
    [[nodiscard]] bool ensureMeshViewBuffer(Core::ICommandList& commandList, f32 fallbackAspectRatio);
    [[nodiscard]] bool uploadInstanceBuffer(Core::ICommandList& commandList, const InstanceGpuDataVector& instanceData);
    [[nodiscard]] bool uploadMaterialParameterBuffer(Core::ICommandList& commandList, const MaterialParameterGpuDataVector& materialParameters);
    [[nodiscard]] bool ensureEmulationViewResources();
    void invalidateGeometryBindingSets();
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
    void setMaterialPassCommonBufferStates(Core::ICommandList& commandList, const GeometryResources& geometry);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    void renderAvboitPasses(Core::ICommandList& commandList, DeferredFrameTargets& targets);
    void dispatchAvboitDepthWarp(Core::ICommandList& commandList, AvboitFrameTargets& targets);
    void dispatchAvboitIntegration(Core::ICommandList& commandList, AvboitFrameTargets& targets);
    [[nodiscard]] bool renderDeferredComposite(Core::ICommandList& commandList, DeferredFrameTargets& targets, Core::IFramebuffer* presentationFramebuffer);
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);
    [[nodiscard]] bool ensureShaderLoaded(
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

    HashMap<Name, GeometryResources, Hasher<Name>, EqualTo<Name>, GeometryResourcesMapAllocator> m_geometryMeshes;
    HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, MaterialSurfaceInfoMapAllocator> m_materialSurfaceInfos;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo, MaterialPipelineMapAllocator> m_materialPipelines;
    HashMap<Name, RenderPath::Enum, Hasher<Name>, EqualTo<Name>, LoggedMaterialPathMapAllocator> m_loggedMaterialPaths;
    Vector<IRuntimeGeometryProvider*, RuntimeGeometryProviderAllocator> m_runtimeGeometryProviders;
    Core::BindingLayoutHandle m_meshBindingLayout;
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BindingLayoutHandle m_emulationViewBindingLayout;
    Core::BindingLayoutHandle m_deferredCompositeBindingLayout;
    Core::BindingLayoutHandle m_avboitEmptyBindingLayout;
    Core::BindingLayoutHandle m_avboitOccupancyBindingLayout;
    Core::BindingLayoutHandle m_avboitDepthWarpBindingLayout;
    Core::BindingLayoutHandle m_avboitExtinctionBindingLayout;
    Core::BindingLayoutHandle m_avboitIntegrateBindingLayout;
    Core::BindingLayoutHandle m_avboitAccumulateBindingLayout;
    Core::SamplerHandle m_deferredSampler;
    Core::SamplerHandle m_avboitLinearSampler;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialParameterBuffer;
    Core::BufferHandle m_meshViewBuffer;
    Core::BindingSetHandle m_emulationViewBindingSet;
    Core::ShaderHandle m_emulationVertexShader;
    Core::ShaderHandle m_deferredCompositeVertexShader;
    Core::ShaderHandle m_deferredCompositePixelShader;
    Core::ShaderHandle m_avboitOccupancyPixelShader;
    Core::ShaderHandle m_avboitDepthWarpComputeShader;
    Core::ShaderHandle m_avboitExtinctionPixelShader;
    Core::ShaderHandle m_avboitIntegrateComputeShader;
    Core::ShaderHandle m_avboitAccumulatePixelShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    Core::GraphicsPipelineHandle m_deferredCompositePipeline;
    Core::ComputePipelineHandle m_avboitDepthWarpPipeline;
    Core::ComputePipelineHandle m_avboitIntegratePipeline;
    DeferredFrameTargets m_deferredTargets;
    usize m_instanceBufferCapacity = 0;
    usize m_materialParameterBufferCapacity = 0;
    bool m_wireframeOverlayEnabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

