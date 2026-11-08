// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_target_builder.h"

#include <impl/ecs_render/avboit/avboit_target_factory.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/assets/graphics/refraction/push_constants.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionTargetBuilder::createImages(DeferredFrameTargets& createdTargets, AvboitFrameTargets& avboitTargets, const Core::Format::Enum accumColorFormat, const Core::Format::Enum accumExtinctionFormat, const Core::Color& transparentBlack){
    auto& device = m_graphics.getDevice();
    auto foregroundAccumColorResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, accumColorFormat, "engine/avboit/foreground_color", transparentBlack, true);
    auto foregroundAccumExtinctionResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, accumExtinctionFormat, "engine/avboit/foreground_extinction", transparentBlack, true);
    auto refractionNormalIorResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RGBA16_FLOAT, "engine/avboit/refraction_normal_ior", transparentBlack, true);
    auto refractionTintCoverageResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RGBA16_FLOAT, "engine/avboit/refraction_tint_coverage", transparentBlack, true);
    auto refractionInstanceResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RG32_FLOAT, "engine/avboit/refraction_instance", transparentBlack, true);
    auto refractionSpecularRoughnessResult = AvboitTargetFactory::CreateRenderTarget(m_graphics, createdTargets.width, createdTargets.height, Core::Format::RGBA16_FLOAT, "engine/avboit/refraction_specular_roughness", Core::Color(0.f, 0.f, 0.f, 1.f), true);
    Core::TextureDesc refractionDepthDesc = createdTargets.depth->getCreationDescription();
    refractionDepthDesc.setName("engine/avboit/refraction_depth").setInitialState(Core::ResourceStates::Common).setKeepInitialState(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    auto refractionDepthResult = m_graphics.createTexture(refractionDepthDesc);
    if(refractionDepthResult)
        avboitTargets.refractionDepth = Move(*refractionDepthResult);
    else
        avboitTargets.refractionDepth.reset();
    Core::TextureDesc refractionResolveDesc;
    refractionResolveDesc.setWidth(createdTargets.width).setHeight(createdTargets.height)
        .setFormat(Core::Format::RGBA16_FLOAT).setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/avboit/refraction_resolve").setClearValue(transparentBlack)
        .setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
    ;
    auto refractionResolveResult = m_graphics.createTexture(refractionResolveDesc);
    if(refractionResolveResult)
        avboitTargets.refractionResolve = Move(*refractionResolveResult);
    else
        avboitTargets.refractionResolve.reset();
    if(!foregroundAccumColorResult || !foregroundAccumExtinctionResult || !refractionNormalIorResult || !refractionTintCoverageResult || !refractionInstanceResult || !refractionSpecularRoughnessResult || !refractionDepthResult || !refractionResolveResult)
        return false;
    avboitTargets.foregroundAccumColor = Move(*foregroundAccumColorResult);
    avboitTargets.foregroundAccumExtinction = Move(*foregroundAccumExtinctionResult);
    avboitTargets.refractionNormalIor = Move(*refractionNormalIorResult);
    avboitTargets.refractionTintCoverage = Move(*refractionTintCoverageResult);
    avboitTargets.refractionInstance = Move(*refractionInstanceResult);
    avboitTargets.refractionSpecularRoughness = Move(*refractionSpecularRoughnessResult);
    Core::FramebufferDesc refractionFramebufferDesc;
    refractionFramebufferDesc
        .addColorAttachment(avboitTargets.refractionNormalIor.get(), ECSRenderDetail::s_FramebufferSubresources)
        .addColorAttachment(avboitTargets.refractionTintCoverage.get(), ECSRenderDetail::s_FramebufferSubresources)
        .addColorAttachment(avboitTargets.refractionInstance.get(), ECSRenderDetail::s_FramebufferSubresources)
        // Fourth output carries reflection inputs; keep it off foreground extinction.
        .addColorAttachment(avboitTargets.refractionSpecularRoughness.get(), ECSRenderDetail::s_FramebufferSubresources).setDepthAttachment(avboitTargets.refractionDepth.get(), ECSRenderDetail::s_FramebufferSubresources)
    ;
    auto refractionFramebufferResult = device.createFramebuffer(refractionFramebufferDesc);
    if(refractionFramebufferResult)
        avboitTargets.refractionFramebuffer = Move(*refractionFramebufferResult);
    else
        avboitTargets.refractionFramebuffer.reset();
    if(!refractionFramebufferResult)
        return false;

    return true;
}

b8 RefractionTargetBuilder::createTileBuffers(const u64 tileCount, AvboitFrameTargets& avboitTargets){
    const auto createTileBuffer = [&](const u64 byteSize, const Name name, const b8 indirect){
        Core::BufferDesc desc;
        desc.setByteSize(byteSize).setStructStride(sizeof(u32)).setCanHaveUAVs(true).setCanHaveRawViews(true).setIsDrawIndirectArgs(indirect)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setDebugName(name).enableAutomaticStateTracking(Core::ResourceStates::Common);
        return m_graphics.createBuffer(desc);
    };
    auto tileList = createTileBuffer(tileCount * sizeof(u32), Name("engine/refraction/tile_list"), false);
    auto tileArguments = createTileBuffer(NWB_REFRACTION_TILE_ARGUMENT_BYTES, Name("engine/refraction/tile_arguments"), true);
    if(!tileList || !tileArguments){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create refraction tile scheduling buffers"));
        return false;
    }
    avboitTargets.refractionTileList = Move(*tileList);
    avboitTargets.refractionTileArguments = Move(*tileArguments);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

