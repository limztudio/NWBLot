// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_checker.h"


NWB_ADHD_BEGIN


class CallableArgumentChecker final{
public:
    explicit CallableArgumentChecker(TypeChecker& checker)noexcept : m_checker(checker){}
    [[nodiscard]] b8 check(
        ClassDecl* ownerClass, Span<const ScopedVarInfo> scope, const MethodDecl* targetCallable, const ClassDecl* targetOwnerCls,
        const TypeRef* recvTypeRef, Span<const TypeArg> callTemplateArgs, const TypeRef* invokedFuncType, const ScopedVarInfo* localCallable,
        MStringView calledMemberName, MethodDecl& method, MVector<Expr*>& arguments, MVector<ParseError>& errors
    );

private:
    TypeChecker& m_checker;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
