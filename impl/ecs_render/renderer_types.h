// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <core/assets/global.h>
#include <core/graphics/api.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_mesh_runtime/mesh.h>

#include <global/containers.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;
class Mesh;
struct MaterialPassDrawItem;


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

namespace MaterialPipelineCsgMode{
    enum Enum : u8{
        None,
        ClipOnly,
        ClipAndCapSource,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InstanceGpuData{
    Float4 rotation = Float4(0.f, 0.f, 0.f, 1.f);
    Float3UInt translation = Float3UInt(0.f, 0.f, 0.f, 0u);
    Float4 scale = Float4(1.f, 1.f, 1.f, 0.f);
};
static_assert(offsetof(InstanceGpuData, rotation) == sizeof(f32) * NWB_MESH_INSTANCE_ROTATION_FLOAT_OFFSET, "InstanceGpuData rotation must be first");
static_assert(offsetof(InstanceGpuData, translation) == sizeof(f32) * NWB_MESH_INSTANCE_TRANSLATION_FLOAT_OFFSET, "InstanceGpuData translation must follow rotation");
static_assert(
    offsetof(InstanceGpuData, translation) + offsetof(Float3UInt, w) == sizeof(f32) * NWB_MESH_INSTANCE_MATERIAL_MUTABLE_BYTE_OFFSET_FLOAT_OFFSET,
    "InstanceGpuData mutable offset must pack into translation.w"
);
static_assert(offsetof(InstanceGpuData, scale) == sizeof(f32) * NWB_MESH_INSTANCE_SCALE_FLOAT_OFFSET, "InstanceGpuData scale must follow translation payload");
static_assert(sizeof(InstanceGpuData) == sizeof(f32) * NWB_MESH_INSTANCE_FLOAT_COUNT, "InstanceGpuData stride must match the mesh shaders");
static_assert(alignof(InstanceGpuData) >= alignof(Float4), "InstanceGpuData must stay SIMD-aligned");

struct CsgReceiverRangeGpuData{
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
    u32 flags = 0u;
    u32 padding0 = 0u;
};

[[nodiscard]] inline Float34 MakeIdentityCsgMatrix(){
    Float34 matrix{};
    matrix.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    return matrix;
}

struct CsgCutterGpuData{
    u32 shapeType = 0u;
    u32 operation = 0u;
    u32 parameterByteOffset = 0u;
    u32 parameterByteSize = 0u;
    Float34 worldToShape = MakeIdentityCsgMatrix();
    Float34 shapeToWorld = MakeIdentityCsgMatrix();
    Float4 parameter0 = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 parameter1 = Float4(0.f, 0.f, 0.f, 0.f);
    f32 worldToShapeScaleBound = 1.f;
    f32 padding0 = 0.f;
    f32 padding1 = 0.f;
    f32 padding2 = 0.f;
};

static_assert(sizeof(CsgReceiverRangeGpuData) == sizeof(u32) * 4u, "CsgReceiverRangeGpuData layout must match the CSG shader");
static_assert(sizeof(CsgCutterGpuData) == sizeof(u32) * 4u + sizeof(Float34) * 2u + sizeof(Float4) * 3u, "CsgCutterGpuData layout must match the CSG shader");
static_assert(alignof(CsgCutterGpuData) >= alignof(Float4), "CsgCutterGpuData must stay SIMD-aligned");
static_assert(IsStandardLayout_V<CsgCutterGpuData>, "CsgCutterGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgCutterGpuData>, "CsgCutterGpuData must stay GPU-uploadable");

using CsgPlaneCapMeshVertex = RuntimeMeshCapSourceVertex;
using CsgPlaneCapMeshTriangle = RuntimeMeshCapSourceTriangle;

struct CsgPlaneCapVertexGpuData{
    Float4 positionReceiverIndex;
    Float4 normalCutterIndex;
    Float4 tangent;
    Float4 color;
    Float4 uv0;
};

struct CsgPlaneCapDrawItem{
    u32 firstVertex = 0u;
    u32 vertexCount = 0u;
};

static_assert(sizeof(CsgPlaneCapMeshVertex) == sizeof(Float4) * 5u, "CsgPlaneCapMeshVertex must stay tightly packed");
static_assert(sizeof(CsgPlaneCapMeshTriangle) == sizeof(CsgPlaneCapMeshVertex) * 3u, "CsgPlaneCapMeshTriangle must stay tightly packed");
static_assert(sizeof(CsgPlaneCapVertexGpuData) == sizeof(Float4) * 5u, "CsgPlaneCapVertexGpuData layout must match the CSG cap shaders");
static_assert(alignof(CsgPlaneCapMeshVertex) >= alignof(Float4), "CsgPlaneCapMeshVertex must stay SIMD-aligned");
static_assert(alignof(CsgPlaneCapMeshTriangle) >= alignof(Float4), "CsgPlaneCapMeshTriangle must stay SIMD-aligned");
static_assert(alignof(CsgPlaneCapVertexGpuData) >= alignof(Float4), "CsgPlaneCapVertexGpuData must stay SIMD-aligned");
static_assert(IsStandardLayout_V<CsgPlaneCapMeshVertex>, "CsgPlaneCapMeshVertex must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<CsgPlaneCapMeshVertex>, "CsgPlaneCapMeshVertex must stay GPU-friendly");
static_assert(IsStandardLayout_V<CsgPlaneCapMeshTriangle>, "CsgPlaneCapMeshTriangle must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<CsgPlaneCapMeshTriangle>, "CsgPlaneCapMeshTriangle must stay GPU-friendly");
static_assert(IsStandardLayout_V<CsgPlaneCapVertexGpuData>, "CsgPlaneCapVertexGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgPlaneCapVertexGpuData>, "CsgPlaneCapVertexGpuData must stay GPU-uploadable");
static_assert(IsStandardLayout_V<CsgPlaneCapDrawItem>, "CsgPlaneCapDrawItem must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgPlaneCapDrawItem>, "CsgPlaneCapDrawItem must stay cheap to pass by value");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialTypedByteVector = Vector<u8, Core::Alloc::GlobalArena>;
using MaterialTypedLayoutBlockVector = Vector<MaterialTypedLayoutBlock, Core::Alloc::GlobalArena>;
using MaterialTypedLayoutFieldVector = Vector<MaterialTypedLayoutField, Core::Alloc::GlobalArena>;
using MaterialPassDrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::ScratchArena>;
using InstanceGpuDataVector = Vector<InstanceGpuData, Core::Alloc::ScratchArena>;
using MaterialTypedByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;
using CsgReceiverRangeGpuDataVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::ScratchArena>;
using CsgCutterGpuDataVector = Vector<CsgCutterGpuData, Core::Alloc::ScratchArena>;
using CsgParameterByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;
using CsgPlaneCapMeshTriangleVector = Vector<CsgPlaneCapMeshTriangle, Core::Alloc::GlobalArena>;
using CsgPlaneCapVertexGpuDataVector = Vector<CsgPlaneCapVertexGpuData, Core::Alloc::ScratchArena>;
using CsgPlaneCapDrawItemVector = Vector<CsgPlaneCapDrawItem, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameGpuData{
    CsgReceiverRangeGpuDataVector receiverRanges;
    CsgCutterGpuDataVector cutters;
    CsgParameterByteDataVector parameterBytes;
    CsgPlaneCapVertexGpuDataVector planeCapVertices;
    CsgPlaneCapDrawItemVector opaquePlaneCapDrawItems;
    CsgPlaneCapDrawItemVector transparentPlaneCapDrawItems;

    explicit CsgFrameGpuData(Core::Alloc::ScratchArena& arena)
        : receiverRanges(arena)
        , cutters(arena)
        , parameterBytes(arena)
        , planeCapVertices(arena)
        , opaquePlaneCapDrawItems(arena)
        , transparentPlaneCapDrawItems(arena)
    {}

    [[nodiscard]] bool hasWork()const noexcept{ return !receiverRanges.empty() && !cutters.empty(); }
    [[nodiscard]] bool hasPlaneCapWork()const noexcept{ return !planeCapVertices.empty() && (!opaquePlaneCapDrawItems.empty() || !transparentPlaneCapDrawItems.empty()); }
    [[nodiscard]] bool hasOpaquePlaneCapWork()const noexcept{ return !planeCapVertices.empty() && !opaquePlaneCapDrawItems.empty(); }
    [[nodiscard]] bool hasTransparentPlaneCapWork()const noexcept{ return !planeCapVertices.empty() && !transparentPlaneCapDrawItems.empty(); }
    void reserve(const usize receiverCapacity, const usize cutterCapacity){
        receiverRanges.reserve(receiverCapacity);
        cutters.reserve(cutterCapacity);
        opaquePlaneCapDrawItems.reserve(cutterCapacity);
        transparentPlaneCapDrawItems.reserve(cutterCapacity);
    }
};


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
    CsgPlaneCapMeshTriangleVector csgPlaneCapTriangles;

    explicit MeshResources(Core::Alloc::GlobalArena& arena)
        : csgPlaneCapTriangles(arena)
    {}

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
    u32 csgCutterCount = 0u;
    bool csgGenerateCaps = false;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassDrawItems{
    MaterialPassDrawItemVector meshDrawItems;
    MaterialPassDrawItemVector computeDrawItems;

    explicit MaterialPassDrawItems(Core::Alloc::ScratchArena& arena)
        : meshDrawItems(arena)
        , computeDrawItems(arena)
    {}

    [[nodiscard]] bool empty()const noexcept{ return meshDrawItems.empty() && computeDrawItems.empty(); }
    void reserve(const usize capacity){
        meshDrawItems.reserve(capacity);
        computeDrawItems.reserve(capacity);
    }
};

struct MaterialPassDrawItemPartitions{
    MaterialPassDrawItems regular;
    MaterialPassDrawItems csg;

    explicit MaterialPassDrawItemPartitions(Core::Alloc::ScratchArena& arena)
        : regular(arena)
        , csg(arena)
    {}

    [[nodiscard]] bool empty()const noexcept{ return regular.empty() && csg.empty(); }
    void reserve(const usize capacity){
        regular.reserve(capacity);
        csg.reserve(capacity);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

