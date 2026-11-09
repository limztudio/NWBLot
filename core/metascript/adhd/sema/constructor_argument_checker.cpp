// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "constructor_argument_checker.h"
#include "callable_argument_checker.h"


NWB_ADHD_BEGIN


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
        if(!IsValidBuiltinVectorOrMatrixCtorArity(baseTargetName, arguments.size())){
            m_checker.reportError(errors, errorSpan, "wrong number of arguments to built-in vector/matrix constructor");
            ok = false;
        }
        else if(arguments.size() == 2u && arguments[0] && !SymbolTable::IsSimdRegisterVectorType(baseTargetName)){
            const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(baseTargetName);
            const BuiltinUnalignedVectorInfo* unalignedInfo = simdVecInfo ? nullptr : FindBuiltinUnalignedVectorType(baseTargetName);
            const b8 requiresSimdRegArg0 = (simdVecInfo && simdVecInfo->lanes == 4u && simdVecInfo->altCtorArity != 2u) || (unalignedInfo && unalignedInfo->lanes == 4u && unalignedInfo->altCtorArity != 2u);
            if(requiresSimdRegArg0 && !SymbolTable::IsSimdRegisterVectorType(m_checker.inferExprTypeName(ownerClass, &method, *arguments[0], scope))){
                m_checker.reportError(errors, errorSpan, "wrong number of arguments to built-in vector/matrix constructor");
                ok = false;
            }
        }
        else if(
            arguments.size() > 1u
            && (
                BuiltinTypeSemantics::IsScalarType(baseTargetName)
                || m_checker.m_symbols.findEnum(baseTargetName) != nullptr
                || IsSingleValueCtorBuiltinType(baseTargetName)
            )
        ){
            m_checker.reportError(errors, errorSpan, "scalar, enum, or single-value constructor accepts at most 1 argument");
            ok = false;
        }
        if(const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(baseTargetName); simdVecInfo && simdVecInfo->isMixed && arguments.size() == 1u && arguments[0]){
            const MStringView arg0Type = m_checker.inferExprTypeName(ownerClass, &method, *arguments[0], scope);
            if(arg0Type != baseTargetName){
                m_checker.reportError(errors, errorSpan, "mixed float/integer SIMD vector type does not support single-scalar splat construction; provide all 4 lane arguments");
                ok = false;
            }
        }
    }
    if(!CallableArgumentChecker(m_checker).check(
        ownerClass,
        scope,
        matchedCtor,
        ctorCls,
        resolvedTargetType,
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


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

