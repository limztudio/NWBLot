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

void SoftwareTransparentSamplingHistory::prepareLighting(const Span<const ECSRenderDetail::SceneLightGpuData> lights)noexcept{
    const u32 lightCount = static_cast<u32>(lights.size());
    m_lightHash = s_Fnv64OffsetBasis;
    Fnv64AppendValue(m_lightHash, lightCount);
    for(const ECSRenderDetail::SceneLightGpuData& light : lights){
        Fnv64AppendValue(m_lightHash, light.position);
        Fnv64AppendValue(m_lightHash, light.direction);
        Fnv64AppendValue(m_lightHash, light.colorIntensity);
        Fnv64AppendValue(m_lightHash, light.params);
        Fnv64AppendValue(m_lightHash, light.params2);
    }
    m_lightingPrepared = true;
}

void SoftwareTransparentSamplingHistory::accept()noexcept{
    m_acceptedScene = m_scene;
    m_acceptedLightHash = m_lightHash;
    m_accepted = m_scene.trusted && m_lightingPrepared;
}

bool SoftwareTransparentSamplingHistory::usable()const noexcept{
    return m_accepted && m_lightingPrepared && m_scene.trusted && m_acceptedScene.trusted && m_scene.geometry == m_acceptedScene.geometry && m_scene.material == m_acceptedScene.material && m_lightHash == m_acceptedLightHash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

