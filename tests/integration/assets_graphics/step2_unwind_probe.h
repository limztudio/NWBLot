// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_material/cook_entry.adhd.h>
#include <impl/assets_texture/texture_mip_decoder.adhd.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/cpu/scheduler.h>
#include <core/task/gpu/scheduler.h>
#include <core/perf/timing.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::AssetsGraphicsStep2Probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NthFailGraphicsArena final : public ArenaBase{
public:
    NthFailGraphicsArena()
        : ArenaBase(Name("tests/assets_graphics/nth_fail"))
        , m_backing(Name("tests/assets_graphics/nth_fail_backing"))
    {}

    void arm(const usize failOnCall)noexcept{
        m_armed = true;
        m_remaining = failOnCall;
        m_hit = false;
    }

    void disarm()noexcept{
        m_armed = false;
        m_remaining = 0u;
        m_hit = false;
    }

    [[nodiscard]] b8 hit()const noexcept{ return m_hit; }

    void* allocate(usize align, usize size)override{
        if(shouldFail())
            return nullptr;
        return m_backing.allocate(align, size);
    }

    void* reallocate(void* memory, usize align, usize size)override{
        if(shouldFail())
            return nullptr;
        return m_backing.reallocate(memory, align, size);
    }

    void deallocate(void* memory, usize align, usize size)noexcept override{
        m_backing.deallocate(memory, align, size);
    }


private:
    [[nodiscard]] b8 shouldFail()noexcept{
        if(!m_armed)
            return false;
        if(m_remaining <= 1u){
            m_hit = true;
            return true;
        }
        --m_remaining;
        return false;
    }


private:
    Core::Alloc::GlobalArena m_backing;
    usize m_remaining = 0u;
    b8 m_armed = false;
    b8 m_hit = false;
};


[[nodiscard]] inline b8 VerifyFindOrBuildMaterialBindTypedLayoutOom(
    const Impl::MaterialBindEntry& entryA,
    const Name nameA,
    const Impl::MaterialBindEntry& entryB,
    const Name nameB
){
    NthFailGraphicsArena failArena;
    Impl::MaterialBindTypedLayoutCache cache(failArena);

    const auto committed = Impl::FindOrBuildMaterialBindTypedLayout(nameA, entryA, cache);
    if(!committed.has_value() || committed.value() != 0u || cache.entries.size() != 1u || cache.lookup.size() != 1u)
        return false;
    const u64 committedHash = cache.entries[0u].layoutHash;
    const AStringView committedPath = cache.entries[0u].interfacePath;

    usize failHits = 0u;
    for(usize failAt = 1u; failAt <= 128u; ++failAt){
        failArena.arm(failAt);
        b8 built = false;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            const auto next = Impl::FindOrBuildMaterialBindTypedLayout(nameB, entryB, cache);
            built = next.has_value();
            return built;
        }));
        const b8 hit = failArena.hit();
        failArena.disarm();

        if(!threw && built){
            if(hit || failHits == 0u)
                return false;
            return cache.entries.size() == 2u
                && cache.lookup.size() == 2u
                && cache.entries[0u].layoutHash == committedHash
                && cache.entries[0u].interfacePath == committedPath
                && cache.entries[1u].layoutHash != 0u
            ;
        }

        if(!hit)
            return false;
        if(cache.entries.size() != 1u || cache.lookup.size() != 1u)
            return false;
        if(!cache.lookup.contains(nameA) || cache.lookup.contains(nameB))
            return false;
        if(cache.entries[0u].layoutHash != committedHash || cache.entries[0u].interfacePath != committedPath)
            return false;
        ++failHits;
    }
    return false;
}


