// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <core/assets/global.h>
#include <core/ecs/system.h>
#include <core/graphics/api.h>
#include <core/graphics/render_pass.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_mesh_runtime/mesh.h>


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
class Mesh;


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

namespace ECSRenderDetail{
    struct MaterialTypedInstanceRangeCollector;
};

struct InstanceGpuData{
    Float4 rotation = Float4(0.f, 0.f, 0.f, 1.f);
    Float3UInt translation = Float3UInt(0.f, 0.f, 0.f, 0u);
    Float4 scale = Float4(1.f, 1.f, 1.f, 0.f);
};
static_assert(offsetof(InstanceGpuData, rotation) == 0u, "InstanceGpuData rotation must be first");
static_assert(offsetof(InstanceGpuData, translation) == sizeof(f32) * 4u, "InstanceGpuData translation must follow rotation");
static_assert(offsetof(InstanceGpuData, translation) + offsetof(Float3UInt, w) == sizeof(f32) * 7u, "InstanceGpuData mutable offset must pack into translation.w");
static_assert(offsetof(InstanceGpuData, scale) == sizeof(f32) * 8u, "InstanceGpuData scale must follow translation payload");
static_assert(sizeof(InstanceGpuData) == sizeof(f32) * 12u, "InstanceGpuData stride must match the mesh shaders");
static_assert(alignof(InstanceGpuData) >= alignof(Float4), "InstanceGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
private:
    using MaterialTypedByteVector = Vector<u8, Core::Alloc::GlobalArena>;
    using MaterialTypedLayoutBlockVector = Vector<MaterialTypedLayoutBlock, Core::Alloc::GlobalArena>;
    using MaterialTypedLayoutFieldVector = Vector<MaterialTypedLayoutField, Core::Alloc::GlobalArena>;


private:
    struct MaterialPipelineKey{
        Name material = NAME_NONE;
        Core::FramebufferInfo framebufferInfo;
        MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
        bool twoSided = false;
    };
    struct MaterialPipelineKeyHasher{
        usize operator()(const MaterialPipelineKey& key)const;
    };
    struct MaterialPipelineKeyEqualTo{
        bool operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const;
    };

    struct MeshResources : public RuntimeMeshBuffers{
        Name meshName = NAME_NONE;
        Core::BufferHandle emulationVertexBuffer;
        Core::BindingSetHandle meshBindingSet;
        Core::BindingSetHandle computeBindingSet;
        u32 meshletCount = 0;
        u32 meshletPrimitiveIndexCount = 0;
        bool runtimeMesh = false;
        bool dynamicMeshletBoundsFresh = false;
        bool dynamicMeshletConesFresh = false;
        u64 runtimeMeshVersion = 0u;

        [[nodiscard]] bool valid()const noexcept{
            return
                meshName != NAME_NONE
                && buffersValid()
                && meshletCount > 0
                && meshletPrimitiveIndexCount > 0
            ;
        }
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

    struct MaterialPassDrawItem{
        Name meshKey = NAME_NONE;
        MaterialPipelineKey pipelineKey;
        u32 instanceIndex = 0;
        u32 materialConstantByteOffset = 0u;
        bool meshletConeCullScaleSafe = false;
    };

    struct MaterialInstanceMutableCacheEntry{
        Name materialName = NAME_NONE;
        Name materialInterface = NAME_NONE;
        u64 typedLayoutHash = 0u;
        u64 revision = 0u;
        MaterialTypedByteVector mutableTypedBytes;

        explicit MaterialInstanceMutableCacheEntry(Core::Alloc::GlobalArena& arena)
            : mutableTypedBytes(arena)
        {}
    };


private:
    using MaterialPassDrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::ScratchArena>;
    using InstanceGpuDataVector = Vector<InstanceGpuData, Core::Alloc::ScratchArena>;
    using MaterialTypedByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;

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
        Core::CommandList& commandList;
        Core::Framebuffer* framebuffer = nullptr;
        MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
        Core::BindingSet* passBindingSet = nullptr;
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
        Core::Alloc::GlobalArena& arena,
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
    virtual void render(Core::Framebuffer* framebuffer)override;

private:
    [[nodiscard]] bool createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh);
    [[nodiscard]] bool createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh);
    void pruneRuntimeMeshResources();
    void destroyMeshBindingSets();
    [[nodiscard]] bool createMeshBindingSet(MeshResources& mesh);
    [[nodiscard]] bool createComputeBindingSet(MeshResources& mesh);
    [[nodiscard]] bool meshFrameBindingResourcesReady(const tchar* context)const;
    [[nodiscard]] bool materialPassDrawResourcesReady(const MeshResources& mesh)const;
    [[nodiscard]] u32 meshDispatchFlags(
        const MeshResources& mesh,
        MaterialPipelinePass::Enum pass,
        bool twoSided,
        bool meshletConeCullScaleSafe
    )const;
    [[nodiscard]] u32 materialPassDrawDispatchFlags(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const MeshResources& mesh
    )const;
    void addMeshSourceBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const;
    void addMeshFrameBindingItems(Core::BindingSetDesc& bindingSetDesc)const;
    void addMeshDrawBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const;
    static void addMeshSourceBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc);
    static void addMeshFrameBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc);
    template<typename BindingHandler>
    static void forEachMeshSourceBindingSlot(BindingHandler&& handler){
        handler(s_MeshPositionBindingSlot, false);
        handler(s_MeshNormalBindingSlot, false);
        handler(s_MeshTangentBindingSlot, false);
        handler(s_MeshUv0BindingSlot, false);
        handler(s_MeshColorBindingSlot, false);
        handler(s_MeshletDescBindingSlot, false);
        handler(s_MeshletBoundsBindingSlot, true);
        handler(s_MeshletPositionRefBindingSlot, true);
        handler(s_MeshletAttributeRefBindingSlot, true);
        handler(s_MeshletLocalVertexRefBindingSlot, false);
        handler(s_MeshletPrimitiveIndexBindingSlot, true);
    }
    [[nodiscard]] static const Core::BufferHandle& meshSourceBuffer(const MeshResources& mesh, u32 bindingSlot){
        switch(bindingSlot){
        case s_MeshPositionBindingSlot: return mesh.positionBuffer;
        case s_MeshNormalBindingSlot: return mesh.normalBuffer;
        case s_MeshTangentBindingSlot: return mesh.tangentBuffer;
        case s_MeshUv0BindingSlot: return mesh.uv0Buffer;
        case s_MeshColorBindingSlot: return mesh.colorBuffer;
        case s_MeshletDescBindingSlot: return mesh.meshletDescBuffer;
        case s_MeshletBoundsBindingSlot: return mesh.meshletBoundsBuffer;
        case s_MeshletPositionRefBindingSlot: return mesh.meshletPositionRefDeltaBuffer;
        case s_MeshletAttributeRefBindingSlot: return mesh.meshletAttributeRefDeltaBuffer;
        case s_MeshletLocalVertexRefBindingSlot: return mesh.meshletLocalVertexRefBuffer;
        case s_MeshletPrimitiveIndexBindingSlot: return mesh.meshletPrimitiveIndexBuffer;
        default:
            NWB_ASSERT(false);
            return mesh.positionBuffer;
        }
    }
    template<typename BufferHandler>
    static void forEachMeshSourceBuffer(const MeshResources& mesh, BufferHandler&& handler){
        forEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool rawView){
            handler(bindingSlot, meshSourceBuffer(mesh, bindingSlot), rawView);
        });
    }

