// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../math_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SourceShMathInternal{


using SourceMathInternal::FXMMATRIX;
using SourceMathInternal::VectorArg;


static constexpr size_t SH_MIN_ORDER = 2;
static constexpr size_t SH_MAX_ORDER = 6;


float* MathCallConv SHEvalDirection(float* result, size_t order, VectorArg dir)noexcept;
float* MathCallConv SHRotate(float* result, size_t order, FXMMATRIX rotMatrix, const float* input)noexcept;
float* SHRotateZ(float* result, size_t order, float angle, const float* input)noexcept;
float* SHAdd(float* result, size_t order, const float* inputA, const float* inputB)noexcept;
float* SHScale(float* result, size_t order, const float* input, float scale)noexcept;
float SHDot(size_t order, const float* inputA, const float* inputB)noexcept;
float* SHMultiply(float* result, size_t order, const float* inputF, const float* inputG)noexcept;
float* SHMultiply2(float* result, const float* inputF, const float* inputG)noexcept;
float* SHMultiply3(float* result, const float* inputF, const float* inputG)noexcept;
float* SHMultiply4(float* result, const float* inputF, const float* inputG)noexcept;
float* SHMultiply5(float* result, const float* inputF, const float* inputG)noexcept;
float* SHMultiply6(float* result, const float* inputF, const float* inputG)noexcept;

bool MathCallConv SHEvalDirectionalLight(
    size_t order,
    VectorArg dir,
    VectorArg color,
    float* resultR,
    float* resultG,
    float* resultB
)noexcept;

bool MathCallConv SHEvalSphericalLight(
    size_t order,
    VectorArg position,
    float radius,
    VectorArg color,
    float* resultR,
    float* resultG,
    float* resultB
)noexcept;

bool MathCallConv SHEvalConeLight(
    size_t order,
    VectorArg dir,
    float radius,
    VectorArg color,
    float* resultR,
    float* resultG,
    float* resultB
)noexcept;

bool MathCallConv SHEvalHemisphereLight(
    size_t order,
    VectorArg dir,
    VectorArg topColor,
    VectorArg bottomColor,
    float* resultR,
    float* resultG,
    float* resultB
)noexcept;


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



