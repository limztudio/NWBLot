// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/assets/ref.h>
#include <impl/assets_material/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace OpticalVolumeCoincidence{
    enum Enum : u8{
        Independent,
        IdenticalMaterial,
        SharedGroup,
    };
};

namespace OpticalBoundaryMode{
    enum Enum : u8{
        Unspecified,
        ClosedNested,
        ClosedPriority,
    };
};

struct RendererComponent{
    Core::Assets::AssetRef<Material> material;
    bool visible = true;
    // Opt-in merging: hooks ID-independent; mesh/material/transform must match. IdenticalMaterial compares all
    // mutable bytes; SharedGroup needs a common group + same boundary. Runtime/CSG receivers stay independent.
    OpticalVolumeCoincidence::Enum opticalVolumeCoincidence = OpticalVolumeCoincidence::Independent;
    Name opticalVolumeGroup = NAME_NONE;
    // Higher priority wins; equal priority uses the lowest full EntityID. The selected material supplies every
    // optical and shading property to raster, RT and caustics, including when screen refraction is disabled.
    i32 opticalVolumePriority = 0;
    // Closed modes: watertight outward geometry, homogeneous IOR/absorption (UV/normal/position/direction invariant).
    // Same-instance shells union; priority wins overlaps; co-active volumes share the mode.
    OpticalBoundaryMode::Enum opticalBoundaryMode = OpticalBoundaryMode::Unspecified;
    // Higher priority selects the medium; equal priority uses the lowest full EntityID. This is separate from
    // opticalVolumePriority, which chooses a representative of explicitly coincident geometry before gathering.
    i32 opticalMediumPriority = 0;
};

struct MaterialInstanceParameter{
    Name parameterName = NAME_NONE;
    Name blockName = NAME_NONE;
    Name fieldName = NAME_NONE;
    MaterialLayoutFieldType::Enum fieldType = MaterialLayoutFieldType::None;
    UInt4U value = {};
};

struct MaterialInstanceComponent{
    using ParameterVector = Vector<MaterialInstanceParameter, Core::Alloc::GlobalArena>;

    Name materialInterface = NAME_NONE;
    ParameterVector overrides;
    u64 revision = 0u;

    explicit MaterialInstanceComponent(Core::Alloc::GlobalArena& arena, const Name& interfaceName)
        : materialInterface(interfaceName)
        , overrides(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

