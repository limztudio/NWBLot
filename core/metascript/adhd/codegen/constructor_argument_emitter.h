// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "source_emitter.h"


NWB_ADHD_BEGIN


class ConstructorArgumentEmitter final{
public:
    explicit ConstructorArgumentEmitter(const SourceEmitter& emitter)noexcept : m_emitter(emitter){}
    void emit(MString& out, MStringView baseTypeName, Span<const Expr* const> arguments, MStringView injectedArenaName = {}, b8 useBracesForList = false, b8 emptyUsesBraces = true)const;

private:
    const SourceEmitter& m_emitter;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

