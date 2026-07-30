// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_FrameParamStorageSlotBytes = sizeof(u64);
inline constexpr usize s_FrameParamU32ValuesPerSlot = s_FrameParamStorageSlotBytes / sizeof(u32);
inline constexpr usize s_FrameParamU16ValuesPerSlot = s_FrameParamStorageSlotBytes / sizeof(u16);
inline constexpr usize s_FrameParamU8ValuesPerSlot = s_FrameParamStorageSlotBytes / sizeof(u8);


template<usize pointerSlots>
union FrameParamStorage{
    void* ptr[pointerSlots];
    u64 u64[pointerSlots];
    u32 u32[pointerSlots * s_FrameParamU32ValuesPerSlot];
    u16 u16[pointerSlots * s_FrameParamU16ValuesPerSlot];
    u8 u8[pointerSlots * s_FrameParamU8ValuesPerSlot];
};

template<usize pointerSlots>
class BasicFrameData{
public:
    using Param = FrameParamStorage<pointerSlots>;


public:
    inline BasicFrameData()
        : m_data{}
    {}


public:
    inline Param& frameParam(){ return m_data; }
    inline const Param& frameParam()const{ return m_data; }


protected:
    Param m_data;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

