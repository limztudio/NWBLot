// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_checker.h"


NWB_ADHD_BEGIN


class ConstructorArgumentChecker final{
public:
    explicit ConstructorArgumentChecker(TypeChecker& checker)noexcept : m_checker(checker){}
    [[nodiscard]] b8 check(ClassDecl* ownerClass, const TypeRef* targetType, Span<const ScopedVarInfo> scope, SourceSpan errorSpan, MethodDecl& method, MVector<Expr*>& arguments, MVector<ParseError>& errors);

private:
    TypeChecker& m_checker;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
