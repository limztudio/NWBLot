// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextWrite{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_CleanFloatEpsilon = 0.00000001f;
inline constexpr int s_OutputFloatPrecision = 9;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline f32 CleanFloat(const f32 value)noexcept{
    if(VectorGetX(VectorAbs(VectorReplicate(value))) < s_CleanFloatEpsilon)
        return 0.0f;
    return value;
}

template<typename Stream>
void WriteFloat(Stream& out, const f32 value)noexcept(noexcept(out << CleanFloat(value))){
    out << CleanFloat(value);
}

template<typename Stream>
void WriteVec2(Stream& out, const Float2U& value)noexcept(noexcept(out << "[") && noexcept(WriteFloat(out, value.x)) && noexcept(out << ", ") && noexcept(WriteFloat(out, value.y)) && noexcept(out << "]")){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << "]";
}

template<typename Stream>
void WriteVec3(Stream& out, const Float3U& value)noexcept(noexcept(out << "[") && noexcept(WriteFloat(out, value.x)) && noexcept(out << ", ") && noexcept(WriteFloat(out, value.y)) && noexcept(WriteFloat(out, value.z)) && noexcept(out << "]")){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << ", ";
    WriteFloat(out, value.z);
    out << "]";
}

template<typename Stream>
void WriteVec4(Stream& out, const Float4U& value)noexcept(
    noexcept(out << "[")
    && noexcept(WriteFloat(out, value.x))
    && noexcept(out << ", ")
    && noexcept(WriteFloat(out, value.y))
    && noexcept(WriteFloat(out, value.z))
    && noexcept(WriteFloat(out, value.w))
    && noexcept(out << "]")
){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << ", ";
    WriteFloat(out, value.z);
    out << ", ";
    WriteFloat(out, value.w);
    out << "]";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

