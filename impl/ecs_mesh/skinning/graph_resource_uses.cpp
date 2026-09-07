// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resource_uses.h"

#include <core/common/log.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_graph_resource_uses{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResourceHash{
    [[nodiscard]] usize operator()(const Core::GpuGraphResourceId resource)const noexcept{
        usize hash = Hasher<u64>{}(resource.generation);
        HashCombine(hash, resource.index);
        return hash;
    }
};

struct ResourceUseInfo{
    usize outputIndex = 0u;
    Core::ResourceStates::Mask requiredState = Core::ResourceStates::Unknown;
    Core::GpuTaskResourceAccess::Enum access = Core::GpuTaskResourceAccess::Read;
};

struct ResourceUseEntry{
    Core::GpuGraphResourceId resource;
    ResourceUseInfo info;
};

// Inline storage covers every role emitted by one plan in this phase. Larger collections index only actual unique
// resource identities, and finish all index growth before output allocation. The scratch tables belong to this call.
template<usize InlineCapacity>
struct ResourceUseCollector : NoCopy{
    using Index = HashMap<Core::GpuGraphResourceId, ResourceUseInfo, ResourceHash, EqualTo<Core::GpuGraphResourceId>, Core::Alloc::ScratchArena>;

    Core::Alloc::ScratchArena& m_scratchArena;
    ResourceUseEntry m_inline[InlineCapacity] = {};
    usize m_count = 0u;
    Optional<Index> m_index;

    [[nodiscard]] static bool mergeUse(ResourceUseInfo& info, const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access){
        if(info.requiredState != state)
            return false;
        if(info.access != access)
            info.access = Core::GpuTaskResourceAccess::ReadWrite;
        return true;
    }

    explicit ResourceUseCollector(Core::Alloc::ScratchArena& scratchArena)
        : m_scratchArena(scratchArena)
    {}

    [[nodiscard]] bool add(const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access){
        if(!resource.valid())
            return false;
        if(m_index)
            return addIndexed(resource, state, access);
        for(usize index = 0u; index < m_count; ++index){
            ResourceUseEntry& entry = m_inline[index];
            if(entry.resource == resource)
                return mergeUse(entry.info, state, access);
        }
        if(m_count < InlineCapacity){
            m_inline[m_count] = { resource, { m_count, state, access } };
            ++m_count;
            return true;
        }

        promoteIndex();
        return addIndexed(resource, state, access);
    }

    void promoteIndex(){
        NWB_ASSERT(!m_index && m_count == InlineCapacity);
        Index index(InlineCapacity * 4u, ResourceHash{}, EqualTo<Core::GpuGraphResourceId>{}, m_scratchArena);
        for(const ResourceUseEntry& entry : m_inline)
            index.emplace(entry.resource, entry.info);
        static_assert(IsNothrowMoveConstructible_V<Index>);
        m_index.emplace(Move(index));
    }

    [[nodiscard]] bool addIndexed(const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access){
        const auto [found, inserted] = m_index->try_emplace(resource, ResourceUseInfo{ m_count, state, access });
        if(inserted){
            ++m_count;
            return true;
        }
        return mergeUse(found.value(), state, access);
    }

    void writeTo(Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& outUses)const{
        outUses.resize(m_count);
        const auto writeUse = [&](const Core::GpuGraphResourceId resource, const ResourceUseInfo& info){
            outUses[info.outputIndex] = Core::GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = info.requiredState,
                .access = info.access,
            };
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMeshSkinningGraphResourceUses(
    const MeshSkinningGraphDispatchPlan* const plans,
    const usize planCount,
    Core::Alloc::ScratchArena& scratchArena,
    MeshSkinningGraphResourceUses& outUses){
    using namespace __hidden_skinning_graph_resource_uses;

    outUses.deformation.clear();
    outUses.postDispatch.clear();
    outUses.finalizer.clear();
    if(planCount == 0u)
        return true;
    if(!plans){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph resource uses require dispatch plans"));
        return false;
    }

    ResourceUseCollector<13u> deformation(scratchArena);
    ResourceUseCollector<10u> postDispatch(scratchArena);
    ResourceUseCollector<5u> finalizer(scratchArena);
    for(usize planIndex = 0u; planIndex < planCount; ++planIndex){
        const MeshSkinningGraphDispatchPlan& plan = plans[planIndex];
        if(!plan.hasActiveSkin)
            continue;
        if(
            !deformation.add(
                plan.bindlessResourceSlotsResource,
                Core::ResourceStates::ConstantBuffer,
                Core::GpuTaskResourceAccess::Read
            )
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
            return false;
        }
    }

    for(usize planIndex = 0u; planIndex < planCount; ++planIndex){
        const MeshSkinningGraphDispatchPlan& plan = plans[planIndex];
        if(
            !postDispatch.add(
                plan.bindlessResourceSlotsResource,
                Core::ResourceStates::ConstantBuffer,
                Core::GpuTaskResourceAccess::Read
            )
            || !postDispatch.add(plan.skinnedPositionResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletDescResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletPositionRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletLocalVertexRefResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletPrimitiveIndexResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !postDispatch.add(plan.meshletBoundsResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || (plan.repacksNormals && (
                !postDispatch.add(plan.skinnedNormalResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !postDispatch.add(plan.meshletAttributeRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !postDispatch.add(plan.attributeResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            ))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph resource uses for skinning bounds/repack"));
            return false;
        }
    }

    for(usize planIndex = 0u; planIndex < planCount; ++planIndex){
        const MeshSkinningGraphDispatchPlan& plan = plans[planIndex];
        if(
            ((plan.hasActiveSkin || plan.copiedRestStreams) && (
                !finalizer.add(plan.skinnedPositionResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !finalizer.add(plan.skinnedNormalResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !finalizer.add(plan.skinnedTangentResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            ))
            || (plan.updatesMeshletBounds && !finalizer.add(plan.meshletBoundsResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read))
            || (plan.repacksNormals && !finalizer.add(plan.attributeResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning final states"));
            return false;
        }
    }
    if(finalizer.m_count == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning dispatch has no final state"));
        return false;
    }

    deformation.writeTo(outUses.deformation);
    postDispatch.writeTo(outUses.postDispatch);
    finalizer.writeTo(outUses.finalizer);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