private:
    static constexpr u32 s_MeshPositionBindingSlot = 0u;
    static constexpr u32 s_MeshNormalBindingSlot = 1u;
    static constexpr u32 s_MeshTangentBindingSlot = 2u;
    static constexpr u32 s_MeshUv0BindingSlot = 3u;
    static constexpr u32 s_MeshColorBindingSlot = 4u;
    static constexpr u32 s_MeshletDescBindingSlot = 5u;
    static constexpr u32 s_MeshletBoundsBindingSlot = 6u;
    static constexpr u32 s_MeshletPositionRefBindingSlot = 7u;
    static constexpr u32 s_MeshletAttributeRefBindingSlot = 8u;
    static constexpr u32 s_MeshletLocalVertexRefBindingSlot = 9u;
    static constexpr u32 s_MeshletPrimitiveIndexBindingSlot = 10u;
    static constexpr u32 s_MeshInstanceBindingSlot = 11u;
    static constexpr u32 s_MeshViewBindingSlot = 12u;
    static constexpr u32 s_MeshMaterialTypedBindingSlot = 13u;
    static constexpr u32 s_MeshGeneratedVertexBindingSlot = 14u;

private:
    [[nodiscard]] static bool splitMaterialTypedBytesByClass(
        const Material& material,
        const Name& materialPath,
        MaterialTypedByteVector& outConstantTypedBytes,
        MaterialTypedByteVector& outMutableDefaultTypedBytes
    );
    [[nodiscard]] bool createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo);
    [[nodiscard]] bool createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool hasTransparentRenderers();
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);

