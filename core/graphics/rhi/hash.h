// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "device.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<NWB::Core::TextureSubresourceSet>{
    size_t operator()(NWB::Core::TextureSubresourceSet const& s)const noexcept{
        usize hash = 0;
        ::HashCombine(hash, s.baseMipLevel);
        ::HashCombine(hash, s.numMipLevels);
        ::HashCombine(hash, s.baseArraySlice);
        ::HashCombine(hash, s.numArraySlices);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BufferRange>{
    size_t operator()(NWB::Core::BufferRange const& s)const noexcept{
        usize hash = 0;
        ::HashCombine(hash, s.byteOffset);
        ::HashCombine(hash, s.byteSize);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BindingSetItem>{
    size_t operator()(NWB::Core::BindingSetItem const& s)const noexcept{
        usize value = 0;
        ::HashCombine(value, s.resourceHandle);
        ::HashCombine(value, s.slot);
        ::HashCombine(value, s.arrayElement);
        ::HashCombine(value, s.type);
        ::HashCombine(value, s.dimension);
        ::HashCombine(value, s.format);
        ::HashCombine(value, s.rawData[0]);
        ::HashCombine(value, s.rawData[1]);
        return static_cast<size_t>(value);
    }
};

template<>
struct hash<NWB::Core::BindingSetDesc>{
    size_t operator()(NWB::Core::BindingSetDesc const& s)const noexcept{
        usize value = 0;
        ::HashCombine(value, s.trackLiveness);
        for(const auto& item : s.bindings)
            ::HashCombine(value, item);
        return static_cast<size_t>(value);
    }
};

template<>
struct hash<NWB::Core::FramebufferInfo>{
    size_t operator()(NWB::Core::FramebufferInfo const& s)const noexcept{
        usize hash = 0;
        for(const auto format : s.colorFormats)
            ::HashCombine(hash, format);
        ::HashCombine(hash, s.depthFormat);
        ::HashCombine(hash, s.sampleCount);
        ::HashCombine(hash, s.sampleQuality);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BlendState::RenderTarget>{
    size_t operator()(NWB::Core::BlendState::RenderTarget const& s)const noexcept{
        usize hash = 0;
        ::HashCombine(hash, s.blendEnable);
        ::HashCombine(hash, s.srcBlend);
        ::HashCombine(hash, s.destBlend);
        ::HashCombine(hash, s.blendOp);
        ::HashCombine(hash, s.srcBlendAlpha);
        ::HashCombine(hash, s.destBlendAlpha);
        ::HashCombine(hash, s.blendOpAlpha);
        ::HashCombine(hash, s.colorWriteMask);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BlendState>{
    size_t operator()(NWB::Core::BlendState const& s)const noexcept{
        usize hash = 0;
        ::HashCombine(hash, s.alphaToCoverageEnable);
        for(const auto& target : s.targets)
            ::HashCombine(hash, target);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::VariableRateShadingState>{
    size_t operator()(NWB::Core::VariableRateShadingState const& s)const noexcept{
        usize hash = 0;
        ::HashCombine(hash, s.enabled);
        ::HashCombine(hash, s.shadingRate);
        ::HashCombine(hash, s.pipelinePrimitiveCombiner);
        ::HashCombine(hash, s.imageCombiner);
        return static_cast<size_t>(hash);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

