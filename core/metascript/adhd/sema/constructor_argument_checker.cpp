// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "constructor_argument_checker.h"
#include "callable_argument_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ConstructorArgumentChecker::check(ClassDecl* ownerClass, const TypeRef* targetType, const Span<const ScopedVarInfo> scope, const SourceSpan errorSpan, MethodDecl& method, MVector<Expr*>& arguments, MVector<ParseError>& errors){
    b8 ok = true;
    const TypeRef* resolvedTargetType = targetType ? m_checker.m_symbols.unwrapTypeAlias(targetType) : nullptr;
    const MStringView baseTargetName = resolvedTargetType
        ? m_checker.m_symbols.resolveBaseTypeName(resolvedTargetType->name)
        : MStringView{};
    const ClassDecl* ctorCls = !baseTargetName.empty()
        ? m_checker.m_symbols.findClass(baseTargetName)
        : nullptr;
    const MethodDecl* matchedCtor = nullptr;
    if(ctorCls){
        MVector<MStringView> argTypeHints(m_checker.m_arena);
        MVector<u8> argIsMutHints(m_checker.m_arena);
        m_checker.buildCallArgHints(ownerClass, &method, arguments, scope, argTypeHints, argIsMutHints);
        matchedCtor = m_checker.m_symbols.findClassConstructor(*ctorCls, arguments.size(), ownerClass != ctorCls, argTypeHints, argIsMutHints);
        if(matchedCtor && ownerClass != ctorCls && matchedCtor->access != AccessSpecifier::Public){
            m_checker.reportError(errors, errorSpan, "cannot call 'private' or 'protected' constructor outside of its declaring class");
            ok = false;
        }
    }
    else if(resolvedTargetType && !baseTargetName.empty()){
        if(!m_checker.checkBuiltinVectorOrMatrixCtorCall(ownerClass, method, baseTargetName, arguments, scope, errorSpan, errors, true))
            ok = false;
    }
    if(!CallableArgumentChecker(m_checker).check(
        ownerClass,
        scope,
        matchedCtor,
        ctorCls,
        resolvedTargetType,
        nullptr,
        false,
        true,
        {},
        nullptr,
        nullptr,
        MStringView{},
        method,
        arguments,
        errors
    )){
        ok = false;
    }
    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