private:
    [[nodiscard]] bool createMeshShaderResources();
    [[nodiscard]] bool createComputeEmulationResources();
    [[nodiscard]] bool createEmulationViewResources();
    [[nodiscard]] bool updateMeshViewBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    void renderMaterialPass(
        Core::CommandList& commandList,
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        Core::BindingSet* passBindingSet,
        const AvboitFrameTargets* avboitTargets
    );
    void gatherMaterialPassDrawItems(
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        MaterialPassDrawItemVector& meshDrawItems,
        MaterialPassDrawItemVector& computeDrawItems,
        InstanceGpuDataVector& instanceData,
        ECSRenderDetail::MaterialTypedInstanceRangeCollector& materialTypedRanges,
        MaterialTypedByteDataVector& materialTypedBytes
    );
    struct MaterialInstanceOverrideField{
        const MaterialTypedLayoutField* field = nullptr;
        u32 blockByteBegin = 0u;
        bool mutableBlock = false;
    };
    [[nodiscard]] static bool findMaterialInstanceOverrideField(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceParameter& parameter,
        MaterialInstanceOverrideField& outField
    );
    [[nodiscard]] static bool applyMaterialInstanceOverrides(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceComponent& materialInstance,
        MaterialTypedByteDataVector& inOutMutableTypedBytes
    );
    [[nodiscard]] bool resolveMaterialInstanceMutableTypedBytes(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceComponent* materialInstance,
        const MaterialTypedByteVector*& outMutableTypedBytes
    );
    void pruneMaterialInstanceMutableCache();
    void setMaterialPassCommonBufferStates(Core::CommandList& commandList, const MeshResources& mesh);
    void setMaterialPassDrawPushConstants(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const MeshResources& mesh
    );
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);

private:
    [[nodiscard]] bool reserveInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] bool reserveMaterialTypedBufferCapacity(usize byteCount);
    [[nodiscard]] bool uploadInstanceBuffer(Core::CommandList& commandList, const InstanceGpuDataVector& instanceData);
    [[nodiscard]] bool uploadMaterialTypedBuffer(
        Core::CommandList& commandList,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool uploadMaterialPassDrawBuffers(
        Core::CommandList& commandList,
        const InstanceGpuDataVector& instanceData,
        const ECSRenderDetail::MaterialTypedInstanceRangeCollector& materialTypedRanges,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool findMaterialPassDrawItemResources(
        const MaterialPassDrawItem& drawItem,
        MeshResources*& outMesh,
        MaterialPipelineResources*& outPipelineResources
    );
    template<typename DrawItemHandler>
    void forEachMaterialPassDrawItemResources(const MaterialPassDrawItemVector& drawItems, DrawItemHandler&& handler){
        for(const MaterialPassDrawItem& drawItem : drawItems){
            MeshResources* mesh = nullptr;
            MaterialPipelineResources* pipelineResources = nullptr;
            if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
                continue;

            handler(drawItem, *mesh, *pipelineResources);
        }
    }

private:
    [[nodiscard]] bool updateSceneShadingBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    [[nodiscard]] bool createDeferredLightingResources();
    [[nodiscard]] bool createDeferredLightingPipeline(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets);

private:
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline(Core::Framebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer);

private:
    [[nodiscard]] bool createAvboitResources();
    [[nodiscard]] bool createAvboitPipelines();
    [[nodiscard]] bool createAvboitFrameTargets(
        DeferredFrameTargets& createdTargets,
        Core::Format::Enum lowRasterFormat,
        Core::Format::Enum accumColorFormat,
        Core::Format::Enum accumExtinctionFormat,
        Core::Format::Enum transmittanceFormat
    );
    void clearAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void renderAvboitPasses(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets);


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
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

private:
    HashMap<Name, MeshResources, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_meshMeshes;


private:
    HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_materialSurfaceInfos;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo, Core::Alloc::GlobalArena> m_materialPipelines;
    HashMap<Core::ECS::EntityID, MaterialInstanceMutableCacheEntry, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_materialInstanceMutableCache;
    HashMap<Name, RenderPath::Enum, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_loggedMaterialPaths;

private:
    Core::BindingLayoutHandle m_meshBindingLayout;
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BindingLayoutHandle m_emulationViewBindingLayout;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialTypedBuffer;
    Core::BufferHandle m_meshViewBuffer;
    Core::BindingSetHandle m_emulationViewBindingSet;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    usize m_instanceBufferCapacity = 0;
    usize m_materialTypedBufferCapacity = 0;

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

