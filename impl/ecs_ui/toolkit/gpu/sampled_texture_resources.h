// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer.h"

#include <impl/ecs_ui/toolkit/gpu/push_constants.adhd.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GpuRasterResourceUses = FixedVector<Core::GpuTaskResourceUse, s_PaintMaxImages + s_GpuRasterFixedResourceUses>;

struct GpuSampledTextureImportDesc{
    Name readyPrefix;
    AStringView readyMarkerLabel;
    TStringView completionLabel;
    Core::QueueSubmissionToken readinessToken;
    Name texturePrefix;
    AStringView textureMarkerLabel;
    Core::TextureHandle texture;
    usize index = 0u;
};

struct GpuPageTextureUploadDesc{
    Name name;
    const void* data = nullptr;
    usize dataSize = 0u;
    usize rowPitch = 0u;
    u32 width = 0u;
    u32 height = 0u;
    Core::Format::Enum format = Core::Format::UNKNOWN;
};


class GpuSampledTextures final{
public:
    [[nodiscard]] static b8 prepareDescriptor(Core::GraphicsRuntime& graphics, const Core::TextureHandle& texture, Core::Format::Enum format, Core::GpuDescriptorHandle& inOutDescriptor);
    [[nodiscard]] static b8 createAndUploadPage(Core::GraphicsRuntime& graphics, const GpuPageTextureUploadDesc& desc, Core::TextureHandle& outTexture, Core::QueueSubmissionToken& outReadinessToken);
    [[nodiscard]] static Expected<Core::GpuGraphResourceId, GpuSampledTextureImportError::Enum> importTexture(const GpuSampledTextureImportDesc& desc, Core::GpuTaskGraph& graph, GpuRasterResourceUses& uses);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

