// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "callable_template_deduction.h"
#include "type_resolver.h"


NWB_ADHD_BEGIN


void CallableTemplateDeduction::deduce(
    const ClassDecl* ownerClass,
    const MethodDecl* enclosingMethod,
    const MethodDecl& targetCallable,
    const Span<const Expr* const> arguments,
    const Span<const ScopedVarInfo> scope,
    MVector<TypeArg>& outDeducedArgs
)const{
    outDeducedArgs.clear();
    outDeducedArgs.reserve(targetCallable.templateParams.size());
    for(const TemplateParamDecl& tp : targetCallable.templateParams){
        TypeArg dArg;
        if(!tp.constValueType){
            for(usize k = 0u; k < arguments.size() && k < targetCallable.params.size(); ++k){
                const Expr* arg = arguments[k];
                const TypeRef* pType = targetCallable.params[k].type;
                if(!arg || !pType)
                    continue;
                if(pType->name == tp.name && pType->templateArgs.empty()){
                    const TypeRef* argRef = nullptr;
                    const MStringView argBase = m_checker.inferExprBaseType(ownerClass, enclosingMethod, *arg, scope, &argRef);
                    if(argRef){
                        dArg.type = const_cast<TypeRef*>(argRef);
                        break;
                    }
                    if(!argBase.empty()){
                        TypeRef* synth = AllocateAstNode<TypeRef>(m_checker.m_arena);
                        synth->name = argBase;
                        dArg.type = synth;
                        break;
                    }
                }
                else if(!pType->templateArgs.empty()){
                    const TypeRef* argRef = m_checker.inferExprTypeRef(ownerClass, enclosingMethod, *arg, scope);
                    if(const TypeRef* sub = TypeResolver(m_checker.m_symbols).deduceTemplateParamTypeRef(pType, argRef, tp.name)){
                        dArg.type = const_cast<TypeRef*>(sub);
                        break;
                    }
                }
            }
        }
        outDeducedArgs.push_back(dArg);
    }
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
