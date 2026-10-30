// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"

#include "../sema/layout_optimizer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString HeaderEmitter::emitHeader(const TranslationUnit& unit, const MStringView namespacePath)const{
    MString out(m_arena);
    out.reserve(4096u);


    out.append(s_AdhdGeneratedFileBanner.data(), s_AdhdGeneratedFileBanner.size());
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append("#pragma once\r\n\r\n\r\n");
    out.append("#include <global/global.h>\r\n");
    out.append("#include <global/simdmath.h>\r\n");
    out.append("#include <global/inplace_function.h>\r\n");
    out.append("#include <global/not_null.h>\r\n");
    out.append("#include <core/alloc/global.h>\r\n");
    out.append("#include <core/alloc/general.h>\r\n");
    out.append("#include <core/alloc/scratch.h>\r\n");
    out.append("#include <core/assets/ref.h>\r\n");
    out.append("#include <core/ecs/entity_id.h>\r\n");
    if(namespacePath == "NWB::Impl::Scene")
        out.append("#include <impl/ecs_scene/global.h>\r\n");
    else if(namespacePath.size() >= 9u && namespacePath.substr(0u, 9u) == "NWB::Impl")
        out.append("#include <impl/global.h>\r\n");

    b8 hasSubsystemDecl = false;
    b8 hasComponentAssetDecl = false;
    b8 hasHeaderTaskScope = false;
    MVector<MStringView> subsystemAssetIncludes(m_arena);
    const auto addSubsystemInclude = [&](const MStringView includeDirective){
        for(const MStringView existing : subsystemAssetIncludes){
            if(existing == includeDirective)
                return;
        }
        subsystemAssetIncludes.push_back(includeDirective);
    };
    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl)
            continue;
        for(const MethodDecl* method : classDecl->methods){
            if(!method || method->isImported || !method->hasBody || !method->usesTaskScope)
                continue;
            if(!classDecl->templateParams.empty() || !method->templateParams.empty() || method->isInline){
                hasHeaderTaskScope = true;
                break;
            }
        }
        if(classDecl->isImported)
            continue;
        if(!classDecl->subsystemTypeName.empty()){
            hasSubsystemDecl = true;
            for(const FieldDecl* field : classDecl->fields){
                if(!field)
                    continue;
                MStringView assetName;
                b8 isVectorField = false;
                if(!tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField))
                    continue;
#define NWB_ADHD_EXPORT_ASSET_INCLUDE(assetTypeName, includeDirective) \
                if(assetName == assetTypeName) addSubsystemInclude(includeDirective);
#define NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE(assetTypeSuffix, includeDirective) \
                if(assetName.size() >= sizeof(assetTypeSuffix) - 1u && assetName.substr(assetName.size() - (sizeof(assetTypeSuffix) - 1u)) == assetTypeSuffix) \
                    addSubsystemInclude(includeDirective);
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE
#undef NWB_ADHD_EXPORT_ASSET_INCLUDE
            }
        }
        if(!classDecl->componentAssetTypeName.empty())
            hasComponentAssetDecl = true;
    }
    if(hasSubsystemDecl){
        for(const MStringView inc : subsystemAssetIncludes){
            out.append(inc.data(), inc.size());
            out.append("\r\n");
        }
        out.append("#include <impl/assets_subsystem/asset.h>\r\n");
    }
    if(hasComponentAssetDecl)
        out.append("#include <impl/assets_scene/components.h>\r\n");
    if(!hasHeaderTaskScope){
        for(const MethodDecl* fn : unit.freeFunctions){
            if(fn && !fn->isImported && fn->hasBody && fn->usesTaskScope && (fn->isInline || !fn->templateParams.empty())){
                hasHeaderTaskScope = true;
                break;
            }
        }
    }
    if(hasHeaderTaskScope)
        out.append("#include <core/task/cpu/scheduler.h>\r\n");

    for(const MString& inc : unit.includedAdhdFiles){
        const MStringView incView = StripAdhdExtension(MStringView(inc.data(), inc.size()));
        out.append("#include \"");
        out.append(incView.data(), incView.size());
        out.append(".adhd.h\"\r\n");
    }

    if(!unit.topLevelNativeBlocks.empty()){
        out.append("\r\n");
        for(const MStringView& rawBlock : unit.topLevelNativeBlocks){
            AppendAdhdTextCrlf(out, rawBlock);
            out.append("\r\n");
        }
    }

    out.append("\r\n\r\n////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

    for(const EnumDecl* enumDecl : unit.enums){
        if(enumDecl && !enumDecl->isImported)
            emitEnumDecl(out, *enumDecl);
    }

    MVector<MStringView> externalForwardClasses(m_arena);
    const auto collectCallableForwardDeclClasses = [&](const MethodDecl& callable, const Span<const TemplateParamDecl> outerTemplateParams, const Span<const TemplateParamDecl> innerTemplateParams){
        if(callable.returnType)
            CollectExternalForwardDeclClasses(*callable.returnType, m_symbols, outerTemplateParams, innerTemplateParams, externalForwardClasses);
        for(const ParamDecl& p : callable.params){
            if(p.type)
                CollectExternalForwardDeclClasses(*p.type, m_symbols, outerTemplateParams, innerTemplateParams, true, 0u, externalForwardClasses);
        }
    };
    for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(aliasDecl && !aliasDecl->isImported && aliasDecl->aliasedType)
            CollectExternalForwardDeclClasses(*aliasDecl->aliasedType, m_symbols, aliasDecl->templateParams, {}, externalForwardClasses);
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
                CollectExternalForwardDeclClasses(*f->type, m_symbols, classDecl->templateParams, {}, externalForwardClasses);
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
            if(aliasDecl && !aliasDecl->isImported && TypeRefReferencesSymbol(*c.varType, aliasDecl->name))
                return true;
        }
        return false;
    };

    const auto constUsesLocalClass = [&](const Stmt& c)noexcept -> b8{
        if(!c.varType)
            return false;
        for(const ClassDecl* classDecl : unit.classes){
            if(classDecl && !classDecl->isImported && TypeRefReferencesSymbolByValue(*c.varType, classDecl->name, m_symbols))
                return true;
        }
        return false;
    };

    b8 emittedEarlyConst = false;
    for(const Stmt* c : unit.globalConstants){
        if(!c || c->isImported || !c->varType || constUsesLocalTypeAlias(*c) || constUsesLocalClass(*c))
            continue;
        emittedEarlyConst = true;
        emitGlobalConstDecl(out, *c);
    }
    if(emittedEarlyConst)
        out.append("\r\n\r\n");

    MVector<const TypeAliasDecl*> localAliases(m_arena);
    for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(aliasDecl && !aliasDecl->isImported)
            localAliases.push_back(aliasDecl);
    }
    if(!localAliases.empty()){
        MVector<const TypeAliasDecl*> sortedAliases(m_arena);
        SortTypeAliasesTopologically(localAliases, m_arena, sortedAliases);
        for(const TypeAliasDecl* aliasDecl : sortedAliases){
            if(aliasDecl)
                emitTypeAliasDecl(out, *aliasDecl);
        }
    }

    b8 emittedMidConst = false;
    for(const Stmt* c : unit.globalConstants){
        if(!c || c->isImported || !c->varType || !constUsesLocalTypeAlias(*c) || constUsesLocalClass(*c))
            continue;
        emittedMidConst = true;
        emitGlobalConstDecl(out, *c);
    }
    if(emittedMidConst)
        out.append("\r\n\r\n");

    MVector<const InterfaceDecl*> localInterfaces(m_arena);
    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(ifaceDecl && !ifaceDecl->isImported)
            localInterfaces.push_back(ifaceDecl);
    }
    if(!localInterfaces.empty()){
        MVector<const InterfaceDecl*> sortedInterfaces(m_arena);
        SortInterfacesTopologically(localInterfaces, m_symbols, m_arena, sortedInterfaces);
        for(const InterfaceDecl* ifaceDecl : sortedInterfaces){
            if(ifaceDecl)
                emitInterfaceDecl(out, *ifaceDecl);
        }
    }

    MVector<const ClassDecl*> allClasses(m_arena);
    for(const ClassDecl* classDecl : unit.classes){
        if(classDecl)
            allClasses.push_back(classDecl);
    }
    if(!allClasses.empty()){
        MVector<const ClassDecl*> sortedClasses(m_arena);
        SortClassesTopologically(allClasses, m_symbols, m_arena, sortedClasses);

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
                        if(InstantiationDependsOnClassByValue(*inst, depCls->name, m_symbols)){
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

    b8 emittedClassConst = false;
    for(const Stmt* c : unit.globalConstants){
        if(!c || c->isImported || !c->varType || !constUsesLocalClass(*c))
            continue;
        emittedClassConst = true;
        emitGlobalConstDecl(out, *c);
    }
    if(emittedClassConst)
        out.append("\r\n\r\n");

    for(const MethodDecl* fn : unit.freeFunctions){
        if(fn && !fn->isImported)
            emitFreeFunctionDecl(out, *fn);
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}

b8 HeaderEmitter::tryResolveSubsystemFieldAssetType(const FieldDecl& field, MStringView& outAssetTypeName, b8& outIsVector)const noexcept{
    outAssetTypeName = {};
    outIsVector = false;
    if(!field.type)
        return false;

    const TypeRef* resolved = m_symbols.unwrapTypeAlias(field.type);
    if(!resolved)
        return false;

    if(resolved->name == "Vector" && !resolved->templateArgs.empty() && resolved->templateArgs[0].type){
        outIsVector = true;
        const TypeRef* elemType = m_symbols.unwrapTypeAlias(resolved->templateArgs[0].type);
        if(elemType && !elemType->templateArgs.empty() && elemType->templateArgs[0].type)
            outAssetTypeName = elemType->templateArgs[0].type->name;
        return true;
    }
    if(resolved->name == "AssetRef" && !resolved->templateArgs.empty() && resolved->templateArgs[0].type){
        outAssetTypeName = resolved->templateArgs[0].type->name;
        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

