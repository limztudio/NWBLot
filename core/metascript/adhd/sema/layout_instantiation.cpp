// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout_optimizer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LayoutOptimizer::collectAllTemplateInstantiations(TranslationUnit& unit){
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
                if(!m)
                    continue;
                if(m->returnType)
                    collectTemplateInstantiationsInType(*m->returnType, unit);
                for(const ParamDecl& p : m->params){
                    if(p.type)
                        collectTemplateInstantiationsInType(*p.type, unit);
                }
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
                if(!m || m->isImported != wantImported)
                    continue;
                if(m->returnType)
                    collectTemplateInstantiationsInType(*m->returnType, unit);
                for(const ParamDecl& p : m->params){
                    if(p.type)
                        collectTemplateInstantiationsInType(*p.type, unit);
                }
                for(const Stmt* s : m->body){
                    if(s)
                        collectTemplateInstantiationsInStmt(*s, unit);
                }
            }
        }
        for(const MethodDecl* fn : unit.freeFunctions){
            if(!fn || fn->isImported != wantImported)
                continue;
            if(fn->returnType)
                collectTemplateInstantiationsInType(*fn->returnType, unit);
            for(const ParamDecl& p : fn->params){
                if(p.type)
                    collectTemplateInstantiationsInType(*p.type, unit);
            }
            for(const Stmt* s : fn->body){
                if(s)
                    collectTemplateInstantiationsInStmt(*s, unit);
            }
        }
        for(const MethodDecl* m : unit.outOfLineMethods){
            if(!m || m->isImported != wantImported)
                continue;
            if(m->returnType)
                collectTemplateInstantiationsInType(*m->returnType, unit);
            for(const ParamDecl& p : m->params){
                if(p.type)
                    collectTemplateInstantiationsInType(*p.type, unit);
            }
            for(const Stmt* s : m->body){
                if(s)
                    collectTemplateInstantiationsInStmt(*s, unit);
            }
        }
    }
    m_collectingImported = false;
}

void LayoutOptimizer::collectTemplateInstantiationsInStmt(const Stmt& stmt, TranslationUnit& unit){
    if(stmt.varType)
        collectTemplateInstantiationsInType(*stmt.varType, unit);
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

void LayoutOptimizer::collectTemplateInstantiationsInExpr(const Expr& expr, TranslationUnit& unit){
    if(expr.targetType)
        collectTemplateInstantiationsInType(*expr.targetType, unit);
    for(const TypeArg& targ : expr.callTemplateArgs){
        if(targ.type)
            collectTemplateInstantiationsInType(*targ.type, unit);
    }
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        if(!expr.callTemplateArgs.empty()){
            if(const ClassDecl* cls = m_symbols.findClass(expr.lhs->text)){
                if(!cls->templateParams.empty() && expr.callTemplateArgs.size() == cls->templateParams.size())
                    instantiateClassTemplate(*cls, expr.callTemplateArgs, unit);
            }
            else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(expr.lhs->text)){
                if(aliasDecl->aliasedType && !aliasDecl->templateParams.empty() && expr.callTemplateArgs.size() == aliasDecl->templateParams.size()){
                    const TypeRef* resolvedAlias = m_symbols.substituteTypeParams(
                        *aliasDecl->aliasedType,
                        aliasDecl->templateParams,
                        expr.callTemplateArgs
                    );
                    collectTemplateInstantiationsInType(*resolvedAlias, unit);
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

void LayoutOptimizer::collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit, const u32 depth){
    if(depth > 16u)
        return;
    if(type.isFuncType){
        if(type.funcReturnType)
            collectTemplateInstantiationsInType(*type.funcReturnType, unit, depth + 1u);
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type)
                collectTemplateInstantiationsInType(*fp.type, unit, depth + 1u);
        }
    }
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type)
            collectTemplateInstantiationsInType(*arg.type, unit, depth + 1u);
    }
    if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
        if(unwrapped != &type)
            collectTemplateInstantiationsInType(*unwrapped, unit, depth + 1u);
    }
    if(const ClassDecl* cls = m_symbols.findClass(type.name)){
        if(!cls->templateParams.empty() && type.templateArgs.size() == cls->templateParams.size())
            instantiateClassTemplate(*cls, type.templateArgs, unit);
    }
}

void LayoutOptimizer::instantiateClassTemplate(
    const ClassDecl& templateDecl,
    const MVector<TypeArg>& templateArgs,
    TranslationUnit& unit
){
    if(!IsConcreteTemplateArgList(templateDecl.templateParams, templateArgs, m_symbols))
        return;

    for(const ClassTemplateInstantiation* existing : unit.templateInstantiations){
        if(existing->templateDecl == &templateDecl && AreTemplateArgListsEqual(existing->templateArgs, templateArgs, m_symbols))
            return;
    }

    ClassTemplateInstantiation* inst = AllocateAstNode<ClassTemplateInstantiation>(m_arena);
    inst->templateDecl = &templateDecl;
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
        clonedField->isTransient = origField->isTransient;
        clonedField->isReflect = origField->isReflect;
        for(Expr* arg : origField->ctorArgs)
            clonedField->ctorArgs.push_back(arg);

        TypeRef* resolvedType = m_symbols.substituteTypeParams(
            *origField->type,
            templateDecl.templateParams,
            templateArgs
        );
        clonedField->type = resolvedType;
        if(m_symbols.resolveRefMode(resolvedType) == ParamPassingMode::None && SymbolTable::isArenaBackedContainer(m_symbols.resolveBaseTypeName(resolvedType->name))){
            clonedField->usesClassArena = true;
            const_cast<ClassDecl&>(templateDecl).needsClassArena = true;
        }
        else if(TypeContainsArenaBackedContainer(*resolvedType, m_symbols)){
            const_cast<ClassDecl&>(templateDecl).needsClassArena = true;
        }
        collectTemplateInstantiationsInType(*resolvedType, unit);

        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*resolvedType, fieldSize, fieldAlign);
        clonedField->resolvedSize = fieldSize;
        clonedField->resolvedAlign = templateDecl.isSerialize ? 1u : fieldAlign;
        inst->orderedFields.push_back(clonedField);
    }

    sortAndPackFields(
        inst->orderedFields,
        templateDecl.isSerialize,
        !templateDecl.implementedInterfaces.empty(),
        inst->totalSize,
        inst->totalAlign
    );
    unit.templateInstantiations.push_back(inst);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

