// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_checker.h"


NWB_ADHD_BEGIN


class CallableTemplateDeduction final{
public:
    explicit CallableTemplateDeduction(const TypeChecker& checker)noexcept : m_checker(checker){}
    void deduce(const ClassDecl* ownerClass, const MethodDecl* enclosingMethod, const MethodDecl& targetCallable, Span<const Expr* const> arguments, Span<const ScopedVarInfo> scope, MVector<TypeArg>& outDeducedArgs)const;

private:
    const TypeChecker& m_checker;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

