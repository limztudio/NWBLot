// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "graph_dispatch_plan.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshSkinningGraphResourceUses{
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> deformation;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> postDispatch;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> localBounds;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> finalizer;


public:
    explicit MeshSkinningGraphResourceUses(Core::Alloc::ScratchArena& scratchArena)noexcept
        : deformation(scratchArena)
        , postDispatch(scratchArena)
        , localBounds(scratchArena)
        , finalizer(scratchArena)
    {}
};

// The result owns ordered uses built from immutable plans.
namespace __hidden_skinning_graph_resource_uses{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResourceUseInfo{
    usize outputIndex = 0u;
    Core::ResourceStates::Mask requiredState = Core::ResourceStates::Unknown;
    Core::GpuTaskResourceAccess::Enum access = Core::GpuTaskResourceAccess::Read;
};

struct ResourceUseEntry{
    Core::GpuGraphResourceId resource;
    ResourceUseInfo info;
};

// Inline storage covers one plan's roles; larger sets index unique identities only.
template<usize InlineCapacity>
class ResourceUseCollector final : NoCopy{
private:
    using Index = HashMap<Core::GpuGraphResourceId, ResourceUseInfo, Core::Alloc::ScratchArena, Core::GpuGraphResourceIdHasher, EqualTo<Core::GpuGraphResourceId>>;


public:
    [[nodiscard]] static b8 MergeUse(const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access, ResourceUseInfo& info)noexcept{
        if(info.requiredState != state)
            return false;
        if(info.access != access)
            info.access = Core::GpuTaskResourceAccess::ReadWrite;
        return true;
    }


public:
    explicit ResourceUseCollector(Core::Alloc::ScratchArena& scratchArena)noexcept
        : m_scratchArena(scratchArena)
    {}


public:
    [[nodiscard]] b8 add(const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access){
        if(!resource.valid())
            return false;
        if(m_index)
            return addIndexed(resource, state, access);
        for(usize index = 0u; index < m_count; ++index){
            ResourceUseEntry& entry = m_inline[index];
            if(entry.resource == resource)
                return MergeUse(state, access, entry.info);
        }
        if(m_count < InlineCapacity){
            m_inline[m_count] = { resource, { m_count, state, access } };
            ++m_count;
            return true;
        }

        if(!promoteIndex())
            return false;
        return addIndexed(resource, state, access);
    }

    [[nodiscard]] b8 promoteIndex(){
        NWB_ASSERT(!m_index && m_count == InlineCapacity);
        Index index(InlineCapacity * s_SkinningPromotedIndexCapacityFactor, Core::GpuGraphResourceIdHasher{}, EqualTo<Core::GpuGraphResourceId>{}, m_scratchArena);
        for(const ResourceUseEntry& entry : m_inline){
            const auto [found, inserted] = index.emplace(entry.resource, entry.info);
            if(!inserted){
                NWB_ASSERT(false);
                return false;
            }
        }
        static_assert(IsNothrowMoveConstructible_V<Index>);
        m_index.emplace(Move(index));
        return true;
    }

    [[nodiscard]] b8 addIndexed(const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access){
        const auto [found, inserted] = m_index->try_emplace(resource, ResourceUseInfo{ m_count, state, access });
        if(inserted){
            ++m_count;
            return true;
        }
        return MergeUse(state, access, found.value());
    }

