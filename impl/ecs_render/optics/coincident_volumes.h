// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material;
class Mesh;
class RendererMaterialSystem;

// Prepared static geometry and effective material inputs. The byte view is borrowed only during select();
// the retained frame selection contains entity IDs, never pointers into material or scratch storage.
struct CoincidentOpticalVolumeCandidate{
    Core::ECS::EntityID entity;
    Core::Assets::AssetRef<Mesh> mesh;
    Core::Assets::AssetRef<Material> material;
    Name group = NAME_NONE;
    i32 priority = 0;
    // Identical-material grouping also requires the same authored medium semantics.
    u32 boundaryMode = 0u;
    i32 mediumPriority = 0;
    Float3U position = Float3U(0.f, 0.f, 0.f);
    Float4U rotation = Float4U(0.f, 0.f, 0.f, 1.f);
    Float3U scale = Float3U(1.f, 1.f, 1.f);
    const u8* mutableTypedBytes = nullptr;
    usize mutableTypedByteCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The frame pipeline owns one selection and freezes it before raster, RT and caustic preparation diverge.
// Every consumer uses the same decision; dense raster and RT indices retain their separate existing ABIs.
class RendererOpticalVolumeSelection final : NoCopy{
public:
    explicit RendererOpticalVolumeSelection(Core::Alloc::GlobalArena& arena);


public:
    void prepare(Core::ECS::World& world, RendererMaterialSystem& materials, Core::Alloc::ScratchArena& scratchArena);
    // Candidates must contain each entity at most once. Null denotes an empty candidate range.
    void select(const CoincidentOpticalVolumeCandidate* candidates, usize candidateCount, Core::Alloc::ScratchArena& scratchArena);
    void reset();
    [[nodiscard]] bool isSuppressed(Core::ECS::EntityID entity)const;


private:
    HashSet<Core::ECS::EntityID, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_suppressed;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

