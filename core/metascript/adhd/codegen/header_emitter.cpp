// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"
#include "header_preamble_emitter.h"

#include "../sema/declaration_dependency_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString HeaderEmitter::emitHeader(const TranslationUnit& unit, const MStringView namespacePath)const{
    const DeclarationDependencyAnalyzer declarationDependencies(m_arena, m_symbols);
    MString out(m_arena);
    out.reserve(s_AdhdEmitReserveBytes);


    HeaderPreambleEmitter::Emit(out, unit, namespacePath);

    for(const EnumDecl* enumDecl : unit.enums){
        if(enumDecl && !enumDecl->isImported)
            emitEnumDecl(out, *enumDecl);
    }

    const b8 forwardDeclRefTypes = unit.topLevelNativeBlocks.empty() && !unit.hasImportedNativeBlocks;
    MVector<MStringView> externalForwardClasses(m_arena);
    const auto collectTypeForwardDecls = [&](const TypeRef& t, const Span<const TemplateParamDecl> outerTemplateParams, const Span<const TemplateParamDecl> innerTemplateParams, const b8 includeDirectRef){
        if(forwardDeclRefTypes){
            declarationDependencies.collectExternalForwardDeclClasses(t, outerTemplateParams, innerTemplateParams, includeDirectRef, 0u, externalForwardClasses);
        }
        else{
            TypeRef stripped = t;
            stripped.refMode = ParamPassingMode::None;
            declarationDependencies.collectExternalForwardDeclClasses(stripped, outerTemplateParams, innerTemplateParams, false, 0u, externalForwardClasses);
        }
    };
    const auto collectCallableForwardDeclClasses = [&](const MethodDecl& callable, const Span<const TemplateParamDecl> outerTemplateParams, const Span<const TemplateParamDecl> innerTemplateParams){
        if(callable.returnType)
            collectTypeForwardDecls(*callable.returnType, outerTemplateParams, innerTemplateParams, false);
        for(const ParamDecl& p : callable.params){
            if(p.type)
                collectTypeForwardDecls(*p.type, outerTemplateParams, innerTemplateParams, forwardDeclRefTypes);
        }
    };
    for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(aliasDecl && !aliasDecl->isImported && aliasDecl->aliasedType)
            collectTypeForwardDecls(*aliasDecl->aliasedType, aliasDecl->templateParams, {}, false);
    }
    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(!ifaceDecl || ifaceDecl->isImported)
            continue;
        for(const MethodDecl* m : ifaceDecl->methods){
            if(m)
                collectCallableForwardDeclClasses(*m, ifaceDecl->templateParams, {});
        }
    }
    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl || classDecl->isImported)
            continue;
        for(const FieldDecl* f : classDecl->fields){
            if(f && f->type)
                collectTypeForwardDecls(*f->type, classDecl->templateParams, {}, false);
        }
        for(const MethodDecl* m : classDecl->methods){
            if(m)
                collectCallableForwardDeclClasses(*m, classDecl->templateParams, m->templateParams);
        }
    }
    for(const MethodDecl* fn : unit.freeFunctions){
        if(fn && !fn->isImported)
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
        if(!ifaceDecl || ifaceDecl->isImported)
            continue;
        emittedForwardDecl = true;
        m_typeEmitter.emitTemplateHeaderCpp(out, ifaceDecl->templateParams);
        out.append("class ");
        out.append(ifaceDecl->name.data(), ifaceDecl->name.size());
        out.append(";\r\n");
    }
    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl || classDecl->isImported)
            continue;
        emittedForwardDecl = true;
        m_typeEmitter.emitTemplateHeaderCpp(out, classDecl->templateParams);
        out.append("class ");
        out.append(classDecl->name.data(), classDecl->name.size());
        out.append(";\r\n");
    }
    if(emittedForwardDecl)
        out.append("\r\n");

    const auto constUsesLocalTypeAlias = [&](const Stmt& c)noexcept -> b8{
        if(!c.varType)
            return false;
        for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
            if(aliasDecl && !aliasDecl->isImported && declarationDependencies.typeRefReferencesSymbol(*c.varType, aliasDecl->name))
                return true;
        }
        return false;
    };

    const auto constUsesLocalClass = [&](const Stmt& c)noexcept -> b8{
        if(!c.varType)
            return false;
        for(const ClassDecl* classDecl : unit.classes){
            if(classDecl && !classDecl->isImported && declarationDependencies.typeRefReferencesSymbolByValue(*c.varType, classDecl->name))
                return true;
        }
        return false;
    };

    const auto emitMatchingGlobalConsts = [&](const auto& pred){
        b8 emittedAny = false;
        for(const Stmt* c : unit.globalConstants){
            if(!c || c->isImported || !c->varType || !pred(*c))
                continue;
            emittedAny = true;
            emitGlobalConstDecl(out, *c);
        }
        if(emittedAny)
            out.append("\r\n\r\n");
    };

    emitMatchingGlobalConsts([&](const Stmt& c)noexcept{
        return !constUsesLocalTypeAlias(c) && !constUsesLocalClass(c);
    });

    MVector<const TypeAliasDecl*> localAliases(m_arena);
    for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(aliasDecl && !aliasDecl->isImported)
            localAliases.push_back(aliasDecl);
    }
    if(!localAliases.empty()){
        MVector<const TypeAliasDecl*> sortedAliases(m_arena);
        declarationDependencies.sortTypeAliasesTopologically(localAliases, sortedAliases);
        for(const TypeAliasDecl* aliasDecl : sortedAliases){
            if(aliasDecl)
                emitTypeAliasDecl(out, *aliasDecl);
        }
    }

    emitMatchingGlobalConsts([&](const Stmt& c)noexcept{
        return constUsesLocalTypeAlias(c) && !constUsesLocalClass(c);
    });

    MVector<const InterfaceDecl*> localInterfaces(m_arena);
    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(ifaceDecl && !ifaceDecl->isImported)
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
            if(fn && !fn->isImported)
                m_sourceEmitter.emitOutOfClassCallable(out, nullptr, *fn, false, false);
        }
    }

    MVector<const ClassDecl*> allClasses(m_arena);
    for(const ClassDecl* classDecl : unit.classes){
        if(classDecl)
            allClasses.push_back(classDecl);
    }
    if(!allClasses.empty()){
        MVector<const ClassDecl*> sortedClasses(m_arena);
        declarationDependencies.sortClassesTopologically(allClasses, sortedClasses);

        MVector<u8> instEmitted(unit.templateInstantiations.size(), 0u, m_arena);
        MVector<const ClassDecl*> readyClasses(m_arena);
        readyClasses.reserve(sortedClasses.size());
        for(const ClassDecl* classDecl : sortedClasses){
            if(classDecl && classDecl->isImported)
                readyClasses.push_back(classDecl);
        }

        const auto isClassReady = [&](const ClassDecl* target)noexcept -> b8{
            for(const ClassDecl* c : readyClasses){
                if(c == target)
                    return true;
            }
            return false;
        };

        const auto emitReadyInstantiations = [&](){
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
                    for(const ClassDecl* depCls : sortedClasses){
                        if(!depCls || isClassReady(depCls))
                            continue;
                        if(declarationDependencies.instantiationDependsOnClassByValue(*inst, depCls->name)){
                            depsReady = false;
                            break;
                        }
                    }
                    if(!depsReady)
                        continue;
                    instEmitted[idx] = 1u;
                    emitClassTemplateSpecialization(out, *inst);
                    progress = true;
                }
            }
        };

        emitReadyInstantiations();
        for(const ClassDecl* classDecl : sortedClasses){
            if(!classDecl || classDecl->isImported)
                continue;
            emitClassDecl(out, *classDecl);
            readyClasses.push_back(classDecl);
            emitReadyInstantiations();
        }
        for(usize idx = 0u; idx < unit.templateInstantiations.size(); ++idx){
            if(instEmitted[idx] == 0u && unit.templateInstantiations[idx] && !unit.templateInstantiations[idx]->isImported)
                emitClassTemplateSpecialization(out, *unit.templateInstantiations[idx]);
        }
        for(const ClassDecl* classDecl : sortedClasses){
            if(!classDecl || !classDecl->isImported)
                continue;
            for(const MethodDecl* method : classDecl->methods){
                if(!method || method->isImported || !method->hasBody)
                    continue;
                if(classDecl->templateParams.empty() && method->templateParams.empty() && !method->isInline)
                    continue;
                m_sourceEmitter.emitOutOfClassCallable(out, classDecl, *method, true, true);
            }
        }
    }

    emitMatchingGlobalConsts([&](const Stmt& c)noexcept{
        return constUsesLocalClass(c);
    });

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported)
            continue;
        const b8 emitInlineBody = (fn->isInline || !fn->templateParams.empty()) && fn->hasBody;
        if(emitFreeFnPrototypesEarly && !emitInlineBody)
            continue;
        m_sourceEmitter.emitOutOfClassCallable(out, nullptr, *fn, emitInlineBody, true);
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