    void writeTo(Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& outUses)const{
        outUses.resize(m_count);
        const auto writeUse = [&](const Core::GpuGraphResourceId resource, const ResourceUseInfo& info)noexcept{
            outUses[info.outputIndex] = Core::GpuTaskResourceUse{ .resource = resource, .range = {}, .requiredState = info.requiredState, .access = info.access, };
        };
        if(m_index){
            for(const auto& entry : *m_index)
                writeUse(entry.first, entry.second);
        }
        else{
            for(usize index = 0u; index < m_count; ++index)
                writeUse(m_inline[index].resource, m_inline[index].info);
        }
    }


public:
    Core::Alloc::ScratchArena& m_scratchArena;
    Array<ResourceUseEntry, InlineCapacity> m_inline{};
    usize m_count = 0u;
    Optional<Index> m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<MeshSkinningGraphResourceUses> BuildMeshSkinningGraphResourceUses(const Span<const MeshSkinningGraphDispatchPlan> plans, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_skinning_graph_resource_uses;

    MeshSkinningGraphResourceUses uses(scratchArena);
    if(plans.empty())
        return uses;
    if(!plans.data()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph resource uses require dispatch plans"));
        return MakeUnexpected(Failure{});
    }

    ResourceUseCollector<s_SkinningDeformationUseCapacity> deformation(scratchArena);
    ResourceUseCollector<s_SkinningPostDispatchUseCapacity> postDispatch(scratchArena);
    ResourceUseCollector<s_SkinningLocalBoundsUseCapacity> localBounds(scratchArena);
    ResourceUseCollector<s_SkinningFinalizerUseCapacity> finalizer(scratchArena);
    for(const MeshSkinningGraphDispatchPlan& plan : plans){
        if(!plan.hasActiveSkin)
            continue;
        if(
            !deformation.add(plan.bindlessResourceSlotsResource, Core::ResourceStates::ConstantBuffer, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.restPositionResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.restNormalResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.restTangentResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.skinnedPositionResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || !deformation.add(plan.skinnedNormalResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || !deformation.add(plan.skinnedTangentResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || !deformation.add(plan.meshletDescResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.meshletPositionRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.meshletAttributeRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.attributeSkinResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.skinResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !deformation.add(plan.jointPaletteResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph resource uses for skinning deformation"));
            return MakeUnexpected(Failure{});
        }
    }

    for(const MeshSkinningGraphDispatchPlan& plan : plans){
        if(
            !postDispatch.add(plan.bindlessResourceSlotsResource, Core::ResourceStates::ConstantBuffer, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.skinnedPositionResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletDescResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletPositionRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletLocalVertexRefResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletPrimitiveIndexResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletBoundsResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || (plan.updatesMeshletBounds && !postDispatch.add(plan.meshletLocalBoundsResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write))
            || (plan.repacksNormals && (
                !postDispatch.add(plan.skinnedNormalResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !postDispatch.add(plan.meshletAttributeRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !postDispatch.add(plan.attributeResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            ))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph resource uses for skinning bounds/repack"));
            return MakeUnexpected(Failure{});
        }
    }

    for(const MeshSkinningGraphDispatchPlan& plan : plans){
        if(!plan.updatesMeshletBounds)
            continue;
        if(
            !localBounds.add(plan.bindlessResourceSlotsResource, Core::ResourceStates::ConstantBuffer, Core::GpuTaskResourceAccess::Read)
            || !localBounds.add(plan.meshletLocalBoundsResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !localBounds.add(plan.localBoundsResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare local bounds reduction resources"));
            return MakeUnexpected(Failure{});
        }
    }

    for(const MeshSkinningGraphDispatchPlan& plan : plans){
        if(
            ((plan.hasActiveSkin || plan.copiedRestStreams) && (
                !finalizer.add(plan.skinnedPositionResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !finalizer.add(plan.skinnedNormalResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !finalizer.add(plan.skinnedTangentResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            ))
            || (plan.updatesMeshletBounds && (
                !finalizer.add(plan.meshletBoundsResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !finalizer.add(plan.localBoundsResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            ))
            || (plan.repacksNormals && !finalizer.add(plan.attributeResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning final states"));
            return MakeUnexpected(Failure{});
        }
    }
    if(finalizer.m_count == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning dispatch has no final state"));
        return MakeUnexpected(Failure{});
    }

    deformation.writeTo(uses.deformation);
    postDispatch.writeTo(uses.postDispatch);
    localBounds.writeTo(uses.localBounds);
    finalizer.writeTo(uses.finalizer);
    return uses;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

