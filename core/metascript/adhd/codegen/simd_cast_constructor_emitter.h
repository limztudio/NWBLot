// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


class SimdCastConstructorEmitter final{
public:
    SimdCastConstructorEmitter(const SourceEmitter& emitter, MString& output, Span<const Expr* const> arguments, b8 isDouble)noexcept
        : m_emitter(emitter)
        , m_output(output)
        , m_arguments(arguments)
        , m_isDouble(isDouble)
    {}

    [[nodiscard]] b8 tryEmit()const;

private:
    const SourceEmitter& m_emitter;
    MString& m_output;
    Span<const Expr* const> m_arguments;
    b8 m_isDouble;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

