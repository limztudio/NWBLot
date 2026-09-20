// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "software_transparent_sampling.h"

#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareTransparentSamplingHistory::prepareScene(const RayTracingSceneContentStamp& scene)noexcept{
    m_scene = scene;
    m_lightingPrepared = false;
}

void SoftwareTransparentSamplingHistory::prepareLighting(const ECSRenderDetail::SceneLightGpuData* lights, const u32 lightCount)noexcept{
    NWB_ASSERT(lights || lightCount == 0u);
    m_lightHash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(m_lightHash, lightCount);
    for(u32 index = 0u; index < lightCount; ++index){
        Fnv64AppendValue(m_lightHash, lights[index].position);
        Fnv64AppendValue(m_lightHash, lights[index].direction);
        Fnv64AppendValue(m_lightHash, lights[index].colorIntensity);
        Fnv64AppendValue(m_lightHash, lights[index].params);
        Fnv64AppendValue(m_lightHash, lights[index].params2);
    }
    m_lightingPrepared = true;
}

void SoftwareTransparentSamplingHistory::accept()noexcept{
    m_acceptedScene = m_scene;
    m_acceptedLightHash = m_lightHash;
    m_accepted = m_scene.trusted && m_lightingPrepared;
}

void SoftwareTransparentSamplingHistory::discard()noexcept{
    m_accepted = false;
}

bool SoftwareTransparentSamplingHistory::usable()const noexcept{
    return
        m_accepted && m_lightingPrepared && m_scene.trusted && m_acceptedScene.trusted
        && m_scene.geometry == m_acceptedScene.geometry && m_scene.material == m_acceptedScene.material
        && m_lightHash == m_acceptedLightHash
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

