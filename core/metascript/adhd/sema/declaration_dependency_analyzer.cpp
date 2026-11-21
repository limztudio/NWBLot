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


template<typename DeclT, typename VisitDepsFn>
void SortDeclsTopologicallyIndexed(
    const Span<const DeclT* const> items,
    MetaArena& arena,
    MVector<const DeclT*>& outSorted,
    HashMap<MStringView, usize, MetaArena, MStringViewHash>& outIndexByName,
    const VisitDepsFn& visitDeps
){
    outSorted.clear();
    if(items.empty())
        return;

    outIndexByName.reserve(items.size());
    for(usize i = 0u; i < items.size(); ++i){
        if(items[i] && !items[i]->name.empty())
            outIndexByName.emplace(items[i]->name, i);
    }

    MVector<u8> state(items.size(), 0u, arena);
    outSorted.reserve(items.size());

    const auto visit = [&](const auto& self, const usize idx) -> void{
        if(idx >= items.size() || state[idx] != 0u)
            return;
        state[idx] = 1u;
        if(items[idx])
            visitDeps(*items[idx], [&](const usize depIdx){ self(self, depIdx); });
        state[idx] = 2u;
        outSorted.push_back(items[idx]);
    };

    for(usize i = 0u; i < items.size(); ++i)
        visit(visit, i);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclarationDependencyAnalyzer::classDependsOnClassByValue(const ClassDecl& cls, const MStringView targetClassName, const u32 depth)const noexcept{
    if(cls.concreteBase && cls.concreteBase->name == targetClassName)
        return true;
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
    HashMap<MStringView, usize, MetaArena, MStringViewHash> indexByName(m_arena);
    const auto visitTypeSymbols = [&](const auto& self, const TypeRef& type, const u32 depth, const auto& visitIdx) -> void{
        if(depth > 16u)
            return;
        if(const auto it = indexByName.find(type.name); it != indexByName.end())
            visitIdx(it->second);
        if(type.isFuncType){
            if(type.funcReturnType)
                self(self, *type.funcReturnType, depth + 1u, visitIdx);
            for(const FuncParamType& fp : type.funcParams){
                if(fp.type)
                    self(self, *fp.type, depth + 1u, visitIdx);
            }
        }
        for(const TypeArg& targ : type.templateArgs){
            if(targ.type)
                self(self, *targ.type, depth + 1u, visitIdx);
        }
    };
    __hidden_declaration_dependencies::SortDeclsTopologicallyIndexed(aliases, m_arena, outSorted, indexByName, [&](const TypeAliasDecl& cur, const auto& visitIdx){
        if(cur.aliasedType)
            visitTypeSymbols(visitTypeSymbols, *cur.aliasedType, 0u, visitIdx);
    });
}


void DeclarationDependencyAnalyzer::sortInterfacesTopologically(const Span<const InterfaceDecl* const> interfaces, MVector<const InterfaceDecl*>& outSorted)const{
    HashMap<MStringView, usize, MetaArena, MStringViewHash> indexByName(m_arena);
    __hidden_declaration_dependencies::SortDeclsTopologicallyIndexed(interfaces, m_arena, outSorted, indexByName, [&](const InterfaceDecl& cur, const auto& visitIdx){
        for(const TypeRef* baseRef : cur.baseInterfaces){
            if(!baseRef)
                continue;
            const MStringView baseName = m_symbols.resolveBaseTypeName(baseRef->name);
            if(const auto it = indexByName.find(baseName); it != indexByName.end())
                visitIdx(it->second);
        }
    });
}


void DeclarationDependencyAnalyzer::sortClassesTopologically(const Span<const ClassDecl* const> classes, MVector<const ClassDecl*>& outSorted)const{
    HashMap<MStringView, usize, MetaArena, MStringViewHash> indexByName(m_arena);
    const auto visitValueTypeSymbols = [&](const auto& self, const TypeRef& type, const u32 depth, const auto& visitIdx) -> void{
        if(depth > 16u || type.isFuncType || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
            return;
        if(const auto it = indexByName.find(type.name); it != indexByName.end())
            visitIdx(it->second);
        if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
            if(unwrapped != &type)
                self(self, *unwrapped, depth + 1u, visitIdx);
        }
        if(IsAssetHandleType(m_symbols.resolveBaseTypeName(type.name)))
            return;
        if(const ClassDecl* cls = m_symbols.findClass(type.name); cls && !cls->templateParams.empty()){
            if(cls->concreteBase){
                if(const auto it = indexByName.find(cls->concreteBase->name); it != indexByName.end())
                    visitIdx(it->second);
            }
            for(const FieldDecl* field : cls->fields){
                if(field && field->type)
                    self(self, *field->type, depth + 1u, visitIdx);
            }
        }
        for(const TypeArg& targ : type.templateArgs){
            if(targ.type)
                self(self, *targ.type, depth + 1u, visitIdx);
        }
    };
    __hidden_declaration_dependencies::SortDeclsTopologicallyIndexed(classes, m_arena, outSorted, indexByName, [&](const ClassDecl& cur, const auto& visitIdx){
        if(cur.concreteBase){
            if(const auto it = indexByName.find(cur.concreteBase->name); it != indexByName.end())
                visitIdx(it->second);
        }
        for(const FieldDecl* field : cur.fields){
            if(field && field->type)
                visitValueTypeSymbols(visitValueTypeSymbols, *field->type, 0u, visitIdx);
        }
    });
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

