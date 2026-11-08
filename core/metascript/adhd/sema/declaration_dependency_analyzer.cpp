// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "declaration_dependency_analyzer.h"

#include "concrete_type_analyzer.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_declaration_dependencies{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename DeclT, typename DependsOnPred>
void SortDeclsTopologically(const Span<const DeclT* const> items, const DependsOnPred& dependsOn, MetaArena& arena, MVector<const DeclT*>& outSorted){
    if(items.empty()){
        outSorted.clear();
        return;
    }
    MVector<u8> state(items.size(), 0u, arena);
    TopologicallySortPointers(items, dependsOn, state, outSorted);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclarationDependencyAnalyzer::classDependsOnClassByValue(const ClassDecl& cls, const MStringView targetClassName, const u32 depth)const noexcept{
    for(const FieldDecl* field : cls.fields){
        if(field && field->type && typeRefReferencesSymbolByValue(*field->type, targetClassName, depth))
            return true;
    }
    return false;
}


b8 DeclarationDependencyAnalyzer::typeRefReferencesSymbol(const TypeRef& type, const MStringView targetName, const u32 depth)const noexcept{
    if(depth > 16u)
        return false;
    if(type.name == targetName)
        return true;
    if(type.isFuncType){
        if(type.funcReturnType && typeRefReferencesSymbol(*type.funcReturnType, targetName, depth + 1u))
            return true;
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type && typeRefReferencesSymbol(*fp.type, targetName, depth + 1u))
                return true;
        }
    }
    for(const TypeArg& targ : type.templateArgs){
        if(targ.type && typeRefReferencesSymbol(*targ.type, targetName, depth + 1u))
            return true;
    }
    return false;
}


b8 DeclarationDependencyAnalyzer::typeRefReferencesSymbolByValue(const TypeRef& type, const MStringView targetName, const u32 depth)const noexcept{
    if(depth > 16u || type.isFuncType || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return false;
    if(type.name == targetName)
        return true;
    if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
        if(unwrapped != &type && typeRefReferencesSymbolByValue(*unwrapped, targetName, depth + 1u))
            return true;
    }
    if(IsAssetHandleType(m_symbols.resolveBaseTypeName(type.name)))
        return false;
    if(const ClassDecl* cls = m_symbols.findClass(type.name)){
        if(!cls->templateParams.empty() && classDependsOnClassByValue(*cls, targetName, depth + 1u))
            return true;
    }
    for(const TypeArg& targ : type.templateArgs){
        if(targ.type && typeRefReferencesSymbolByValue(*targ.type, targetName, depth + 1u))
            return true;
    }
    return false;
}


b8 DeclarationDependencyAnalyzer::instantiationDependsOnClassByValue(const ClassTemplateInstantiation& inst, const MStringView targetClassName)const noexcept{
    for(const FieldDecl* field : inst.orderedFields){
        if(field && field->type && typeRefReferencesSymbolByValue(*field->type, targetClassName))
            return true;
    }
    for(const TypeArg& targ : inst.templateArgs){
        if(targ.type && typeRefReferencesSymbolByValue(*targ.type, targetClassName))
            return true;
    }
    return false;
}


void DeclarationDependencyAnalyzer::sortTypeAliasesTopologically(const Span<const TypeAliasDecl* const> aliases, MVector<const TypeAliasDecl*>& outSorted)const{
    __hidden_declaration_dependencies::SortDeclsTopologically(aliases, [this](const TypeAliasDecl& cur, const TypeAliasDecl& dep)noexcept{
        return cur.aliasedType && typeRefReferencesSymbol(*cur.aliasedType, dep.name);
    }, m_arena, outSorted);
}


void DeclarationDependencyAnalyzer::sortInterfacesTopologically(const Span<const InterfaceDecl* const> interfaces, MVector<const InterfaceDecl*>& outSorted)const{
    __hidden_declaration_dependencies::SortDeclsTopologically(interfaces, [&](const InterfaceDecl& cur, const InterfaceDecl& dep)noexcept{
        for(const TypeRef* baseRef : cur.baseInterfaces){
            if(baseRef && m_symbols.resolveBaseTypeName(baseRef->name) == dep.name)
                return true;
        }
        return false;
    }, m_arena, outSorted);
}


void DeclarationDependencyAnalyzer::sortClassesTopologically(const Span<const ClassDecl* const> classes, MVector<const ClassDecl*>& outSorted)const{
    __hidden_declaration_dependencies::SortDeclsTopologically(classes, [&](const ClassDecl& cur, const ClassDecl& dep)noexcept{
        return classDependsOnClassByValue(cur, dep.name);
    }, m_arena, outSorted);
}


void DeclarationDependencyAnalyzer::collectExternalForwardDeclClasses(
    const TypeRef& type,
    const Span<const TemplateParamDecl> outerTemplateParams,
    const Span<const TemplateParamDecl> innerTemplateParams,
    const b8 inRefOrAssetContext,
    const u32 depth,
    MVector<MStringView>& outNames
)const{
    if(depth > 16u || type.isAuto)
        return;

    if(type.isFuncType){
        if(type.funcReturnType)
            collectExternalForwardDeclClasses(*type.funcReturnType, outerTemplateParams, innerTemplateParams, type.funcReturnType->refMode != ParamPassingMode::None, depth + 1u, outNames);
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type)
                collectExternalForwardDeclClasses(*fp.type, outerTemplateParams, innerTemplateParams, true, depth + 1u, outNames);
        }
        return;
    }

    const b8 isRefOrAsset = inRefOrAssetContext || (type.refMode != ParamPassingMode::None);
    if(
        isRefOrAsset
        && !type.name.empty()
        && type.templateArgs.empty()
        && !FindTemplateParamByName(outerTemplateParams, type.name)
        && !FindTemplateParamByName(innerTemplateParams, type.name)
        && !ConcreteTypeAnalyzer(m_symbols).isKnownConcreteTypeName(type.name)
    ){
        b8 alreadyPresent = false;
        for(const MStringView existing : outNames){
            if(existing == type.name){
                alreadyPresent = true;
                break;
            }
        }
        if(!alreadyPresent)
            outNames.push_back(type.name);
    }

    const b8 childInAssetContext = IsAssetHandleType(m_symbols.resolveBaseTypeName(type.name));
    for(const TypeArg& targ : type.templateArgs){
        if(targ.type)
            collectExternalForwardDeclClasses(*targ.type, outerTemplateParams, innerTemplateParams, childInAssetContext || (targ.type->refMode != ParamPassingMode::None), depth + 1u, outNames);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

