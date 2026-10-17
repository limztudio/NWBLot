// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pipeline.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsPipelineDesc::~GraphicsPipelineDesc() = default;
GraphicsPipelineDesc& GraphicsPipelineDesc::setInputLayout(const InputLayoutHandle& value)noexcept{ inputLayout = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setVertexShader(const ShaderHandle& value)noexcept{ vertexShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setHullShader(const ShaderHandle& value)noexcept{ hullShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setTessellationControlShader(const ShaderHandle& value)noexcept{ hullShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setDomainShader(const ShaderHandle& value)noexcept{ domainShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setTessellationEvaluationShader(const ShaderHandle& value)noexcept{ domainShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setGeometryShader(const ShaderHandle& value)noexcept{ geometryShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setPixelShader(const ShaderHandle& value)noexcept{ pixelShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::setFragmentShader(const ShaderHandle& value)noexcept{ pixelShader = value; return *this; }
GraphicsPipelineDesc& GraphicsPipelineDesc::addBindingLayout(const BindingLayoutHandle& layout){ bindingLayouts.push_back(layout); return *this; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipelineDesc::~ComputePipelineDesc() = default;
ComputePipelineDesc& ComputePipelineDesc::setComputeShader(const ShaderHandle& value)noexcept{ computeShader = value; return *this; }
ComputePipelineDesc& ComputePipelineDesc::addBindingLayout(const BindingLayoutHandle& layout){ bindingLayouts.push_back(layout); return *this; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipelineDesc::~MeshletPipelineDesc() = default;
MeshletPipelineDesc& MeshletPipelineDesc::setTaskShader(const ShaderHandle& value)noexcept{ amplificationShader = value; return *this; }
MeshletPipelineDesc& MeshletPipelineDesc::setAmplificationShader(const ShaderHandle& value)noexcept{ amplificationShader = value; return *this; }
MeshletPipelineDesc& MeshletPipelineDesc::setMeshShader(const ShaderHandle& value)noexcept{ meshShader = value; return *this; }
MeshletPipelineDesc& MeshletPipelineDesc::setPixelShader(const ShaderHandle& value)noexcept{ pixelShader = value; return *this; }
MeshletPipelineDesc& MeshletPipelineDesc::setFragmentShader(const ShaderHandle& value)noexcept{ pixelShader = value; return *this; }
MeshletPipelineDesc& MeshletPipelineDesc::addBindingLayout(const BindingLayoutHandle& layout){ bindingLayouts.push_back(layout); return *this; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipelineShaderDesc::~RayTracingPipelineShaderDesc() = default;
RayTracingPipelineShaderDesc& RayTracingPipelineShaderDesc::setShader(const ShaderHandle& value)noexcept{ shader = value; return *this; }
RayTracingPipelineShaderDesc& RayTracingPipelineShaderDesc::setBindingLayout(const BindingLayoutHandle& value)noexcept{ bindingLayout = value; return *this; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipelineHitGroupDesc::~RayTracingPipelineHitGroupDesc() = default;
RayTracingPipelineHitGroupDesc& RayTracingPipelineHitGroupDesc::setClosestHitShader(const ShaderHandle& value)noexcept{ closestHitShader = value; return *this; }
RayTracingPipelineHitGroupDesc& RayTracingPipelineHitGroupDesc::setAnyHitShader(const ShaderHandle& value)noexcept{ anyHitShader = value; return *this; }
RayTracingPipelineHitGroupDesc& RayTracingPipelineHitGroupDesc::setIntersectionShader(const ShaderHandle& value)noexcept{ intersectionShader = value; return *this; }
RayTracingPipelineHitGroupDesc& RayTracingPipelineHitGroupDesc::setBindingLayout(const BindingLayoutHandle& value)noexcept{ bindingLayout = value; return *this; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipelineDesc::~RayTracingPipelineDesc() = default;
RayTracingPipelineDesc& RayTracingPipelineDesc::addShader(const RayTracingPipelineShaderDesc& value){ shaders.push_back(value); return *this; }
RayTracingPipelineDesc& RayTracingPipelineDesc::addHitGroup(const RayTracingPipelineHitGroupDesc& value){ hitGroups.push_back(value); return *this; }
RayTracingPipelineDesc& RayTracingPipelineDesc::addBindingLayout(const BindingLayoutHandle& value){ globalBindingLayouts.push_back(value); return *this; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

