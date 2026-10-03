// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/vulkan/backend_context.h>

#include <global/allocation_size.h>
#include <global/hash_utils.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_pipeline_imports{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CompatiblePipelineMetadata(
    const GpuTaskGraphPipelineView& pipeline,
    const GpuGraphPipelineDesc& desc)noexcept{
    // Identity and concrete pipeline kind define the graph-side table key.  Marker text is observational metadata,
    // matching resource imports where a later compatible import reuses the original graph-owned label.
    return pipeline.identity == desc.identity && pipeline.type == desc.type;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GpuTaskGraph::PipelinePointerHasher::operator()(const PipelinePointerKey& key)const noexcept{
    usize hash = Hasher<const void*>{}(key.pointer);
    HashCombine(hash, static_cast<u8>(key.type));
    return hash;
}

GpuTaskGraph::PipelinePointerKey GpuTaskGraph::pipelinePointerKey(const GpuGraphPipelineNode& pipeline)noexcept{
    switch(pipeline.type){
    case GpuGraphPipelineType::Graphics:
        return { pipeline.graphicsPipeline.get(), pipeline.type };
    case GpuGraphPipelineType::Compute:
        return { pipeline.computePipeline.get(), pipeline.type };
    case GpuGraphPipelineType::Meshlet:
        return { pipeline.meshletPipeline.get(), pipeline.type };
    case GpuGraphPipelineType::RayTracing:
        return { pipeline.rayTracingPipeline.get(), pipeline.type };
    default:
        return {};
    }
}

GpuGraphPipelineId GpuTaskGraph::importPipeline(const GpuGraphPipelineDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphPipelineType::kCount)
        return {};

    const u32 pipelineIndex = findPipelineIdentity(desc.identity);
    if(pipelineIndex != s_InvalidImportIndex){
        if(!__hidden_gpu_task_graph_pipeline_imports::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
            return {};
        return GpuGraphPipelineId{ .generation = m_generation, .index = pipelineIndex };
    }

    return appendPipeline(desc, {});
}

GpuGraphPipelineId GpuTaskGraph::importGraphicsPipeline(
    const GraphicsPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Graphics)
        return {};

    const PipelineImportMatch match = findPipelineImportMatch(desc.identity, { pipeline.get(), desc.type });
    if(match.index != s_InvalidImportIndex){
        if(
            !match.samePointer
            || !__hidden_gpu_task_graph_pipeline_imports::CompatiblePipelineMetadata(pipelineAt(match.index), desc)
        )
            return {};
        return GpuGraphPipelineId{ .generation = m_generation, .index = match.index };
    }

    return appendPipeline(desc, { .graphicsPipeline = &pipeline, .deviceGeneration = pipeline->getDeviceGeneration() });
}

GpuGraphPipelineId GpuTaskGraph::importComputePipeline(
    const ComputePipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Compute)
        return {};

    const PipelineImportMatch match = findPipelineImportMatch(desc.identity, { pipeline.get(), desc.type });
    if(match.index != s_InvalidImportIndex){
        if(
            !match.samePointer
            || !__hidden_gpu_task_graph_pipeline_imports::CompatiblePipelineMetadata(pipelineAt(match.index), desc)
        )
            return {};
        return GpuGraphPipelineId{ .generation = m_generation, .index = match.index };
    }

    return appendPipeline(desc, { .computePipeline = &pipeline, .deviceGeneration = pipeline->getDeviceGeneration() });
}

GpuGraphPipelineId GpuTaskGraph::importMeshletPipeline(
    const MeshletPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Meshlet)
        return {};

    const PipelineImportMatch match = findPipelineImportMatch(desc.identity, { pipeline.get(), desc.type });
    if(match.index != s_InvalidImportIndex){
        if(
            !match.samePointer
            || !__hidden_gpu_task_graph_pipeline_imports::CompatiblePipelineMetadata(pipelineAt(match.index), desc)
        )
            return {};
        return GpuGraphPipelineId{ .generation = m_generation, .index = match.index };
    }

    return appendPipeline(desc, { .meshletPipeline = &pipeline, .deviceGeneration = pipeline->getDeviceGeneration() });
}

