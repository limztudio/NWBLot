// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph_reflection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportReflectionTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    AStringView resourceLabel = "Reflection Output",
    bool firstWrite = true
);
[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportReflectionBuffer(Core::GpuTaskGraph& graph, const Core::BufferHandle& buffer);
[[nodiscard]] Core::GpuTaskDesc ReflectionTaskDesc(Name identity, AStringView label, const Core::GpuTaskId& dependency)noexcept;

[[nodiscard]] Expected<Core::GpuTaskId> DeclareReflectionDepthPyramidTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    const ReflectionDepthPyramidSnapshot& pyramid,
    Core::GpuGraphResourceId opaqueDepth,
    Core::GpuTaskId dependency,
    Core::GpuGraphResourceId& outDepthPyramid
);

[[nodiscard]] Expected<Core::GpuTaskId> DeclareReflectionPostprocessTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    ReflectionGraphResult& result
);

[[nodiscard]] Expected<Core::GpuTaskId> DeclareReflectionStatisticsAndFinalizeTasks(
    Core::GpuTaskGraph& graph,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    bool feedbackReserved,
    Core::GpuTaskId dependency,
    ReflectionGraphResult& result
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

