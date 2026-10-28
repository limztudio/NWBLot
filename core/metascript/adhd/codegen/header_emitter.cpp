// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_header_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool TypeRefReferencesSymbol(const TypeRef& type, const MStringView targetName, const u32 depth = 0u)noexcept{
    if(depth > 16u)
        return false;
    if(type.name == targetName)
        return true;
    if(type.isFuncType){
        if(type.funcReturnType && TypeRefReferencesSymbol(*type.funcReturnType, targetName, depth + 1u))
            return true;
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type && TypeRefReferencesSymbol(*fp.type, targetName, depth + 1u))
                return true;
        }
    }
    for(const TypeArg& targ : type.templateArgs){
        if(targ.type && TypeRefReferencesSymbol(*targ.type, targetName, depth + 1u))
            return true;
    }
    return false;
}

[[nodiscard]] bool ClassDependsOnClassByValue(
    const ClassDecl& cls,
    const MStringView targetClassName,
    const SymbolTable& symbols,
    u32 depth = 0u
)noexcept;

[[nodiscard]] bool TypeRefReferencesSymbolByValue(
    const TypeRef& type,
    const MStringView targetName,
    const SymbolTable& symbols,
    const u32 depth = 0u
)noexcept{
    if(depth > 16u || type.isFuncType || symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return false;
    if(type.name == targetName)
        return true;
    if(const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type)){
        if(unwrapped != &type && TypeRefReferencesSymbolByValue(*unwrapped, targetName, symbols, depth + 1u))
            return true;
    }
    if(const ClassDecl* cls = symbols.findClass(type.name)){
        if(!cls->templateParams.empty() && ClassDependsOnClassByValue(*cls, targetName, symbols, depth + 1u))
            return true;
    }
    for(const TypeArg& targ : type.templateArgs){
        if(targ.type && TypeRefReferencesSymbolByValue(*targ.type, targetName, symbols, depth + 1u))
            return true;
    }
    return false;
}

[[nodiscard]] bool ClassDependsOnClassByValue(
    const ClassDecl& cls,
    const MStringView targetClassName,
    const SymbolTable& symbols,
    const u32 depth
)noexcept{
    for(const FieldDecl* field : cls.fields){
        if(!field || !field->type)
            continue;
        if(TypeRefReferencesSymbolByValue(*field->type, targetClassName, symbols, depth))
            return true;
    }
    return false;
}

[[nodiscard]] bool InstantiationDependsOnClassByValue(
    const ClassTemplateInstantiation& inst,
    const MStringView targetClassName,
    const SymbolTable& symbols
)noexcept{
    for(const FieldDecl* field : inst.orderedFields){
        if(field && field->type && TypeRefReferencesSymbolByValue(*field->type, targetClassName, symbols))
            return true;
    }
    for(const TypeArg& targ : inst.templateArgs){
        if(targ.type && TypeRefReferencesSymbolByValue(*targ.type, targetClassName, symbols))
            return true;
    }
    return false;
}

void VisitTypeAliasTopo(
    const usize idx,
    const MVector<const TypeAliasDecl*>& aliases,
    MVector<u8>& state,
    MVector<const TypeAliasDecl*>& outSorted
){
    if(state[idx] != 0u)
        return;
    state[idx] = 1u;
    const TypeAliasDecl* cur = aliases[idx];
    if(cur && cur->aliasedType){
        for(usize j = 0u; j < aliases.size(); ++j){
            if(j != idx && state[j] == 0u && aliases[j] && TypeRefReferencesSymbol(*cur->aliasedType, aliases[j]->name))
                VisitTypeAliasTopo(j, aliases, state, outSorted);
        }
    }
    state[idx] = 2u;
    outSorted.push_back(cur);
}

void VisitInterfaceTopo(
    const usize idx,
    const MVector<const InterfaceDecl*>& interfaces,
    const SymbolTable& symbols,
    MVector<u8>& state,
    MVector<const InterfaceDecl*>& outSorted
){
    if(state[idx] != 0u)
        return;
    state[idx] = 1u;
    const InterfaceDecl* cur = interfaces[idx];
    if(cur){
        for(const TypeRef* baseRef : cur->baseInterfaces){
            if(!baseRef)
                continue;
            const MStringView baseName = symbols.resolveBaseTypeName(baseRef->name);
            for(usize j = 0u; j < interfaces.size(); ++j){
                if(j != idx && state[j] == 0u && interfaces[j] && interfaces[j]->name == baseName)
                    VisitInterfaceTopo(j, interfaces, symbols, state, outSorted);
            }
        }
    }
    state[idx] = 2u;
    outSorted.push_back(cur);
}