[[nodiscard]] inline b8 VerifyTextureUploadFailureDecodeVsSubmitAndZeroByteGuard(const Impl::Texture& textureAsset){
    Core::Alloc::GlobalArena gfxArena(Name("tests/assets_graphics/headless_gfx"));
    Core::GraphicsAllocator gfxAllocator(gfxArena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Core::GpuTaskScheduler gpuScheduler;
    Core::Perf::TimingRecorder gpuTiming(gfxArena);
    Core::GraphicsRuntime graphics(gfxAllocator, cpuScheduler, gpuScheduler, gpuTiming);

    Impl::TextureMipDecoder decoder;

    const auto decodeFailure = decoder.decodeAndUploadStaticTextureMips(textureAsset, Core::Format::R8_UNORM, graphics, Core::TextureHandle{});
    if(decodeFailure.has_value() || decodeFailure.error() != Impl::TextureUploadFailure::Decode)
        return false;

    const auto submitFailureNull = decoder.decodeAndUploadStaticTextureMips(textureAsset, Core::Format::RGBA8_UNORM, graphics, Core::TextureHandle{});
    if(submitFailureNull.has_value() || submitFailureNull.error() != Impl::TextureUploadFailure::Submit)
        return false;

    Core::DeviceCreationParameters params(gfxAllocator.getObjectArena());
    params.headlessDevice = true;
    params.bindlessHeapAbi.resourceSetIndex = 0u;
    params.bindlessHeapAbi.samplerSetIndex = 1u;
    params.bindlessHeapAbi.accelStructSetIndex = 2u;
    params.bindlessHeapAbi.sampledImageBinding = 0u;
    params.bindlessHeapAbi.storageImageBinding = 1u;
    params.bindlessHeapAbi.sampledBufferBinding = 2u;
    params.bindlessHeapAbi.storageBufferBinding = 3u;
    params.bindlessHeapAbi.uniformBufferBinding = 4u;
    params.bindlessHeapAbi.sampledImage2DArrayBinding = 5u;
    params.bindlessHeapAbi.sampledImage3DBinding = 6u;
    params.bindlessHeapAbi.sampledImage2DArrayUintBinding = 7u;
    params.bindlessHeapAbi.sampledImageCubeBinding = 8u;
    params.bindlessHeapAbi.samplerBinding = 0u;
    params.bindlessHeapAbi.accelStructBinding = 0u;

    Core::SwapChainRuntimeState swapChainState;
    Core::GraphicsBackend::Backend backend(params, swapChainState, gfxAllocator, cpuScheduler);
    const b8 createdInstance = backend.createInstance();
    if(!createdInstance){
        ADD_FAILURE() << "backend.createInstance() failed";
        return false;
    }
    const b8 createdDevice = backend.createDevice();
    if(!createdDevice){
        ADD_FAILURE() << "backend.createDevice() failed";
        if(!backend.destroy())
            ADD_FAILURE() << "backend.destroy() failed after createDevice error";
        return false;
    }
    Core::GraphicsBackend::Device* const device = backend.getDevice();
    if(!device){
        ADD_FAILURE() << "backend.getDevice() returned null";
        if(!backend.destroy())
            ADD_FAILURE() << "backend.destroy() failed after null device";
        return false;
    }

    b8 passed = false;
    {
        Core::TextureDesc texDesc;
        texDesc.setName(Name("tests/assets_graphics/headless_upload_probe")).setWidth(1u).setHeight(1u).setFormat(Core::Format::RGBA8_UNORM);
        const Expected<Core::TextureHandle> nonNullTextureResult = device->createTexture(texDesc);
        if(!nonNullTextureResult || !*nonNullTextureResult){
            ADD_FAILURE() << "device->createTexture(texDesc) failed";
            if(!backend.destroy())
                ADD_FAILURE() << "backend.destroy() failed after createTexture error";
            return false;
        }

        Array<Core::GraphicsStandaloneService::TextureUploadRegion, 1> zeroRegions{};
        const auto emptyUpload = graphics.standaloneGraph().uploadTextureBatch(*nonNullTextureResult, Span<const Core::GraphicsStandaloneService::TextureUploadRegion>(zeroRegions), Core::ResourceStates::ShaderResource, Core::ResourceStates::Unknown);
        if(emptyUpload.has_value()){
            ADD_FAILURE() << "emptyUpload unexpectedly succeeded";
        }
        else{
            Core::TextureDesc mismatchDesc;
            mismatchDesc.setName(Name("tests/assets_graphics/headless_mismatch_probe")).setWidth(64u).setHeight(64u).setFormat(Core::Format::RGBA8_UNORM);
            const Expected<Core::TextureHandle> mismatchTextureResult = device->createTexture(mismatchDesc);
            if(!mismatchTextureResult || !*mismatchTextureResult){
                ADD_FAILURE() << "device->createTexture(mismatchDesc) failed";
            }
            else{
                const auto submitFailureMismatch = decoder.decodeAndUploadStaticTextureMips(textureAsset, Core::Format::RGBA8_UNORM, graphics, *mismatchTextureResult);
                if(submitFailureMismatch.has_value() || submitFailureMismatch.error() != Impl::TextureUploadFailure::Submit){
                    ADD_FAILURE() << "submitFailureMismatch has_value=" << submitFailureMismatch.has_value()
                        << " err=" << (submitFailureMismatch.has_value() ? -1 : static_cast<i32>(submitFailureMismatch.error()));
                }
                passed = !submitFailureMismatch.has_value() && submitFailureMismatch.error() == Impl::TextureUploadFailure::Submit;
            }
        }
    }

    const b8 destroyed = backend.destroy();
    if(!destroyed)
        ADD_FAILURE() << "backend.destroy() failed";
    return passed && destroyed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

