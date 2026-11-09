// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


class SimdLaneOperationEmitter final{
public:
    SimdLaneOperationEmitter(const SourceEmitter& emitter, MString& output, MStringView simdType, Span<const Expr* const> arguments, b8 isDouble, b8 isInt)noexcept
        : m_emitter(emitter)
        , m_output(output)
        , m_simdType(simdType)
        , m_arguments(arguments)
        , m_isDouble(isDouble)
        , m_isInt(isInt)
    {}

    [[nodiscard]] b8 tryEmit()const;

private:
    const SourceEmitter& m_emitter;
    MString& m_output;
    MStringView m_simdType;
    Span<const Expr* const> m_arguments;
    b8 m_isDouble;
    b8 m_isInt;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

