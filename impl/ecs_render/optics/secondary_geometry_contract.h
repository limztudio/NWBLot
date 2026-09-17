// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh;
class Material;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// P9 secondary-effect geometry contract (prototype gate, not an enabled optimization). A proxy keeps the visible
// high-poly model while assigning a cheaper representation to selected shadow/GI queries. Qualification requires:
// closed-volume or explicit thin-sheet status, volume/material identity match, sampled path-length error bounds,
// small-gap preservation, posed (not bind-pose-only) deformation compatibility, a declared CSG policy, and stable
// LOD transitions. No proxy ships without closing the measured gap at an approved visual cost (P9 decision gate).
struct SecondaryEffectGeometryProxyDescriptor{
    Core::Assets::AssetRef<Mesh> highDetailMesh;
    Core::Assets::AssetRef<Mesh> proxyMesh;
    Core::Assets::AssetRef<Material> material;
    f32 maxPathLengthError = 0.0f;
    bool closedVolume = false;
    bool thinSheetFallback = false;
    bool csgFallback = true;
    bool qualified = false;

    [[nodiscard]] bool usable()const noexcept{
        return qualified
            && proxyMesh
            && highDetailMesh
            && (closedVolume || thinSheetFallback)
            && maxPathLengthError >= 0.0f
        ;
    }
};

// P9 light-space transmission field definition (prototype gate). For the passive attenuation model:
//   opticalDepth(z) = integrated extinction along the light ray up to depth z
//   transmission(z) = interfaceWeight(z) * exp(-opticalDepth(z))
// Storage is bounded and versioned; receivers sample at their light-space depth. Overlapping volumes sum
// independent-occluder depth (the P1 policy); entry/exit, inside-origin, and thin-sheet semantics must match the
// exact path before any prototype claims equivalence.
struct LightSpaceTransmissionDescriptor{
    u32 version = 1u;
    u32 sliceCount = 0u;
    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
    bool qualified = false;

    [[nodiscard]] bool usable()const noexcept{
        return qualified
            && version == 1u
            && sliceCount > 0u
            && farDepth > nearDepth
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
