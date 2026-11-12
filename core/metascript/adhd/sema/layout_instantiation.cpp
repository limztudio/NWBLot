// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "template_instantiation_collector.h"

#include "layout_optimizer.h"

#include "concrete_type_analyzer.h"
#include "type_comparator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TemplateInstantiationCollector::collectAllTemplateInstantiations(TranslationUnit& unit){
    for(const u32 pass : { 0u, 1u }){
        const b8 wantImported = (pass == 0u);
        m_collectingImported = wantImported;

        for(const Stmt* c : unit.globalConstants){
            if(!c || c->isImported != wantImported)
                continue;
            if(c->varType)
                collectTemplateInstantiationsInType(*c->varType, unit);
            if(c->expr)
                collectTemplateInstantiationsInExpr(*c->expr, unit);
            for(const Expr* arg : c->arguments){
                if(arg)
                    collectTemplateInstantiationsInExpr(*arg, unit);
            }
        }
        for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
            if(aliasDecl && aliasDecl->isImported == wantImported && aliasDecl->templateParams.empty() && aliasDecl->aliasedType)
                collectTemplateInstantiationsInType(*aliasDecl->aliasedType, unit);
        }
        for(const InterfaceDecl* ifaceDecl : unit.interfaces){
            if(!ifaceDecl || ifaceDecl->isImported != wantImported || !ifaceDecl->templateParams.empty())
                continue;
            for(const TypeRef* baseIface : ifaceDecl->baseInterfaces){
                if(baseIface)
                    collectTemplateInstantiationsInType(*baseIface, unit);
            }
            for(const MethodDecl* m : ifaceDecl->methods){
                if(m)
                    collectTemplateInstantiationsInCallable(*m, unit);
            }
        }
        for(ClassDecl* classDecl : unit.classes){
            if(classDecl->isImported == wantImported){
                for(const TypeRef* implIface : classDecl->implementedInterfaces){
                    if(implIface)
                        collectTemplateInstantiationsInType(*implIface, unit);
                }
                for(const FieldDecl* f : classDecl->fields){
                    if(!f)
                        continue;
                    if(f->type)
                        collectTemplateInstantiationsInType(*f->type, unit);
                    if(f->defaultExpr)
                        collectTemplateInstantiationsInExpr(*f->defaultExpr, unit);
                    for(const Expr* arg : f->ctorArgs){
                        if(arg)
                            collectTemplateInstantiationsInExpr(*arg, unit);
                    }
                }
            }
            for(const MethodDecl* m : classDecl->methods){
                if(m && m->isImported == wantImported)
                    collectTemplateInstantiationsInCallable(*m, unit);
            }
        }
        for(const MethodDecl* fn : unit.freeFunctions){
            if(fn && fn->isImported == wantImported)
                collectTemplateInstantiationsInCallable(*fn, unit);
        }
        for(const MethodDecl* m : unit.outOfLineMethods){
            if(m && m->isImported == wantImported)
                collectTemplateInstantiationsInCallable(*m, unit);
        }
    }
    m_collectingImported = false;
}

void TemplateInstantiationCollector::collectTemplateInstantiationsInCallable(const MethodDecl& callable, TranslationUnit& unit){
    if(callable.returnType)
        collectTemplateInstantiationsInType(*callable.returnType, unit);
    for(const ParamDecl& p : callable.params){
        if(p.type)
            collectTemplateInstantiationsInType(*p.type, unit);
    }
    for(const Stmt* s : callable.body){
        if(s)
            collectTemplateInstantiationsInStmt(*s, unit);
    }
}

void TemplateInstantiationCollector::collectTemplateInstantiationsInStmt(const Stmt& stmt, TranslationUnit& unit){
    if(stmt.varType)
        collectTemplateInstantiationsInType(*stmt.varType, unit);
    if(stmt.inferredAutoType)
        collectTemplateInstantiationsInType(*stmt.inferredAutoType, unit);
    if(stmt.expr)
        collectTemplateInstantiationsInExpr(*stmt.expr, unit);
    if(stmt.condition)
        collectTemplateInstantiationsInExpr(*stmt.condition, unit);
    if(stmt.stepExpr)
        collectTemplateInstantiationsInExpr(*stmt.stepExpr, unit);
    for(const Expr* arg : stmt.arguments){
        if(arg)
            collectTemplateInstantiationsInExpr(*arg, unit);
    }
    for(const NativeBinding& nb : stmt.nativeBindings){
        if(nb.sourceExpr)
            collectTemplateInstantiationsInExpr(*nb.sourceExpr, unit);
    }
    if(stmt.initStmt)
        collectTemplateInstantiationsInStmt(*stmt.initStmt, unit);
    for(const Stmt* sub : stmt.statements){
        if(sub)
            collectTemplateInstantiationsInStmt(*sub, unit);
    }
    for(const Stmt* sub : stmt.elseStatements){
        if(sub)
            collectTemplateInstantiationsInStmt(*sub, unit);
    }
    for(const SwitchCase& sc : stmt.switchCases){
        if(sc.matchExpr)
            collectTemplateInstantiationsInExpr(*sc.matchExpr, unit);
        for(const Stmt* sub : sc.statements){
            if(sub)
                collectTemplateInstantiationsInStmt(*sub, unit);
        }
    }
}

