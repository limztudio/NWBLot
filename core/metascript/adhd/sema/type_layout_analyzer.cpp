// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/operator_semantics.h>

#include "type_layout_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeLayoutAnalyzer::containsArenaBackedContainer(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || type.refMode != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None)
        return false;
    if(!m_symbols.hasTypeSymbol(unwrapped->name) && SymbolTable::IsArenaBackedContainer(unwrapped->name))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && containsArenaBackedContainer(*targ.type, depth + 1u))
            return true;
    }
    return false;
}

b8 TypeLayoutAnalyzer::isNonRootMotherArenaType(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return false;
    const ClassDecl* cls = m_symbols.findClass(unwrapped->name);
    if(!cls || cls->isArenaOwner || cls->isSerialize || !cls->subsystemTypeName.empty() || !cls->componentAssetTypeName.empty())
        return false;
    if(cls->needsClassArena)
        return true;
    if(!cls->templateParams.empty() && !unwrapped->templateArgs.empty()){
        for(const FieldDecl* f : cls->fields){
            if(!f || !f->type)
                continue;
            const TypeRef* subType = m_symbols.substituteTypeParams(f->type, cls->templateParams, unwrapped);
            if(subType && requiresMotherArena(*subType, depth + 1u))
                return true;
        }
    }
    return false;
}

b8 TypeLayoutAnalyzer::requiresMotherArena(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return false;
    if(!m_symbols.hasTypeSymbol(unwrapped->name) && SymbolTable::IsArenaBackedContainer(unwrapped->name))
        return true;
    if(isNonRootMotherArenaType(*unwrapped, depth))
        return true;
    if(!m_symbols.hasTypeSymbol(unwrapped->name)){
        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(unwrapped->name); genericInfo && genericInfo->byteSize == 0u){
            for(const TypeArg& targ : unwrapped->templateArgs){
                if(targ.type && requiresMotherArena(*targ.type, depth + 1u))
                    return true;
            }
        }
    }
    return false;
}

b8 TypeLayoutAnalyzer::isFixedArrayNeedingArenaDefaultInit(const TypeRef& type)const noexcept{
    if(m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || m_symbols.hasTypeSymbol(unwrapped->name) || !IsFixedArrayContainerType(unwrapped->name))
        return false;
    if(unwrapped->templateArgs.empty() || !unwrapped->templateArgs[0].type)
        return false;
    return requiresMotherArena(*unwrapped->templateArgs[0].type);
}

b8 TypeLayoutAnalyzer::interfaceInheritsNoCopy(const TypeRef& ifaceRef, const u32 depth)const noexcept{
    if(depth >= 8u)
        return false;
    if(IsNonCopyableBaseInterfaceName(ifaceRef.name))
        return true;
    if(!m_symbols.hasTypeSymbol(ifaceRef.name)){
        if(const BuiltinObjectTypeInfo* builtinObj = FindBuiltinObjectType(ifaceRef.name); builtinObj && !builtinObj->isCopyMoveConstructible)
            return true;
    }
    if(const InterfaceDecl* iface = m_symbols.findInterface(ifaceRef.name)){
        for(const TypeRef* baseRef : iface->baseInterfaces){
            if(baseRef && interfaceInheritsNoCopy(*baseRef, depth + 1u))
                return true;
        }
    }
    return false;
}

b8 TypeLayoutAnalyzer::isTypeConstructible(const TypeRef& type, const b8 requireCopy, const u32 depth)const noexcept{
    if(depth >= 8u || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return true;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return true;
    const MStringView name = unwrapped->name;
    if(const ClassDecl* cls = m_symbols.findClass(name)){
        if(!cls->subsystemTypeName.empty())
            return false;
        if(!cls->templateParams.empty() && !unwrapped->templateArgs.empty()){
            for(const TypeRef* implIface : cls->implementedInterfaces){
                if(implIface && interfaceInheritsNoCopy(*implIface))
                    return false;
            }
            for(const MethodDecl* m : cls->methods){
                if(m && m->isDestructor)
                    return false;
            }
            for(const FieldDecl* f : cls->fields){
                if(!f || !f->type)
                    continue;
                const TypeRef* subType = m_symbols.substituteTypeParams(f->type, cls->templateParams, unwrapped);
                if(subType && !isTypeConstructible(*subType, requireCopy, depth + 1u))
                    return false;
            }
            return true;
        }
        return isClassConstructible(*cls, cls->fields, requireCopy, depth + 1u);
    }
    if(m_symbols.hasTypeSymbol(name))
        return true;
    if(IsMoveOnlyTypeName(name))
        return !requireCopy;
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name); objInfo && !objInfo->isCopyMoveConstructible)
        return false;
    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name)){
        if(!genericInfo->isCopyMoveConstructible)
            return false;
        for(const TypeArg& targ : unwrapped->templateArgs){
            if(targ.type && !isTypeConstructible(*targ.type, requireCopy, depth + 1u))
                return false;
        }
        return true;
    }
    return true;
}

