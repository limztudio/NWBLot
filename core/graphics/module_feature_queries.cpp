// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_feature_queries{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_DefaultWaveLaneCount = 64u;


constexpr bool IsFp16CoopVecFormat(const CooperativeVectorMatMulFormatCombo& combo){
    return
        combo.inputType == CooperativeVectorDataType::Float16
        && combo.inputInterpretation == CooperativeVectorDataType::Float16
        && combo.matrixInterpretation == CooperativeVectorDataType::Float16
        && combo.outputType == CooperativeVectorDataType::Float16
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Graphics::queryFeatureSupport(const Feature::Enum feature, void* featureInfo, const usize featureInfoSize)const{
    auto& device = getDevice();
    return device.queryFeatureSupport(feature, featureInfo, featureInfoSize);
}

u32 Graphics::queryWaveLaneCount()const noexcept{
    WaveLaneCountMinMaxFeatureInfo info{};
    if(queryFeatureSupport(Feature::WaveLaneCountMinMax, &info, sizeof(info)) && info.maxWaveLaneCount > 0u)
        return info.maxWaveLaneCount;
    // Conservative fallback for backends/paths that cannot report a wave size: 64 lanes is the safe upper
    // bound across all desktop GPUs and keeps groupshared reductions correct without wave intrinsics.
    return __hidden_graphics_feature_queries::s_DefaultWaveLaneCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Graphics::CoopVectorSupport Graphics::queryCoopVecSupport()const{
    CoopVectorSupport output;

    output.inferencingSupported = queryFeatureSupport(Feature::CooperativeVectorInferencing);
    output.trainingSupported = queryFeatureSupport(Feature::CooperativeVectorTraining);

    auto& device = getDevice();
    const CooperativeVectorDeviceFeatures features = device.queryCoopVecFeatures();
    output.fp32TrainingSupported = output.trainingSupported && features.trainingFloat32;

    for(const auto& combo : features.matMulFormats){
        if(__hidden_graphics_feature_queries::IsFp16CoopVecFormat(combo)){
            output.fp16InferencingSupported = output.inferencingSupported;
            output.fp16TrainingSupported = output.trainingSupported && features.trainingFloat16;
            break;
        }
    }

    return output;
}

CooperativeVectorDeviceFeatures Graphics::queryCoopVecFeatures()const{
    auto& device = getDevice();
    return device.queryCoopVecFeatures();
}

usize Graphics::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const{
    auto& device = getDevice();
    return device.getCoopVecMatrixSize(type, layout, rows, columns);
}

void Graphics::waitJob(JobHandle handle)const{
    if(!handle.valid())
        return;

    m_jobSystem.wait(handle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

