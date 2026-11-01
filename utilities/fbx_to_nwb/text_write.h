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
inline constexpr i32 s_OutputFloatPrecision = 9;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr f32 CleanFloat(const f32 value)noexcept{
    return (value < 0.0f ? -value : value) < s_CleanFloatEpsilon ? 0.0f : value;
}

template<typename Stream>
void WriteFloat(const f32 value, Stream& out)noexcept(noexcept(out << CleanFloat(value))){
    out << CleanFloat(value);
}

template<typename Stream>
void WriteVec2(const Float2U& value, Stream& out)noexcept(noexcept(out << "[") && noexcept(WriteFloat(value.x, out)) && noexcept(out << ", ") && noexcept(WriteFloat(value.y, out)) && noexcept(out << "]")){
    out << "[";
    WriteFloat(value.x, out);
    out << ", ";
    WriteFloat(value.y, out);
    out << "]";
}

template<typename Stream>
void WriteVec3(const Float3U& value, Stream& out)noexcept(noexcept(out << "[") && noexcept(WriteFloat(value.x, out)) && noexcept(out << ", ") && noexcept(WriteFloat(value.y, out)) && noexcept(WriteFloat(value.z, out)) && noexcept(out << "]")){
    out << "[";
    WriteFloat(value.x, out);
    out << ", ";
    WriteFloat(value.y, out);
    out << ", ";
    WriteFloat(value.z, out);
    out << "]";
}

template<typename Stream>
void WriteVec4(const Float4U& value, Stream& out)noexcept(
    noexcept(out << "[")
    && noexcept(WriteFloat(value.x, out))
    && noexcept(out << ", ")
    && noexcept(WriteFloat(value.y, out))
    && noexcept(WriteFloat(value.z, out))
    && noexcept(WriteFloat(value.w, out))
    && noexcept(out << "]")
){
    out << "[";
    WriteFloat(value.x, out);
    out << ", ";
    WriteFloat(value.y, out);
    out << ", ";
    WriteFloat(value.z, out);
    out << ", ";
    WriteFloat(value.w, out);
    out << "]";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

