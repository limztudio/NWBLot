#include "backend_contract.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(GraphicsContract::BackendApi<GraphicsBackend::Backend>);
static_assert(GraphicsContract::DeviceApi<GraphicsBackend::Device>);
static_assert(GraphicsContract::CommandListApi<GraphicsBackend::CommandList>);

static_assert(GraphicsContract::DescribedResourceApi<GraphicsBackend::Heap, HeapDesc>);
static_assert(GraphicsContract::TextureApi<GraphicsBackend::Texture>);
static_assert(GraphicsContract::DescribedResourceApi<GraphicsBackend::StagingTexture, TextureDesc>);
static_assert(GraphicsContract::BufferApi<GraphicsBackend::Buffer>);
static_assert(GraphicsContract::ShaderApi<GraphicsBackend::Shader>);
static_assert(GraphicsContract::ShaderLibraryApi<GraphicsBackend::ShaderLibrary>);
static_assert(GraphicsContract::DescribedResourceApi<GraphicsBackend::Sampler, SamplerDesc>);
static_assert(GraphicsContract::InputLayoutApi<GraphicsBackend::InputLayout>);
static_assert(GraphicsContract::FramebufferApi<GraphicsBackend::Framebuffer>);
static_assert(GraphicsContract::RayTracingOpacityMicromapApi<GraphicsBackend::RayTracingOpacityMicromap>);
static_assert(GraphicsContract::RayTracingAccelStructApi<GraphicsBackend::RayTracingAccelStruct>);
static_assert(GraphicsContract::BindingLayoutApi<GraphicsBackend::BindingLayout>);
static_assert(GraphicsContract::BindingSetApi<GraphicsBackend::BindingSet>);
static_assert(GraphicsContract::DescriptorTableApi<GraphicsBackend::DescriptorTable>);
static_assert(GraphicsContract::GraphicsPipelineApi<GraphicsBackend::GraphicsPipeline>);
static_assert(GraphicsContract::ComputePipelineApi<GraphicsBackend::ComputePipeline>);
static_assert(GraphicsContract::MeshletPipelineApi<GraphicsBackend::MeshletPipeline>);
static_assert(GraphicsContract::RayTracingShaderTableApi<GraphicsBackend::RayTracingShaderTable>);
static_assert(GraphicsContract::RayTracingPipelineApi<GraphicsBackend::RayTracingPipeline>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