GpuGraphPipelineId GpuTaskGraph::importRayTracingPipeline(
    const RayTracingPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::RayTracing)
        return {};

    const PipelineImportMatch match = findPipelineImportMatch(desc.identity, { pipeline.get(), desc.type });
    if(match.index != s_InvalidImportIndex){
        if(
            !match.samePointer
            || !__hidden_gpu_task_graph_pipeline_imports::CompatiblePipelineMetadata(pipelineAt(match.index), desc)
        )
            return {};
        return GpuGraphPipelineId{ .generation = m_generation, .index = match.index };
    }

    return appendPipeline(desc, { .rayTracingPipeline = &pipeline, .deviceGeneration = pipeline->getDeviceGeneration() });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 GpuTaskGraph::findPipelineIdentity(const Name& identity)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_pipelineIdentityIndex){
        const auto found = m_pipelineIdentityIndex->find(key);
        return found == m_pipelineIdentityIndex->end() ? s_InvalidImportIndex : found.value();
    }
    for(usize index = 0u; index < m_pipelines.size(); ++index){
        if(m_pipelines[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

GpuTaskGraph::PipelineImportMatch GpuTaskGraph::findPipelineImportMatch(
    const Name& identity,
    const PipelinePointerKey& pointer)const noexcept{
    if(!pointer.pointer)
        return {};
    if(m_pipelineIdentityIndex){
        PipelineImportMatch match;
        if(m_pipelinePointerIndex){
            const auto found = m_pipelinePointerIndex->find(pointer);
            if(found != m_pipelinePointerIndex->end())
                match = { found.value(), true };
        }
        const auto found = m_pipelineIdentityIndex->find(identity.identityHash());
        // Pointer equality wins at the same ordinal; an earlier identity conflict retains its rejection priority.
        if(found != m_pipelineIdentityIndex->end() && found.value() < match.index)
            return { found.value(), false };
        return match;
    }
    for(usize index = 0u; index < m_pipelines.size(); ++index){
        const GpuGraphPipelineNode& existing = m_pipelines[index];
        if(existing.type == pointer.type && pipelinePointerKey(existing).pointer == pointer.pointer)
            return { static_cast<u32>(index), true };
        if(existing.identity == identity)
            return { static_cast<u32>(index), false };
    }
    return {};
}

void GpuTaskGraph::preparePipelineIndexes(const PipelinePointerKey& pendingPointer){
    if(!m_pipelineIdentityIndex){
        if(m_pipelines.size() < s_InlineImportIndexCount)
            return;

        const usize identityCount = AddSize(m_pipelines.size(), 1u);
        ImportIdentityIndex identities(AddSize(identityCount, identityCount), m_arena);
        usize pointerCount = pendingPointer.pointer ? 1u : 0u;
        for(usize index = 0u; index < m_pipelines.size(); ++index){
            const GpuGraphPipelineNode& pipeline = m_pipelines[index];
            if(!identities.emplace(pipeline.identity.identityHash(), static_cast<u32>(index)).second){
                NWB_FATAL_ASSERT_MSG(false, "Pipeline import index requires unique retained identities");
                TerminateInvariant();
            }
            if(pipelinePointerKey(pipeline).pointer)
                ++pointerCount;
        }
        Optional<PipelinePointerIndex> pointers;
        if(pointerCount != 0u){
            pointers.emplace(AddSize(pointerCount, pointerCount), m_arena);
            for(usize index = 0u; index < m_pipelines.size(); ++index){
                const PipelinePointerKey key = pipelinePointerKey(m_pipelines[index]);
                if(key.pointer && !pointers->emplace(key, static_cast<u32>(index)).second){
                    NWB_FATAL_ASSERT_MSG(false, "Pipeline import index requires unique retained typed pointers");
                    TerminateInvariant();
                }
            }
        }

        // Publish only indexes of retained nodes. Failed appends may keep capacity without changing lookup results.
        static_assert(IsNothrowMoveConstructible_V<ImportIdentityIndex>);
        static_assert(IsNothrowMoveConstructible_V<PipelinePointerIndex>);
        m_pipelineIdentityIndex.emplace(Move(identities));
        if(pointers)
            m_pipelinePointerIndex.emplace(Move(*pointers));
    }
    else if(pendingPointer.pointer && !m_pipelinePointerIndex)
        m_pipelinePointerIndex.emplace(2u, m_arena);
}

GpuGraphPipelineId GpuTaskGraph::appendPipeline(const GpuGraphPipelineDesc& desc, const PipelineBinding& binding){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || desc.type >= GpuGraphPipelineType::kCount
        || m_pipelines.size() >= Limit<u32>::s_Max
    )
        return {};

    GpuGraphPipelineNode pipeline;
    pipeline.identity = desc.identity;
    pipeline.type = desc.type;
    pipeline.deviceGeneration = binding.deviceGeneration;
    if(binding.graphicsPipeline)
        pipeline.graphicsPipeline = *binding.graphicsPipeline;
    if(binding.computePipeline)
        pipeline.computePipeline = *binding.computePipeline;
    if(binding.meshletPipeline)
        pipeline.meshletPipeline = *binding.meshletPipeline;
    if(binding.rayTracingPipeline)
        pipeline.rayTracingPipeline = *binding.rayTracingPipeline;
    const PipelinePointerKey pointerKey = pipelinePointerKey(pipeline);
    const NameHash& identityKey = desc.identity.identityHash();

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_pipelines, m_pipelines.size() + 1u);
    preparePipelineIndexes(pointerKey);

    const usize markerCount = m_markerText.size();
    const usize pipelineCount = m_pipelines.size();
    bool identityInserted = false;
    bool pointerInserted = false;
    ScopeExit appendRollback([&]()noexcept{
        if(pointerInserted)
            m_pipelinePointerIndex->erase(pointerKey);
        if(identityInserted)
            m_pipelineIdentityIndex->erase(identityKey);
        while(m_pipelines.size() > pipelineCount)
            m_pipelines.pop_back();
        while(m_markerText.size() > markerCount)
            m_markerText.pop_back();
    });
    if(!appendMarkerLabel(desc.markerLabel, pipeline.markerLabelOffset, pipeline.markerLabelSize))
        return {};

    const u32 index = static_cast<u32>(m_pipelines.size());
    m_pipelines.push_back(Move(pipeline));
    if(m_pipelineIdentityIndex){
        identityInserted = m_pipelineIdentityIndex->emplace(identityKey, index).second;
        if(!identityInserted)
            return {};
        if(pointerKey.pointer){
            pointerInserted = m_pipelinePointerIndex->emplace(pointerKey, index).second;
            if(!pointerInserted)
                return {};
        }
    }
    appendRollback.release();
    m_declarationRevision = allocateGeneration();
    return GpuGraphPipelineId{ .generation = m_generation, .index = index };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

