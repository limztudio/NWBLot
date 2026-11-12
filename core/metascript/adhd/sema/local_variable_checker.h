// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_checker.h"


NWB_ADHD_BEGIN


class LocalVariableChecker final{
public:
    explicit LocalVariableChecker(TypeChecker& checker)noexcept : m_checker(checker){}
    [[nodiscard]] b8 check(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors);

private:
    TypeChecker& m_checker;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

