// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../descriptor_buffer/round_trip/round_trip_fixture.h"

#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_surfel_gather_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Pattern{
enum Enum : u8{
    Empty, Dead, Unsampled, Opposed, Disoccluded, Constant, Directional, Weighted,
    CapRejectsTail, CapIgnoresNonfiniteTail, Cycle, ZeroWeightNaN, ZeroWeightInfinity,
    Infinity, HalfOverflow, Biased, TinyRadius, NearCoverageThreshold, BoundedHead, BoundedNext,
};
};

struct Surfel{
    Float3U position{};
    f32 radius = 0.9f;
    Float3U normal{ 0.0f, 0.0f, 1.0f };
    u32 nextInCell = NWB_SURFEL_CELL_INVALID;
    Float4U shR{ 4.0f, 0.0f, 0.0f, 0.0f };
    Float4U shG{ 2.0f, 0.0f, 0.0f, 0.0f };
    Float4U shB{ 1.0f, 0.0f, 0.0f, 0.0f };
    u32 sampleCount = 8u;
    u32 lastSeenFrame = 0u;
    u32 alive = 1u;
    u32 padding = 0u;
};

struct Constants{
    Float4U cameraPositionCellSize{ 0.0f, 0.0f, 0.0f, 0.6f };
    Float4U hashPoolFrameDivisor{};
    Float4U coverageRadiusBiasHyst{};
    Float4U ageRaysTileScreen{};
    Float4U screenHeightPad{};
};

struct Query{
    Float3U position{};
    u32 padding = 0u;
    Float3U normal{ 0.0f, 0.0f, 1.0f };
    u32 capacity = 0u;
};

struct Observation{
    u32 covered;
    u32 r;
    u32 g;
    u32 b;
};

struct PushConstants{
    u32 constantsHeapSlot;
    u32 poolHeapSlot;
    u32 cellHeadHeapSlot;
    u32 queryHeapSlot;
    u32 outputRecordOffset;
    u32 unused[7]{};
    u32 outputHeapSlot;
    u32 queryCount;
};

struct Case{
    AStringView label;
    Pattern::Enum pattern;
    u32 cellCount = 1u;
    u32 poolCount = 1u;
    bool boundedOnly = false;
};

using Surfels = Vector<Surfel, Alloc::ScratchArena>;
using Heads = Vector<u32, Alloc::ScratchArena>;
using Queries = Vector<Query, Alloc::ScratchArena>;

constexpr u32 s_QueryCount = 65u;
constexpr u32 s_OutputStride = 128u;
constexpr u32 s_PipelineCount = 4u;
constexpr u32 s_GuardWord = 0x5a5aa5a5u;
constexpr Observation s_Guard{ s_GuardWord, s_GuardWord, s_GuardWord, s_GuardWord };

