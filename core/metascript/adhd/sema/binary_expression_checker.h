// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_checker.h"


NWB_ADHD_BEGIN


class BinaryExpressionChecker final{
public:
    explicit BinaryExpressionChecker(TypeChecker& checker)noexcept : m_checker(checker){}
    [[nodiscard]] b8 check(ClassDecl* ownerClass, Span<const ScopedVarInfo> scope, MethodDecl& method, Expr& expr, MVector<ParseError>& errors);

private:
    [[nodiscard]] b8 applyInterfaceRefRebind(
        const ClassDecl* ownerClass,
        const MethodDecl& method,
        const TypeRef* targetType,
        ParamPassingMode::Enum targetRefMode,
        Span<const ScopedVarInfo> scope,
        MStringView errorMessage,
        Expr& assignExpr,
        MVector<ParseError>& errors,
        b8& inOutRhsNeedsMut
    );
    TypeChecker& m_checker;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
