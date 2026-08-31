// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_pipeline_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    ::HashCombine(seed, static_cast<u32>(key.pass));
    ::HashCombine(seed, key.twoSided ? 1u : 0u);
    ::HashCombine(seed, static_cast<u32>(key.csgMode));
    ::HashCombine(seed, Hasher<Name>{}(key.csgEvaluatorVariant));
    ::HashCombine(seed, key.framebufferInfo.depthFormat);
    ::HashCombine(seed, key.framebufferInfo.sampleCount);
    ::HashCombine(seed, key.framebufferInfo.sampleQuality);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        ::HashCombine(seed, format);

    return seed;
}


bool MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return
        lhs.material == rhs.material
        && lhs.pass == rhs.pass
        && lhs.twoSided == rhs.twoSided
        && lhs.csgMode == rhs.csgMode
        && lhs.csgEvaluatorVariant == rhs.csgEvaluatorVariant
        && lhs.framebufferInfo == rhs.framebufferInfo
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