b8 TypeLayoutAnalyzer::isTypeAssignable(const TypeRef& type, const b8 requireCopy, const u32 depth)const noexcept{
    if(!isTypeConstructible(type, requireCopy, depth))
        return false;
    if(depth >= 8u || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return true;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return true;
    const MStringView name = unwrapped->name;
    if(const ClassDecl* cls = m_symbols.findClass(name)){
        if(!cls->templateParams.empty() && !unwrapped->templateArgs.empty()){
            for(const FieldDecl* f : cls->fields){
                if(!f || !f->type)
                    continue;
                const TypeRef* subType = m_symbols.substituteTypeParams(f->type, cls->templateParams, unwrapped);
                if(subType && !isTypeAssignable(*subType, requireCopy, depth + 1u))
                    return false;
            }
            return true;
        }
        return isClassAssignableImpl(*cls, cls->fields, requireCopy, depth + 1u);
    }
    if(m_symbols.hasTypeSymbol(name))
        return true;
    if(IsMoveOnlyTypeName(name))
        return !requireCopy;
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(name); objInfo && !objInfo->isAssignable)
        return false;
    if(FindBuiltinGenericType(name)){
        for(const TypeArg& targ : unwrapped->templateArgs){
            if(targ.type && !isTypeAssignable(*targ.type, requireCopy, depth + 1u))
                return false;
        }
        return true;
    }
    return true;
}

b8 TypeLayoutAnalyzer::isClassConstructible(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const b8 requireCopy, const u32 depth)const noexcept{
    if(depth >= 8u || !decl.subsystemTypeName.empty())
        return false;
    for(const TypeRef* implIface : decl.implementedInterfaces){
        if(implIface && interfaceInheritsNoCopy(*implIface))
            return false;
    }
    for(const MethodDecl* m : decl.methods){
        if(m && m->isDestructor)
            return false;
    }
    for(const FieldDecl* field : fields){
        if(field && field->type && !isTypeConstructible(*field->type, requireCopy, depth + 1u))
            return false;
    }
    return true;
}

b8 TypeLayoutAnalyzer::isClassAssignableImpl(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const b8 requireCopy, const u32 depth)const noexcept{
    if(!isClassConstructible(decl, fields, requireCopy, depth))
        return false;
    for(const MethodDecl* m : decl.methods){
        if(m && m->isOperator && OperatorSemantics::MapOperatorSymbolToTokenType(m->name) == TokenType::Equal)
            return false;
    }
    for(const FieldDecl* field : fields){
        if(field && field->type && !isTypeAssignable(*field->type, requireCopy, depth + 1u))
            return false;
    }
    return true;
}

b8 TypeLayoutAnalyzer::isClassCopyConstructible(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const u32 depth)const noexcept{
    return isClassConstructible(decl, fields, true, depth);
}

b8 TypeLayoutAnalyzer::isClassMoveConstructible(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const u32 depth)const noexcept{
    return isClassConstructible(decl, fields, false, depth);
}

b8 TypeLayoutAnalyzer::isClassCopyAssignable(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const u32 depth)const noexcept{
    return isClassAssignableImpl(decl, fields, true, depth);
}

b8 TypeLayoutAnalyzer::isClassMoveAssignable(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const u32 depth)const noexcept{
    return isClassAssignableImpl(decl, fields, false, depth);
}

b8 TypeLayoutAnalyzer::containsSimdRegisterType(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || type.isFuncType)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->isFuncType)
        return false;
    const MStringView baseName = m_symbols.resolveBaseTypeName(unwrapped->name);
    if(!m_symbols.hasTypeSymbol(baseName) && (SymbolTable::IsSimdRegisterType(baseName) || HasBuiltinSimdRegisterField(baseName)))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && containsSimdRegisterType(*targ.type, depth + 1u))
            return true;
    }
    return false;
}

b8 TypeLayoutAnalyzer::containsOverAlignedPackType(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return false;
    const MStringView baseName = unwrapped->name;
    if(const ClassDecl* cls = m_symbols.findClass(baseName))
        return !shouldPackSerializeClass(cls->fields, depth + 1u);
    if(m_symbols.hasTypeSymbol(baseName))
        return false;
    if(IsOverAlignedBuiltinType(baseName))
        return true;
    for(const TypeArg& targ : unwrapped->templateArgs){
        if(targ.type && containsOverAlignedPackType(*targ.type, depth + 1u))
            return true;
    }
    return false;
}

b8 TypeLayoutAnalyzer::shouldPackSerializeClass(const Span<const FieldDecl* const> fields, const u32 depth)const noexcept{
    if(depth >= 8u)
        return true;
    for(const FieldDecl* field : fields){
        if(field && field->type && containsOverAlignedPackType(*field->type, depth))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

