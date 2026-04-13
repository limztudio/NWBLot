// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize pointerSlots>
union FrameParamStorage{
    void* ptr[pointerSlots];
    u64 u64[pointerSlots];
    u32 u32[pointerSlots * 2];
    u16 u16[pointerSlots * 4];
    u8 u8[pointerSlots * 8];
};

template<usize pointerSlots>
class BasicFrameData{
public:
    using Param = FrameParamStorage<pointerSlots>;


public:
    inline BasicFrameData() : m_data{}{}


public:
    inline Param& frameParam(){ return m_data; }
    inline const Param& frameParam()const{ return m_data; }


protected:
    Param m_data;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

