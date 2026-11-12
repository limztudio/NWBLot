// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_checker.h"


NWB_ADHD_BEGIN


class ReturnStatementChecker final{
public:
    explicit ReturnStatementChecker(TypeChecker& checker)noexcept : m_checker(checker){}
    [[nodiscard]] b8 check(ClassDecl* ownerClass, Span<const ScopedVarInfo> scope, MethodDecl& method, Stmt& stmt, MVector<ParseError>& errors);

private:
    TypeChecker& m_checker;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

