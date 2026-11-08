// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tlas_static_scene_identity.h"

#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>
#include <impl/ecs_render/raytrace/raytracing_scene_identity_builder.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


void TlasStaticSceneIdentity::Append(const Core::RayTracingInstanceDesc& instance, u64& inOutHash)noexcept{
    RayTracingSceneIdentityBuilder().updateTlasStaticInput(
        LoadFloat(instance.transform),
        static_cast<u32>(instance.instanceID),
        static_cast<u32>(instance.instanceMask),
        static_cast<u32>(instance.instanceContributionToHitGroupIndex),
        static_cast<u32>(instance.flags),
        inOutHash
    );
    Fnv64AppendValue(inOutHash, instance.bottomLevelAS);
}

u64 TlasStaticSceneIdentity::Compute(const Span<const Core::RayTracingInstanceDesc> instances)noexcept{
    u64 hash = s_Fnv64OffsetBasis;
    Fnv64AppendValue(hash, s_SceneStaticCacheHashVersion);
    Fnv64AppendValue(hash, instances.size());
    for(const Core::RayTracingInstanceDesc& instance : instances)
        TlasStaticSceneIdentity::Append(instance, hash);
    return hash;
}



NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
