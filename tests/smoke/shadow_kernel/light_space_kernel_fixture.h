// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "shadow_kernel_fixture.h"

#include <impl/assets/graphics/shadow/light_space_constants.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LightSpaceKernel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MapSize = 32u;
constexpr u32 s_FullWidth = 3u;
constexpr u32 s_FullHeight = 3u;
constexpr u32 s_OutputSize = 4u;
constexpr u32 s_OutputLayer = 1u;

struct Node{ Float3U minimum; u32 left; Float3U maximum; u32 right; };
struct Instance{ Float4U inverseRows[3]; u32 meshIndex = 0u; u32 primitiveCount = 12u; u32 reserved[2]{}; };
struct Material{
    u32 modelId = 0u;
    u32 flags = 1u;
    u32 shadingModelId = 0u;
    u32 materialOffset = 0u;
    u32 meshInstanceIndex = 0u;
    u32 indexSlot = 0u;
    u32 attributeSlot = 0u;
    u32 positionSlot = 0u;
    u32 nodeSlot = 0u;
};
struct Attribute{ u16 normal[4]{}; Float2U uv{}; };
struct Context{ u32 scene[4]{}; u32 material[4]{}; };
struct Deferred{ u32 slots[11][4]{}; };
struct Light{ Float4U position; Float4U direction; Float4U color; Float4U params; Float4U size; };
struct View{ Float4U rows[4]{}; Float4U origin{}; Float4U depth{}; u32 map[4]{}; u32 light[4]{}; };
struct Event{ f32 depth; u32 instanceAndFacing; u32 primitive; u32 absorptionRG; u32 absorptionBInterface; };
struct Push{
    u32 viewSlot = 0u;
    u32 viewIndex = 0u;
    u32 materialContextSlot = 0u;
    u32 countsSlot = 0u;
    u32 eventsSlot = 0u;
    u32 depthSlot = 0u;
    u32 instanceIndex = 0u;
    u32 instanceCount = 0u;
    u32 width = s_FullWidth;
    u32 height = s_FullHeight;
    u32 frameIndex = 7u;
    u32 sampleCount = 3u;
    u32 deferredResourcesSlot = 0u;
    u32 outputSlot = 0u;
    u32 sceneRootSlot = 0u;
    u32 viewCount = 0u;
};
struct Sample{
    Float3U direction;
    f32 maximum;
    Float3U software;
    u32 face;
    Float3U selected;
    u32 accepted;
    Float3U reprojected;
    u32 reprojectedAccepted;
};
struct Observation{
    Float3U software;
    u32 mapReady;
    Float3U mapped;
    u32 count;
    u32 pixelIndex;
    u32 face;
    u32 viewReady;
    u32 projected;
    f32 blockerDistance;
    u32 blockerReady;
    u32 faceMask;
    u32 fallbackMask;
    Sample samples[3];
};
struct Half4{ u16 values[4]; };
struct CaptureDraw{ u32 vertexCount; u32 instanceCount; u32 firstVertex; u32 firstInstance; };
namespace ReceiverExpectation{ enum Enum : u8{ None, Lit, Blocked, Singular }; };
namespace SoftExpectation{ enum Enum : u8{ None, Lit, Blocked, Interior, Edge, Seam, MissingNeighbor }; };
struct Quality{
    bool penumbra = false;
    bool geometricPenumbra = false;
    u32 faceMask = 0u;
    u32 fallbackMask = 0u;
};
struct Case{
    AStringView label;
    u32 count = 1u;
    bool point = false;
    u32 face = 4u;
    bool opaque = false;
    bool varyingThickness = false;
    bool background = false;
    bool invalidView = false;
    bool missingFace = false;
    bool corruptEvent = false;
    i32 seam = 0;
    f32 thickness = 1.0f;
    u32 sampleCount = 3u;
    bool asymmetric = false;
    bool closed = false;
    bool inside = false;
    bool nearClip = false;
    bool invalidFit = false;
    f32 sourceSize = 0.0f;
    f32 receiverX = 0.0f;
    u32 frameIndex = 7u;
    SoftExpectation::Enum softExpectation = SoftExpectation::None;
    ReceiverExpectation::Enum receiverExpectation = ReceiverExpectation::None;
    f32 receiverSlopeX = 0.8f;
    f32 receiverSlopeY = 0.35f;
    f32 receiverGap = 0.01f;
    bool receiverHalf = true;
    bool corruptNeighbor = false;
};
struct Inputs{
    Vector<Float3U, Alloc::ScratchArena> positions;
    Vector<u32, Alloc::ScratchArena> indices;
    Vector<Attribute, Alloc::ScratchArena> attributes;
    Vector<Node, Alloc::ScratchArena> mesh;
    Vector<Node, Alloc::ScratchArena> scene;
    Vector<Instance, Alloc::ScratchArena> instances;
    Vector<Impl::InstanceGpuData, Alloc::ScratchArena> transforms;
    Vector<Material, Alloc::ScratchArena> materials;
    Float3U localMinimum{};
    Float3U localMaximum{};
    Float3U receiver{};
    Float3U normal{};
    Float3U encodedNormal{};
    Light light{};

    explicit Inputs(Alloc::ScratchArena& arena);
};
struct Programs{
    BindingLayoutHandle layout;
    ShaderHandle vertex;
    ShaderHandle pixel;
    ComputePipelineHandle view;
    ComputePipelineHandle shade;
    ComputePipelineHandle poison;
    ComputePipelineHandle cull;
    ComputePipelineHandle resolve[2];
    ComputePipelineHandle fallback[2];
    ComputePipelineHandle observe[2];
};

void BuildInputs(const Case& testCase, Inputs& input, Alloc::ScratchArena& scratchArena);
[[nodiscard]] Float3U Expected(const Case& testCase, const Inputs& input, bool software = false);
void CheckCaptureParity(GraphicsBackend::Device& device, Buffer& referenceCounts, Buffer& referenceEvents,
    StagingTexture& referenceDepth, Buffer& counts, Buffer& events, StagingTexture& depth,
    u32 pixelCount, u32 viewCount, bool corruptedEvents);
[[nodiscard]] Float3U ExpectedRay(const Case& testCase, const Inputs& input, const Float3U& direction,
    f64 maximum, bool software);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LightSpaceKernelTest : public DescriptorBufferRoundTripTest{
protected:
    static void SetUpTestSuite();
    [[nodiscard]] bool loadPrograms(Alloc::ScratchArena& scratchArena, LightSpaceKernel::Programs& programs);
    void runCullCases(const LightSpaceKernel::Programs& programs);
    void runCase(const LightSpaceKernel::Case& testCase, const LightSpaceKernel::Programs& programs,
        LightSpaceKernel::Quality* quality = nullptr);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