void TemplateInstantiationCollector::collectTemplateInstantiationsInExpr(const Expr& expr, TranslationUnit& unit){
    if(expr.targetType)
        collectTemplateInstantiationsInType(*expr.targetType, unit);
    for(const TypeArg& targ : expr.callTemplateArgs){
        if(targ.type)
            collectTemplateInstantiationsInType(*targ.type, unit);
    }
    for(const TypeArg& targ : expr.deducedTemplateArgs){
        if(targ.type)
            collectTemplateInstantiationsInType(*targ.type, unit);
    }
    const Span<const TypeArg> effectiveTemplateArgs = !expr.callTemplateArgs.empty()
        ? Span<const TypeArg>(expr.callTemplateArgs)
        : Span<const TypeArg>(expr.deducedTemplateArgs);
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        if(!effectiveTemplateArgs.empty()){
            if(const ClassDecl* cls = m_symbols.findClass(expr.lhs->text)){
                if(!cls->templateParams.empty() && effectiveTemplateArgs.size() == cls->templateParams.size())
                    instantiateClassTemplate(*cls, effectiveTemplateArgs, unit);
            }
            else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(expr.lhs->text)){
                if(aliasDecl->aliasedType && !aliasDecl->templateParams.empty() && effectiveTemplateArgs.size() == aliasDecl->templateParams.size()){
                    const TypeRef* resolvedAlias = m_symbols.substituteTypeParams(*aliasDecl->aliasedType, aliasDecl->templateParams, effectiveTemplateArgs);
                    collectTemplateInstantiationsInType(*resolvedAlias, unit);
                }
            }
            else if(const MethodDecl* fn = m_symbols.findFreeFunction(expr.lhs->text)){
                if(!fn->templateParams.empty() && effectiveTemplateArgs.size() == fn->templateParams.size() && ConcreteTypeAnalyzer(m_symbols).isConcreteTemplateArgList(fn->templateParams, effectiveTemplateArgs)){
                    if(fn->returnType){
                        if(const TypeRef* subRet = m_symbols.substituteTypeParams(*fn->returnType, fn->templateParams, effectiveTemplateArgs))
                            collectTemplateInstantiationsInType(*subRet, unit);
                    }
                    for(const ParamDecl& p : fn->params){
                        if(!p.type)
                            continue;
                        if(const TypeRef* subParam = m_symbols.substituteTypeParams(*p.type, fn->templateParams, effectiveTemplateArgs))
                            collectTemplateInstantiationsInType(*subParam, unit);
                    }
                    for(const Stmt* s : fn->body){
                        if(s)
                            collectSubstitutedTemplateInstantiationsInStmt(*s, fn->templateParams, effectiveTemplateArgs, unit);
                    }
                }
            }
        }
        else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(expr.lhs->text)){
            if(aliasDecl->aliasedType && aliasDecl->templateParams.empty())
                collectTemplateInstantiationsInType(*aliasDecl->aliasedType, unit);
        }
    }
    if(expr.lhs)
        collectTemplateInstantiationsInExpr(*expr.lhs, unit);
    if(expr.rhs)
        collectTemplateInstantiationsInExpr(*expr.rhs, unit);
    for(const Expr* arg : expr.arguments){
        if(arg)
            collectTemplateInstantiationsInExpr(*arg, unit);
    }
    if(expr.lambdaReturnType)
        collectTemplateInstantiationsInType(*expr.lambdaReturnType, unit);
    for(const ParamDecl& p : expr.lambdaParams){
        if(p.type)
            collectTemplateInstantiationsInType(*p.type, unit);
    }
    for(const Stmt* s : expr.lambdaBody){
        if(s)
            collectTemplateInstantiationsInStmt(*s, unit);
    }
}

