// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"
#include "header_preamble_emitter.h"

#include "../sema/declaration_dependency_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString HeaderEmitter::emitHeader(const TranslationUnit& unit, const MStringView namespacePath)const{
    m_typeEmitter.setCurrentNamespace(namespacePath);
    m_sourceEmitter.setCurrentNamespace(namespacePath);
    const DeclarationDependencyAnalyzer declarationDependencies(m_arena, m_symbols);
    MString out(m_arena);
    out.reserve(s_AdhdEmitReserveBytes);


    HeaderPreambleEmitter::Emit(out, unit, namespacePath);

    for(const EnumDecl* enumDecl : unit.enums){
        if(enumDecl && !enumDecl->isImported && !enumDecl->isExtern)
            emitEnumDecl(out, *enumDecl);
    }

    MVector<MStringView> externalForwardClasses(m_arena);
    const auto collectTypeForwardDecls = [&](const TypeRef& t, const Span<const TemplateParamDecl> outerTemplateParams, const Span<const TemplateParamDecl> innerTemplateParams, const b8 includeDirectRef){
        declarationDependencies.collectExternalForwardDeclClasses(t, outerTemplateParams, innerTemplateParams, includeDirectRef, 0u, externalForwardClasses);
    };
    const auto collectCallableForwardDeclClasses = [&](const MethodDecl& callable, const Span<const TemplateParamDecl> outerTemplateParams, const Span<const TemplateParamDecl> innerTemplateParams){
        if(callable.returnType)
            collectTypeForwardDecls(*callable.returnType, outerTemplateParams, innerTemplateParams, false);
        for(const ParamDecl& p : callable.params){
            if(p.type)
                collectTypeForwardDecls(*p.type, outerTemplateParams, innerTemplateParams, true);
        }
    };
    for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(aliasDecl && !aliasDecl->isImported && !aliasDecl->isExtern && aliasDecl->aliasedType)
            collectTypeForwardDecls(*aliasDecl->aliasedType, aliasDecl->templateParams, {}, false);
    }
    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(!ifaceDecl || ifaceDecl->isImported || ifaceDecl->isExtern)
            continue;
        for(const MethodDecl* m : ifaceDecl->methods){
            if(m && !m->isExtern)
                collectCallableForwardDeclClasses(*m, ifaceDecl->templateParams, {});
        }
    }
    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl || classDecl->isImported || classDecl->isExtern)
            continue;
        for(const MStringView peer : classDecl->friendClasses){
            const ClassDecl* known = m_symbols.findClass(peer);
            if((!known || known->isImported) && !ContainsStringView(externalForwardClasses, peer))
                externalForwardClasses.push_back(peer);
        }
        for(const FieldDecl* f : classDecl->fields){
            if(f && !f->isStatic && f->type)
                collectTypeForwardDecls(*f->type, classDecl->templateParams, {}, false);
        }
        for(const MethodDecl* m : classDecl->methods){
            if(m && !m->isExtern)
                collectCallableForwardDeclClasses(*m, classDecl->templateParams, m->templateParams);
        }
    }
    for(const MethodDecl* fn : unit.freeFunctions){
        if(fn && !fn->isImported && !fn->isExtern)
            collectCallableForwardDeclClasses(*fn, fn->templateParams, {});
    }

    b8 emittedForwardDecl = false;
    for(const MStringView extCls : externalForwardClasses){
        emittedForwardDecl = true;
        out.append("class ");
        out.append(extCls.data(), extCls.size());
        out.append(";\r\n");
    }
    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(!ifaceDecl || ifaceDecl->isImported || ifaceDecl->isExtern)
            continue;
        emittedForwardDecl = true;
        m_typeEmitter.emitTemplateHeaderCpp(out, ifaceDecl->templateParams);
        out.append("class ");
        out.append(ifaceDecl->name.data(), ifaceDecl->name.size());
        out.append(";\r\n");
    }
    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl || classDecl->isImported || classDecl->isExtern)
            continue;
        emittedForwardDecl = true;
        m_typeEmitter.emitTemplateHeaderCpp(out, classDecl->templateParams);
        out.append("class ");
        out.append(classDecl->name.data(), classDecl->name.size());
        out.append(";\r\n");
    }
    if(emittedForwardDecl)
        out.append("\r\n");

    const auto typeRefUsesAnyName = [&](const auto& self, const TypeRef& type, const HashMap<MStringView, u8, MetaArena, MStringViewHash>& names, const u32 depth)noexcept -> b8{
        if(depth > 16u)
            return false;
        if(names.find(type.name) != names.end())
            return true;
        if(type.isFuncType){
            if(type.funcReturnType && self(self, *type.funcReturnType, names, depth + 1u))
                return true;
            for(const FuncParamType& fp : type.funcParams){
                if(fp.type && self(self, *fp.type, names, depth + 1u))
                    return true;
            }
        }
        for(const TypeArg& targ : type.templateArgs){
            if(targ.type && self(self, *targ.type, names, depth + 1u))
                return true;
        }
        return false;
    };

    const auto typeRefUsesAnyClassByValue = [&](const auto& self, const TypeRef& type, const HashMap<MStringView, u8, MetaArena, MStringViewHash>& classNames, const u32 depth)noexcept -> b8{
        if(depth > 16u || type.isFuncType || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
            return false;
        if(classNames.find(type.name) != classNames.end())
            return true;
        if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
            if(unwrapped != &type && self(self, *unwrapped, classNames, depth + 1u))
                return true;
        }
        if(IsAssetHandleType(m_symbols.resolveBaseTypeName(type.name)))
            return false;
        if(const ClassDecl* cls = m_symbols.findClass(type.name); cls && !cls->templateParams.empty()){
            if(cls->concreteBase && classNames.find(cls->concreteBase->name) != classNames.end())
                return true;
            for(const FieldDecl* field : cls->fields){
                if(field && field->type && self(self, *field->type, classNames, depth + 1u))
                    return true;
            }
        }
        for(const TypeArg& targ : type.templateArgs){
            if(targ.type && self(self, *targ.type, classNames, depth + 1u))
                return true;
        }
        return false;
    };

    HashMap<MStringView, u8, MetaArena, MStringViewHash> localAliasNames(m_arena);
    MVector<const TypeAliasDecl*> localAliases(m_arena);
    for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(aliasDecl && !aliasDecl->isImported && !aliasDecl->isExtern){
            localAliasNames.emplace(aliasDecl->name, 1u);
            localAliases.push_back(aliasDecl);
        }
    }

    HashMap<MStringView, u8, MetaArena, MStringViewHash> localClassNames(m_arena);
    MVector<const ClassDecl*> localClasses(m_arena);
    for(const ClassDecl* classDecl : unit.classes){
        if(classDecl && !classDecl->isImported && !classDecl->isExtern){
            localClassNames.emplace(classDecl->name, 1u);
            localClasses.push_back(classDecl);
        }
    }

    MVector<const Stmt*> preAliasConsts(m_arena);
    MVector<const Stmt*> preClassConsts(m_arena);
    MVector<const Stmt*> postClassConsts(m_arena);
    for(const Stmt* c : unit.globalConstants){
        if(!c || c->isImported || c->isExtern || !c->varType)
            continue;
        if(!localClassNames.empty() && typeRefUsesAnyClassByValue(typeRefUsesAnyClassByValue, *c->varType, localClassNames, 0u)){
            postClassConsts.push_back(c);
            continue;
        }
        if(!localAliasNames.empty() && typeRefUsesAnyName(typeRefUsesAnyName, *c->varType, localAliasNames, 0u)){
            preClassConsts.push_back(c);
            continue;
        }
        preAliasConsts.push_back(c);
    }

    const auto emitGlobalConstBucket = [&](const Span<const Stmt* const> bucket){
        if(bucket.empty())
            return;
        for(const Stmt* c : bucket)
            emitGlobalConstDecl(out, *c);
        out.append("\r\n\r\n");
    };

    emitGlobalConstBucket(Span<const Stmt* const>(preAliasConsts.data(), preAliasConsts.size()));

    if(!localAliases.empty()){
        MVector<const TypeAliasDecl*> sortedAliases(m_arena);
        declarationDependencies.sortTypeAliasesTopologically(localAliases, sortedAliases);
        for(const TypeAliasDecl* aliasDecl : sortedAliases){
            if(aliasDecl)
                emitTypeAliasDecl(out, *aliasDecl);
        }
    }

    emitGlobalConstBucket(Span<const Stmt* const>(preClassConsts.data(), preClassConsts.size()));

    MVector<const InterfaceDecl*> localInterfaces(m_arena);
    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(ifaceDecl && !ifaceDecl->isImported && !ifaceDecl->isExtern)
            localInterfaces.push_back(ifaceDecl);
    }
    if(!localInterfaces.empty()){
        MVector<const InterfaceDecl*> sortedInterfaces(m_arena);
        declarationDependencies.sortInterfacesTopologically(localInterfaces, sortedInterfaces);
        for(const InterfaceDecl* ifaceDecl : sortedInterfaces){
            if(ifaceDecl)
                emitInterfaceDecl(out, *ifaceDecl);
        }
    }

    const b8 emitFreeFnPrototypesEarly = !unit.classes.empty() || unit.freeFunctions.size() > 1u;
    if(emitFreeFnPrototypesEarly){
        for(const MethodDecl* fn : unit.freeFunctions){
            if(fn && !fn->isImported && !fn->isExtern)
                m_sourceEmitter.emitOutOfClassCallable(out, nullptr, *fn, false, false, true);
        }
    }

    if(!localClasses.empty() || !unit.templateInstantiations.empty()){
        MVector<const ClassDecl*> sortedLocalClasses(m_arena);
        declarationDependencies.sortClassesTopologically(localClasses, sortedLocalClasses);

        MVector<u8> instEmitted(unit.templateInstantiations.size(), 0u, m_arena);
        MVector<u8> localClassReady(sortedLocalClasses.size(), 0u, m_arena);
        HashMap<const ClassDecl*, usize, MetaArena> localClassIndex(m_arena);
        localClassIndex.reserve(sortedLocalClasses.size());
        for(usize idx = 0u; idx < sortedLocalClasses.size(); ++idx){
            if(sortedLocalClasses[idx])
                localClassIndex.emplace(sortedLocalClasses[idx], idx);
        }

        const auto isClassReady = [&](const ClassDecl* target)noexcept -> b8{
            if(!target)
                return false;
            if(target->isImported || target->isExtern)
                return true;
            const auto it = localClassIndex.find(target);
            return it != localClassIndex.end() && localClassReady[it->second] != 0u;
        };

        const auto emitReadyInstantiations = [&](){
            if(unit.templateInstantiations.empty())
                return;
            b8 progress = true;
            while(progress){
                progress = false;
                for(usize idx = 0u; idx < unit.templateInstantiations.size(); ++idx){
                    if(instEmitted[idx] != 0u)
                        continue;
                    const ClassTemplateInstantiation* inst = unit.templateInstantiations[idx];
                    if(!inst || inst->isImported || !inst->templateDecl){
                        instEmitted[idx] = 1u;
                        continue;
                    }
                    if(!isClassReady(inst->templateDecl))
                        continue;
                    b8 depsReady = true;
                    for(usize depIdx = 0u; depIdx < sortedLocalClasses.size(); ++depIdx){
                        if(localClassReady[depIdx] != 0u || !sortedLocalClasses[depIdx])
                            continue;
                        if(declarationDependencies.instantiationDependsOnClassByValue(*inst, sortedLocalClasses[depIdx]->name)){
                            depsReady = false;
                            break;
                        }
                    }
                    if(!depsReady)
                        continue;
                    instEmitted[idx] = 1u;
                    emitClassTemplateSpecialization(out, *inst, namespacePath);
                    progress = true;
                }
            }
        };

        emitReadyInstantiations();
        for(usize idx = 0u; idx < sortedLocalClasses.size(); ++idx){
            const ClassDecl* classDecl = sortedLocalClasses[idx];
            if(!classDecl)
                continue;
            emitClassDecl(out, *classDecl);
            localClassReady[idx] = 1u;
            emitReadyInstantiations();
        }
        for(usize idx = 0u; idx < unit.templateInstantiations.size(); ++idx){
            if(instEmitted[idx] == 0u && unit.templateInstantiations[idx] && !unit.templateInstantiations[idx]->isImported)
                emitClassTemplateSpecialization(out, *unit.templateInstantiations[idx], namespacePath);
        }
        for(const ClassDecl* classDecl : sortedLocalClasses){
            if(!classDecl || !classDecl->templateParams.empty())
                continue;
            for(const MethodDecl* method : classDecl->methods){
                if(!method || method->isImported || method->isExtern || !method->hasBody || !method->hasSynthesizedArenaParams)
                    continue;
                m_sourceEmitter.emitOutOfClassCallable(out, classDecl, *method, true, true, false);
            }
        }
    }

    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl || !classDecl->isImported || classDecl->isExtern)
            continue;
        for(const MethodDecl* method : classDecl->methods){
            if(!method || method->isImported || method->isExtern || !ShouldEmitCallableInHeader(classDecl, *method))
                continue;
            m_sourceEmitter.emitOutOfClassCallable(out, classDecl, *method, true, true, false);
        }
    }

    emitGlobalConstBucket(Span<const Stmt* const>(postClassConsts.data(), postClassConsts.size()));

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported || fn->isExtern)
            continue;
        const b8 emitInlineBody = ShouldEmitCallableInHeader(nullptr, *fn);
        if(emitFreeFnPrototypesEarly && !emitInlineBody)
            continue;
        m_sourceEmitter.emitOutOfClassCallable(out, nullptr, *fn, emitInlineBody, true, !emitFreeFnPrototypesEarly);
    }

    for(const StaticAssertion& assertion : unit.staticAssertions){
        out.append("static_assert(");
        m_sourceEmitter.emitExpr(out, *assertion.condition);
        if(assertion.message){
            out.append(", ");
            m_sourceEmitter.emitExpr(out, *assertion.message);
        }
        out.append(");\r\n");
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