static_assert(sizeof(Surfel) == NWB_SURFEL_RECORD_SIZE);
static_assert(offsetof(Surfel, nextInCell) == 28u);
static_assert(offsetof(Surfel, shR) == 32u);
static_assert(offsetof(Surfel, sampleCount) == 80u);
static_assert(sizeof(Constants) == NWB_SURFEL_CONSTANTS_FLOAT4_COUNT * sizeof(Float4U));
static_assert(sizeof(Query) == 32u);
static_assert(sizeof(Observation) == 16u);
static_assert(sizeof(PushConstants) == 56u);
static_assert(offsetof(PushConstants, outputHeapSlot) == 48u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildInputs(const Case& testCase, Surfels& pool, Heads& heads, Queries& queries, Constants& constants){
    pool.resize(testCase.poolCount);
    heads.resize(testCase.cellCount, 0u);
    queries.resize(s_QueryCount);
    constants.hashPoolFrameDivisor = { static_cast<f32>(testCase.cellCount), static_cast<f32>(testCase.poolCount), 0.0f, 1.0f };
    for(u32 index = 0u; index < testCase.poolCount; ++index)
        pool[index].nextInCell = index + 1u < testCase.poolCount ? index + 1u : NWB_SURFEL_CELL_INVALID;
    for(u32 index = 0u; index < s_QueryCount; ++index){
        Query& query = queries[index];
        query.capacity = testCase.poolCount;
        if(index == 0u)
            continue;
        query.position = {
            static_cast<f32>(static_cast<i32>(index % 9u) - 4) * 0.1375f,
            static_cast<f32>(static_cast<i32>((index / 9u) % 7u) - 3) * 0.1875f,
            static_cast<f32>(static_cast<i32>(index % 3u) - 1) * 0.075f,
        };
        constexpr Float3U normals[] = {
            { 0.0f, 0.0f, 1.0f }, { 0.6f, 0.0f, 0.8f }, { 0.0f, -0.8f, 0.6f }, { -0.8f, 0.0f, 0.6f },
        };
        query.normal = normals[index % LengthOf(normals)];
    }
    switch(testCase.pattern){
    case Pattern::Empty:
        for(u32& head : heads)
            head = NWB_SURFEL_CELL_INVALID;
        break;
    case Pattern::Dead:
        for(Surfel& surfel : pool)
            surfel.alive = 0u;
        break;
    case Pattern::Unsampled:
        for(Surfel& surfel : pool)
            surfel.sampleCount = 0u;
        break;
    case Pattern::Opposed:
        for(Surfel& surfel : pool)
            surfel.normal.z = -1.0f;
        break;
    case Pattern::Disoccluded:
        for(Surfel& surfel : pool)
            surfel.position = { 100.0f, -200.0f, 300.0f };
        break;
    case Pattern::Constant:
        break;
    case Pattern::Directional:
    case Pattern::Weighted:
        for(u32 index = 0u; index < testCase.poolCount; ++index){
            Surfel& surfel = pool[index];
            const f32 scale = static_cast<f32>(index + 1u) / static_cast<f32>(testCase.poolCount);
            surfel.position = { 0.6f * scale - 0.3f, 0.125f * scale, -0.0625f * scale };
            surfel.radius = 0.125f + 1.375f * scale;
            surfel.normal = index % 2u == 0u ? Float3U{ 0.6f, 0.0f, 0.8f } : Float3U{ -0.8f, 0.0f, 0.6f };
            surfel.sampleCount = index % 10u;
            surfel.shR = { 1.0f + scale, -0.413f * scale, 0.727f - scale, 0.137f };
            surfel.shG = { 0.375f, 0.183f * scale, 1.271f - scale, -0.719f * scale };
            surfel.shB = { 0.0625f + scale, -0.937f, -0.271f + scale, 0.391f * scale };
            if(testCase.pattern == Pattern::Weighted){
                surfel.shR.x = index % 2u == 0u ? 0.0078125f : 48.0625f;
                surfel.shG.x = index % 3u == 0u ? 0.001953125f : 7.0625f;
                surfel.shB.x = index % 4u == 0u ? 31.9375f : 0.015625f;
            }
        }
        // Unequal bucket starts and empty buckets make all five dx head selections observable.
        for(u32 index = 0u; index < testCase.cellCount; ++index)
            heads[index] = index % 7u == 0u ? NWB_SURFEL_CELL_INVALID : (index * 11u) % testCase.poolCount;
        break;
    case Pattern::CapRejectsTail:
        for(u32 index = 0u; index < NWB_SURFEL_MAX_WALK; ++index)
            pool[index].alive = 0u;
        break;
    case Pattern::CapIgnoresNonfiniteTail:
        pool[NWB_SURFEL_MAX_WALK].shR.x = Limit<f32>::s_QuietNaN;
        break;
    case Pattern::Cycle:
        pool.back().nextInCell = 0u;
        break;
    case Pattern::ZeroWeightNaN:
    case Pattern::ZeroWeightInfinity:
        pool[1].position.x = 100.0f;
        pool[1].shR.x = testCase.pattern == Pattern::ZeroWeightNaN ? Limit<f32>::s_QuietNaN : Limit<f32>::s_Infinity;
        break;
    case Pattern::Infinity:
        pool[0].shR.x = Limit<f32>::s_Infinity;
        break;
    case Pattern::HalfOverflow:
        pool[0].shG.x = 131072.0f;
        break;
    case Pattern::Biased:
        constants.coverageRadiusBiasHyst.z = 0.28125f;
        break;
    case Pattern::TinyRadius:
        pool[0].radius = 0.0f;
        break;
    case Pattern::NearCoverageThreshold:
        pool[0].normal.z = 0.0000008f;
        break;
    case Pattern::BoundedHead:
        for(u32 index = 0u; index < testCase.cellCount; ++index)
            heads[index] = index % 2u == 0u ? testCase.poolCount : NWB_SURFEL_CELL_PENDING;
        break;
    case Pattern::BoundedNext:
        pool[0].nextInCell = testCase.poolCount + 9u;
        break;
    }
}

void CheckIndependentProperties(const Case& testCase, const NotNull<const Observation*> observationData){
    const Observation* const observations = observationData.get();
    const bool uncovered = testCase.pattern == Pattern::Empty || testCase.pattern == Pattern::Dead
        || testCase.pattern == Pattern::Unsampled || testCase.pattern == Pattern::Opposed
        || testCase.pattern == Pattern::Disoccluded || testCase.pattern == Pattern::CapRejectsTail
        || testCase.pattern == Pattern::BoundedHead;
    for(u32 index = 0u; index < s_QueryCount; ++index){
        EXPECT_LE(observations[index].covered, 1u);
        EXPECT_LE(observations[index].r, 0xffffu);
        EXPECT_LE(observations[index].g, 0xffffu);
        EXPECT_LE(observations[index].b, 0xffffu);
        if(uncovered){
            EXPECT_EQ(observations[index].covered, 0u);
            EXPECT_EQ(observations[index].r, 0u);
            EXPECT_EQ(observations[index].g, 0u);
            EXPECT_EQ(observations[index].b, 0u);
        }
    }
    const Observation& center = observations[0];
    if(testCase.pattern == Pattern::Constant || testCase.pattern == Pattern::CapIgnoresNonfiniteTail
        || testCase.pattern == Pattern::Cycle || testCase.pattern == Pattern::TinyRadius || testCase.pattern == Pattern::BoundedNext){
        EXPECT_EQ(center.covered, 1u);
        const f32 r = ConvertHalfToFloat(static_cast<u16>(center.r));
        const f32 g = ConvertHalfToFloat(static_cast<u16>(center.g));
        const f32 b = ConvertHalfToFloat(static_cast<u16>(center.b));
        EXPECT_TRUE(IsFinite(r));
        EXPECT_GT(b, 0.0f);
        EXPECT_EQ(r, 2.0f * g);
        EXPECT_EQ(g, 2.0f * b);
        if(testCase.poolCount == 1u){
            // At the origin every weight is one: 125 visits, an exact integer denominator and 124 positive half additions.
            constexpr f64 unitRoundoff = 1.0 / 2048.0;
            constexpr f64 sumError = (124.0 * unitRoundoff) / (1.0 - 124.0 * unitRoundoff);
            constexpr f64 expected = 4.0 * 0.2820947918;
            constexpr f64 relativeError = (1.0 + sumError) * (1.0 + unitRoundoff) * (1.0 + unitRoundoff) - 1.0;
            EXPECT_NEAR(static_cast<f64>(r), expected, expected * relativeError);
        }
    }
    if(testCase.pattern == Pattern::Infinity || testCase.pattern == Pattern::HalfOverflow){
        EXPECT_EQ(center.covered, 1u);
        const u32 nonfinite = testCase.pattern == Pattern::Infinity ? center.r : center.g;
        EXPECT_EQ(nonfinite & 0x7c00u, 0x7c00u);
    }
    if(testCase.pattern == Pattern::ZeroWeightInfinity){
        EXPECT_EQ(center.covered, 1u);
        // Removing zero-weight material/SH work would incorrectly remove this observable 0 * infinity propagation.
        EXPECT_EQ(center.r & 0x7c00u, 0x7c00u);
        EXPECT_NE(center.r & 0x03ffu, 0u);
    }
}

void RunCase(
    GraphicsBackend::Device& device,
    const ComputePipelineHandle (&pipelines)[s_PipelineCount],
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.label.data());
    auto& heap = device.getDescriptorHeap();
    Surfels pool(scratchArena);
    Heads heads(scratchArena);
    Queries queries(scratchArena);
    Constants constants;
    BuildInputs(testCase, pool, heads, queries, constants);
    const void* const data[] = { &constants, pool.data(), heads.data(), queries.data() };
    const usize sizes[] = { sizeof(constants), pool.size() * sizeof(Surfel), heads.size() * sizeof(u32), queries.size() * sizeof(Query) };
    BufferHandle buffers[5];
    GpuDescriptorHandle descriptors[5];
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        const bool uniform = index == 0u;
        const bool output = index == 4u;
        BufferDesc desc;
        desc
            .setByteSize(output ? s_PipelineCount * s_OutputStride * sizeof(Observation) : sizes[index])
            .setIsConstantBuffer(uniform)
            .setCanHaveRawViews(!uniform)
            .setCanHaveUAVs(output)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        if(index == 1u || index == 2u)
            desc.setStructStride(index == 1u ? sizeof(Surfel) : sizeof(u32));
        if(output)
            desc.setCpuAccess(CpuAccessMode::Read);
        buffers[index] = device.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        descriptors[index] = heap.allocate(uniform ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        const DescriptorWriteItem view = uniform ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get())
            : output ? DescriptorWriteItem::RawBuffer_UAV(0u, buffers[index].get())
            : index == 1u || index == 2u ? DescriptorWriteItem::StructuredBuffer_SRV(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get());
        ASSERT_TRUE(heap.write(descriptors[index], view));
    }
    Vector<Observation, Alloc::ScratchArena> sentinels(s_PipelineCount * s_OutputStride, s_Guard, scratchArena);
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    for(u32 index = 0u; index < LengthOf(data); ++index){
        ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[index], data[index], sizes[index]));
        commandList->setBufferState(buffers[index].get(), index == 0u ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource);
    }
    ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[4], sentinels.data(), sentinels.size() * sizeof(Observation)));
    commandList->setBufferState(buffers[4].get(), ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    for(u32 index = 0u; index < s_PipelineCount; ++index){
        if(testCase.boundedOnly && index < 2u)
            continue;
        ComputeState state;
        state.setPipeline(pipelines[index].get());
        commandList->setComputeState(state);
        heap.bindCompute(*commandList, *pipelines[index]);
        const PushConstants push{
            descriptors[0].slot(), descriptors[1].slot(), descriptors[2].slot(), descriptors[3].slot(),
            index * s_OutputStride, {}, descriptors[4].slot(), s_QueryCount,
        };
        commandList->setPushConstants(&push, sizeof(push));
        commandList->dispatch(DivideUp<u32>(s_QueryCount, 64u), 1u, 1u);
    }
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const submitted[] = { commandList.get() };
    ASSERT_TRUE(device.executeCommandLists(
        submitted, LengthOf(submitted), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
    const auto* const observations = static_cast<const Observation*>(device.mapBuffer(*buffers[4], CpuAccessMode::Read));
    ASSERT_NE(observations, nullptr);
    ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*buffers[4]); });
    for(u32 policy = testCase.boundedOnly ? 1u : 0u; policy < 2u; ++policy){
        SCOPED_TRACE(policy);
        const Observation* const candidate = observations + policy * 2u * s_OutputStride;
        const Observation* const reference = candidate + s_OutputStride;
        for(u32 index = 0u; index < s_QueryCount; ++index){
            SCOPED_TRACE(index);
            EXPECT_EQ(candidate[index].covered, reference[index].covered);
            EXPECT_EQ(candidate[index].r, reference[index].r);
            EXPECT_EQ(candidate[index].g, reference[index].g);
            EXPECT_EQ(candidate[index].b, reference[index].b);
            if(policy == 1u && !testCase.boundedOnly)
                EXPECT_EQ(NWB_MEMCMP(candidate + index, observations + index, sizeof(Observation)), 0);
        }
        CheckIndependentProperties(testCase, MakeNotNull(candidate));
        CheckIndependentProperties(testCase, MakeNotNull(reference));
    }
    for(u32 pipeline = 0u; pipeline < s_PipelineCount; ++pipeline){
        const u32 firstUntouched = testCase.boundedOnly && pipeline < 2u ? 0u : s_QueryCount;
        for(u32 index = firstUntouched; index < s_OutputStride; ++index)
            EXPECT_EQ(NWB_MEMCMP(observations + pipeline * s_OutputStride + index, &s_Guard, sizeof(Observation)), 0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SurfelGatherKernelTest : public DescriptorBufferRoundTripTest{
protected:
    [[nodiscard]] bool loadPipelines(Alloc::ScratchArena& scratchArena, ComputePipelineHandle (&pipelines)[4]);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SurfelGatherKernelTest::loadPipelines(Alloc::ScratchArena& scratchArena, ComputePipelineHandle (&pipelines)[4]){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_SURFEL_KERNEL_SOURCE_ROOT);
    const Path assetRoot = sourceRoot / "tests/smoke/surfel_kernel/assets";
    const Path outputRoot(memoryArena, NWB_SURFEL_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    if(!shaderCook.parseShaderMeta(assetRoot / "gather_field_cs.nwb", entry, scratchArena))
        return false;
    Impl::ShaderCook::CookVector<Path> includes(memoryArena);
    includes.push_back(sourceRoot / "impl/assets/graphics");
    // The historical helper retains its original relative includes and is frozen without source rewriting.
    includes.push_back(sourceRoot / "impl/assets/graphics/gi/surfel");
    Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
    const Path sourcePath = assetRoot / "gather_field_cs.slang";
    if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
        return false;
    constexpr AStringView variants[] = { "live_current", "live_reference", "snapshot_current", "snapshot_reference" };
    for(u32 index = 0u; index < LengthOf(pipelines); ++index){
        const Impl::ShaderCook::ShaderMacroDefinition defines[] = {
            { "NWB_SURFEL_TEST_BOUNDED", index >= 2u ? AStringView("1") : AStringView("0") },
            { "NWB_SURFEL_TEST_REFERENCE", index % 2u == 1u ? AStringView("1") : AStringView("0") },
        };
        AString<Alloc::ScratchArena> outputName(variants[index], scratchArena);
        outputName += ".spv";
        const Impl::ShaderCook::ShaderCompilerRequest request{
            .shaderName = "tests/surfel_kernel/gather_field_cs",
            .stage = entry.stage.view(),
            .targetProfile = entry.targetProfile.view(),
            .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
            .variantName = variants[index],
            .defines = defines,
            .includeDirectories = includes,
            .dependencies = dependencies,
            .sourcePath = sourcePath,
            .outputPath = outputRoot / outputName.c_str(),
            .defineCount = LengthOf(defines),
            .optimizationLevel = entry.optimizationLevel,
        };
        Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
        if(!shaderCook.compileVariant(request, bytecode) || bytecode.empty())
            return false;
        ShaderDesc shaderDesc(memoryArena);
        shaderDesc.setShaderType(ShaderType::Compute).setEntryName(request.entryPoint);
        const ShaderHandle shader = graphicsDevice.createShader(shaderDesc, bytecode.data(), bytecode.size());
        if(!shader)
            return false;
        BindingLayoutDesc layoutDesc(memoryArena);
        layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, sizeof(__hidden_surfel_gather_tests::PushConstants)));
        const BindingLayoutHandle layout = graphicsDevice.createBindingLayout(layoutDesc);
        if(!layout)
            return false;
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.setComputeShader(shader).addBindingLayout(layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        pipelines[index] = graphicsDevice.createComputePipeline(pipelineDesc);
        if(!pipelines[index])
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(SurfelGatherKernelTest, PrefetchedHeadsPreserveFrozenLiveAndSnapshotGatherExactly){
    using namespace __hidden_surfel_gather_tests;
    const Common::LoggerRegistrationGuard diagnosticLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const auto run = [&](){
        Alloc::ScratchArena scratchArena(Name("tests/smoke/surfel_kernel/gather_field"));
        ComputePipelineHandle pipelines[s_PipelineCount];
        ASSERT_TRUE(loadPipelines(scratchArena, pipelines));
        constexpr Case cases[] = {
            { "empty field", Pattern::Empty, 256u },
            { "dead linked records", Pattern::Dead, 8u, 16u },
            { "unsampled linked records", Pattern::Unsampled, 2u, 16u },
            { "opposed normal rejection", Pattern::Opposed, 1u, 16u },
            { "far disocclusion", Pattern::Disoccluded, 1u, 16u },
            { "125 overlapping visits", Pattern::Constant },
            { "15-node chains", Pattern::Constant, 2u, 15u },
            { "16-node cap", Pattern::Constant, 8u, 16u },
            { "tail after cap cannot provide coverage", Pattern::CapRejectsTail, 1u, 17u },
            { "tail after cap cannot poison RGB", Pattern::CapIgnoresNonfiniteTail, 4u, 17u },
            { "three-node cycle stops at cap", Pattern::Cycle, 8u, 3u },
            { "directional mixed buckets", Pattern::Directional, 256u, 19u },
            { "directional collisions", Pattern::Directional, 8u, 19u },
            { "weight-sensitive mixed buckets", Pattern::Weighted, 256u, 23u },
            { "weight-sensitive collisions", Pattern::Weighted, 4u, 23u },
            { "zero weight NaN remains evaluated", Pattern::ZeroWeightNaN, 2u, 2u },
            { "zero weight infinity remains evaluated", Pattern::ZeroWeightInfinity, 2u, 2u },
            { "infinite irradiance", Pattern::Infinity },
            { "finite SH overflows half", Pattern::HalfOverflow },
            { "normal-biased lookup", Pattern::Biased, 256u, 3u },
            { "radius minimum", Pattern::TinyRadius },
            { "half coverage threshold", Pattern::NearCoverageThreshold },
            { "snapshot invalid and pending heads", Pattern::BoundedHead, 8u, 3u, true },
            { "snapshot out-of-range next", Pattern::BoundedNext, 4u, 2u, true },
        };
        for(const Case& testCase : cases)
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), pipelines, testCase, scratchArena));
    };
    run();
    const TStringView validationPrefix = NWB_TEXT("Vulkan debug: [severity=error");
    const bool validationFailed = s_logger->sawMessageContaining(validationPrefix);
    if(HasFailure() || validationFailed || s_logger->errorCount() != 0u){
        s_logger->emitErrorsToStderr();
        s_logger->emitMessagesContainingToStderr(validationPrefix);
    }
    EXPECT_EQ(s_logger->errorCount(), 0u);
    EXPECT_FALSE(validationFailed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