void TemplateInstantiationCollector::collectSubstitutedTemplateInstantiationsInStmt(const Stmt& stmt, const Span<const TemplateParamDecl> params, const Span<const TypeArg> args, TranslationUnit& unit){
    if(stmt.varType){
        if(const TypeRef* sub = m_symbols.substituteTypeParams(*stmt.varType, params, args))
            collectTemplateInstantiationsInType(*sub, unit);
    }
    if(stmt.inferredAutoType){
        if(const TypeRef* sub = m_symbols.substituteTypeParams(*stmt.inferredAutoType, params, args))
            collectTemplateInstantiationsInType(*sub, unit);
    }
    if(stmt.expr)
        collectSubstitutedTemplateInstantiationsInExpr(*stmt.expr, params, args, unit);
    if(stmt.condition)
        collectSubstitutedTemplateInstantiationsInExpr(*stmt.condition, params, args, unit);
    if(stmt.stepExpr)
        collectSubstitutedTemplateInstantiationsInExpr(*stmt.stepExpr, params, args, unit);
    for(const Expr* arg : stmt.arguments){
        if(arg)
            collectSubstitutedTemplateInstantiationsInExpr(*arg, params, args, unit);
    }
    if(stmt.initStmt)
        collectSubstitutedTemplateInstantiationsInStmt(*stmt.initStmt, params, args, unit);
    for(const Stmt* sub : stmt.statements){
        if(sub)
            collectSubstitutedTemplateInstantiationsInStmt(*sub, params, args, unit);
    }
    for(const Stmt* sub : stmt.elseStatements){
        if(sub)
            collectSubstitutedTemplateInstantiationsInStmt(*sub, params, args, unit);
    }
    for(const SwitchCase& sc : stmt.switchCases){
        if(sc.matchExpr)
            collectSubstitutedTemplateInstantiationsInExpr(*sc.matchExpr, params, args, unit);
        for(const Stmt* sub : sc.statements){
            if(sub)
                collectSubstitutedTemplateInstantiationsInStmt(*sub, params, args, unit);
        }
    }
}

void TemplateInstantiationCollector::collectSubstitutedTemplateInstantiationsInExpr(const Expr& expr, const Span<const TemplateParamDecl> params, const Span<const TypeArg> args, TranslationUnit& unit){
    if(expr.targetType){
        if(const TypeRef* sub = m_symbols.substituteTypeParams(*expr.targetType, params, args))
            collectTemplateInstantiationsInType(*sub, unit);
    }
    const Span<const TypeArg> effectiveArgs = !expr.callTemplateArgs.empty()
        ? Span<const TypeArg>(expr.callTemplateArgs)
        : Span<const TypeArg>(expr.deducedTemplateArgs);
    if(!effectiveArgs.empty() && expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        TypeRef tempCallType(m_arena);
        tempCallType.name = expr.lhs->text;
        for(const TypeArg& targ : effectiveArgs)
            tempCallType.templateArgs.push_back(targ);
        if(const TypeRef* sub = m_symbols.substituteTypeParams(tempCallType, params, args))
            collectTemplateInstantiationsInType(*sub, unit);
    }
    if(expr.lhs)
        collectSubstitutedTemplateInstantiationsInExpr(*expr.lhs, params, args, unit);
    if(expr.rhs)
        collectSubstitutedTemplateInstantiationsInExpr(*expr.rhs, params, args, unit);
    for(const Expr* arg : expr.arguments){
        if(arg)
            collectSubstitutedTemplateInstantiationsInExpr(*arg, params, args, unit);
    }
    for(const Stmt* s : expr.lambdaBody){
        if(s)
            collectSubstitutedTemplateInstantiationsInStmt(*s, params, args, unit);
    }
}

void TemplateInstantiationCollector::collectTemplateInstantiationsInType(const TypeRef& type, const u32 depth, TranslationUnit& unit){
    if(depth > 16u)
        return;
    if(type.isFuncType){
        if(type.funcReturnType)
            collectTemplateInstantiationsInType(*type.funcReturnType, depth + 1u, unit);
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type)
                collectTemplateInstantiationsInType(*fp.type, depth + 1u, unit);
        }
    }
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type)
            collectTemplateInstantiationsInType(*arg.type, depth + 1u, unit);
    }
    if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
        if(unwrapped != &type)
            collectTemplateInstantiationsInType(*unwrapped, depth + 1u, unit);
    }
    if(const ClassDecl* cls = m_symbols.findClass(type.name)){
        if(!cls->templateParams.empty() && type.templateArgs.size() == cls->templateParams.size())
            instantiateClassTemplate(*cls, type.templateArgs, unit);
    }
    else if(const InterfaceDecl* iface = m_symbols.findInterface(type.name)){
        if(!iface->templateParams.empty() && type.templateArgs.size() == iface->templateParams.size()){
            for(const TypeRef* baseIface : iface->baseInterfaces){
                if(!baseIface)
                    continue;
                if(const TypeRef* subBase = m_symbols.substituteTypeParams(*baseIface, iface->templateParams, type.templateArgs))
                    collectTemplateInstantiationsInType(*subBase, depth + 1u, unit);
            }
            for(const MethodDecl* m : iface->methods){
                if(!m)
                    continue;
                if(m->returnType){
                    if(const TypeRef* subRet = m_symbols.substituteTypeParams(*m->returnType, iface->templateParams, type.templateArgs))
                        collectTemplateInstantiationsInType(*subRet, depth + 1u, unit);
                }
                for(const ParamDecl& p : m->params){
                    if(!p.type)
                        continue;
                    if(const TypeRef* subParam = m_symbols.substituteTypeParams(*p.type, iface->templateParams, type.templateArgs))
                        collectTemplateInstantiationsInType(*subParam, depth + 1u, unit);
                }
            }
        }
    }
}

