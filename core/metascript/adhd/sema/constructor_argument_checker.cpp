// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "constructor_argument_checker.h"
#include "callable_argument_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ConstructorArgumentChecker::markRetainedParameters(const ClassDecl& owner, MethodDecl& ctor, const Span<const Stmt* const> statements, MVector<ParseError>& errors)const{
    b8 ok = true;
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;
        if(IsThisMemberSimpleAssignStmt(*stmt)){
            const FieldDecl* field = SymbolTable::FindClassField(owner, stmt->expr->lhs->text);
            const Expr* source = stmt->expr->rhs;
            if(field && m_checker.m_symbols.resolveRefMode(field->type) == ParamPassingMode::In){
                while(source && (source->kind == ExprKind::MemberAccess || source->kind == ExprKind::Index))
                    source = source->lhs;
                if(source && source->kind == ExprKind::Identifier){
                    for(ParamDecl& param : ctor.params){
                        if(param.name != source->text || param.mode != ParamPassingMode::In || param.retainsBorrow)
                            continue;
                        if(ctor.isOutOfLine){
                            m_checker.reportError(errors, param.span, "declare the retained constructor parameter as 'in borrow' on the class declaration");
                            ok = false;
                        }
                        else
                            param.retainsBorrow = true;
                    }
                }
            }
        }
        if(!markRetainedParameters(owner, ctor, stmt->statements, errors))
            ok = false;
        if(!markRetainedParameters(owner, ctor, stmt->elseStatements, errors))
            ok = false;
        for(const SwitchCase& arm : stmt->switchCases){
            if(!markRetainedParameters(owner, ctor, arm.statements, errors))
                ok = false;
        }
    }
    return ok;
}

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
        matchedCtor = m_checker.m_symbols.findClassConstructor(*ctorCls, arguments.size(), !m_checker.m_symbols.canAccessPrivateMembers(*ctorCls, ownerClass), argTypeHints, argIsMutHints);
        if(matchedCtor && !m_checker.m_symbols.canAccessPrivateMembers(*ctorCls, ownerClass) && matchedCtor->access != AccessSpecifier::Public){
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
    if(resolvedTargetType && m_checker.wrappedReferenceType(resolvedTargetType) && arguments.size() == 1u && arguments[0]){
        if(!m_checker.checkWrappedReferenceArgument(ownerClass, method, *resolvedTargetType, *arguments[0], scope, errors))
            ok = false;
    }
    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

