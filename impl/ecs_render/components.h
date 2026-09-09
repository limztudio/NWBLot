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
    // Merging is opt-in: both modes assert that the material's geometry and surface hooks are independent of
    // dense instance ID. Mesh, material asset and exact transform must match. IdenticalMaterial also compares
    // every effective mutable byte; SharedGroup requires a nonempty common group and asserts that differing
    // mutable inputs preserve the same boundary. Runtime meshes and CSG receivers always remain independent.
    OpticalVolumeCoincidence::Enum opticalVolumeCoincidence = OpticalVolumeCoincidence::Independent;
    Name opticalVolumeGroup = NAME_NONE;
    // Higher priority wins; equal priority uses the lowest full EntityID. The selected material supplies every
    // optical and shading property to raster, RT and caustics, including when screen refraction is disabled.
    i32 opticalVolumePriority = 0;
    // Closed modes assert watertight outward-oriented geometry and homogeneous IOR/unit-distance absorption:
    // these optical inputs are invariant across UV, normals, world position, and ray/observer direction.
    // Outward closed shells in one instance form a union. Nested asserts disjoint or properly nested instance
    // unions. Priority replaces the effective medium in overlaps;
    // simultaneously active volumes must use the same mode. These contracts are independent of caustic eligibility.
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