void TemplateInstantiationCollector::instantiateClassTemplate(const ClassDecl& templateDecl, const Span<const TypeArg> templateArgs, TranslationUnit& unit){
    if(!ConcreteTypeAnalyzer(m_symbols).isConcreteTemplateArgList(templateDecl.templateParams, templateArgs))
        return;

    for(const ClassTemplateInstantiation* existing : unit.templateInstantiations){
        if(existing->templateDecl == &templateDecl && TypeComparator(m_symbols).areTemplateArgListsEqual(existing->templateArgs, templateArgs))
            return;
    }

    ClassTemplateInstantiation* inst = AllocateAstNode<ClassTemplateInstantiation>(m_arena);
    inst->templateDecl = &templateDecl;
    inst->needsClassArena = templateDecl.needsClassArena;
    inst->isImported = m_collectingImported;
    for(const TypeArg& targ : templateArgs)
        inst->templateArgs.push_back(targ);

    for(const FieldDecl* origField : templateDecl.fields){
        if(!origField || !origField->type)
            continue;

        FieldDecl* clonedField = AllocateAstNode<FieldDecl>(m_arena);
        clonedField->access = origField->access;
        clonedField->name = origField->name;
        clonedField->defaultExpr = origField->defaultExpr;
        clonedField->span = origField->span;
        clonedField->originalIndex = origField->originalIndex;
        clonedField->hasCtorInit = origField->hasCtorInit;
        clonedField->usesClassArena = origField->usesClassArena;
        clonedField->arenaFirstDefaultInit = origField->arenaFirstDefaultInit;
        clonedField->isTransient = origField->isTransient;
        clonedField->isReflect = origField->isReflect;
        for(Expr* arg : origField->ctorArgs)
            clonedField->ctorArgs.push_back(arg);

        TypeRef* resolvedType = m_symbols.substituteTypeParams(*origField->type, templateDecl.templateParams, templateArgs);
        clonedField->type = resolvedType;
        m_optimizer.markFieldArenaUsage(*resolvedType, *clonedField, templateDecl, inst->needsClassArena);
        collectTemplateInstantiationsInType(*resolvedType, unit);
        inst->orderedFields.push_back(clonedField);
    }

    const b8 hasMotherArenaPointer = !templateDecl.isSerialize && !templateDecl.usesStaticClassArena() && templateDecl.subsystemTypeName.empty() && inst->needsClassArena;
    m_optimizer.finalizeClassFieldLayouts(templateDecl.isSerialize, !templateDecl.implementedInterfaces.empty() || !templateDecl.assetTypeName.empty(), hasMotherArenaPointer, inst->orderedFields, inst->totalSize, inst->totalAlign);
    unit.templateInstantiations.push_back(inst);

    for(const TypeRef* implIface : templateDecl.implementedInterfaces){
        if(!implIface)
            continue;
        if(const TypeRef* resolvedIface = m_symbols.substituteTypeParams(*implIface, templateDecl.templateParams, templateArgs))
            collectTemplateInstantiationsInType(*resolvedIface, unit);
    }
    for(const MethodDecl* method : templateDecl.methods){
        if(!method || !method->templateParams.empty())
            continue;
        if(method->returnType){
            if(const TypeRef* resolvedRet = m_symbols.substituteTypeParams(*method->returnType, templateDecl.templateParams, templateArgs))
                collectTemplateInstantiationsInType(*resolvedRet, unit);
        }
        for(const ParamDecl& p : method->params){
            if(!p.type)
                continue;
            if(const TypeRef* resolvedParam = m_symbols.substituteTypeParams(*p.type, templateDecl.templateParams, templateArgs))
                collectTemplateInstantiationsInType(*resolvedParam, unit);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

