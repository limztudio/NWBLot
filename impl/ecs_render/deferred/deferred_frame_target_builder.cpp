// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_frame_target_builder.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/shadow/shadow_visibility_target_builder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredFrameTargets> DeferredFrameTargetBuilder::build(const u32 width, const u32 height, const u32 shadowReceiverFactor, const u32 surfelResolveFactor){
    auto createdTargetsResult = m_deferred.createDeferredFrameTargets(width, height);
    if(!createdTargetsResult)
        return MakeUnexpected(Failure{});
    DeferredFrameTargets& createdTargets = *createdTargetsResult;
    const auto resetCreatedTargets = [this, &createdTargets](){
        m_avboit.resetAvboitFrameTargets(createdTargets.avboit);
        m_deferred.resetDeferredFrameTargets(createdTargets);
    };
    if(!m_avboit.createAvboitSampler()){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_avboit.createAvboitFrameTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_csg.createCsgPeelTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    createdTargets.shadowReceiverFactor = shadowReceiverFactor;
    if(!ShadowVisibilityTargetBuilder(m_graphics, m_rayTracingState).build(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_rayTracing.createCausticTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    createdTargets.surfelResolveFactor = surfelResolveFactor;
    if(!m_rayTracing.createSurfelTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_deferred.createDeferredFrameTargetResources(createdTargets, m_avboit.linearSampler())){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_avboit.registerAvboitFrameTargetDescriptors(createdTargets, createdTargets.avboit)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    return Move(createdTargets);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