void VisitClassTopo(
    const usize idx,
    const MVector<const ClassDecl*>& classes,
    const SymbolTable& symbols,
    MVector<u8>& state,
    MVector<const ClassDecl*>& outSorted
){
    if(state[idx] != 0u)
        return;
    state[idx] = 1u;
    const ClassDecl* cur = classes[idx];
    if(cur){
        for(usize j = 0u; j < classes.size(); ++j){
            if(j != idx && state[j] == 0u && classes[j] && ClassDependsOnClassByValue(*cur, classes[j]->name, symbols))
                VisitClassTopo(j, classes, symbols, state, outSorted);
        }
    }
    state[idx] = 2u;
    outSorted.push_back(cur);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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

    bool emittedForwardDecl = false;
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

    auto constUsesLocalTypeAlias = [&](const Stmt& c)noexcept -> bool{
        if(!c.varType)
            return false;
        for(const TypeAliasDecl* aliasDecl : unit.typeAliases){
            if(aliasDecl && !aliasDecl->isImported && __hidden_adhd_header_emitter::TypeRefReferencesSymbol(*c.varType, aliasDecl->name))
                return true;
        }
        return false;
    };

    auto constUsesLocalClass = [&](const Stmt& c)noexcept -> bool{
        if(!c.varType)
            return false;
        for(const ClassDecl* classDecl : unit.classes){
            if(classDecl && !classDecl->isImported && __hidden_adhd_header_emitter::TypeRefReferencesSymbolByValue(*c.varType, classDecl->name, m_symbols))
                return true;
        }
        return false;
    };

    bool emittedEarlyConst = false;
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
        MVector<u8> aliasState(localAliases.size(), 0u, m_arena);
        MVector<const TypeAliasDecl*> sortedAliases(m_arena);
        sortedAliases.reserve(localAliases.size());
        for(usize i = 0u; i < localAliases.size(); ++i)
            __hidden_adhd_header_emitter::VisitTypeAliasTopo(i, localAliases, aliasState, sortedAliases);
        for(const TypeAliasDecl* aliasDecl : sortedAliases){
            if(aliasDecl)
                emitTypeAliasDecl(out, *aliasDecl);
        }
    }

    bool emittedMidConst = false;
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
        MVector<u8> ifaceState(localInterfaces.size(), 0u, m_arena);
        MVector<const InterfaceDecl*> sortedInterfaces(m_arena);
        sortedInterfaces.reserve(localInterfaces.size());
        for(usize i = 0u; i < localInterfaces.size(); ++i)
            __hidden_adhd_header_emitter::VisitInterfaceTopo(i, localInterfaces, m_symbols, ifaceState, sortedInterfaces);
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
        MVector<u8> classState(allClasses.size(), 0u, m_arena);
        MVector<const ClassDecl*> sortedClasses(m_arena);
        sortedClasses.reserve(allClasses.size());
        for(usize i = 0u; i < allClasses.size(); ++i)
            __hidden_adhd_header_emitter::VisitClassTopo(i, allClasses, m_symbols, classState, sortedClasses);

        MVector<u8> instEmitted(unit.templateInstantiations.size(), 0u, m_arena);
        MVector<const ClassDecl*> readyClasses(m_arena);
        readyClasses.reserve(sortedClasses.size());
        for(const ClassDecl* classDecl : sortedClasses){
            if(classDecl && classDecl->isImported)
                readyClasses.push_back(classDecl);
        }

        auto isClassReady = [&](const ClassDecl* target)noexcept -> bool{
            for(const ClassDecl* c : readyClasses){
                if(c == target)
                    return true;
            }
            return false;
        };

        auto emitReadyInstantiations = [&](){
            bool progress = true;
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
                    bool depsReady = true;
                    for(const ClassDecl* depCls : sortedClasses){
                        if(!depCls || isClassReady(depCls))
                            continue;
                        if(__hidden_adhd_header_emitter::InstantiationDependsOnClassByValue(*inst, depCls->name, m_symbols)){
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
    }

    bool emittedClassConst = false;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

