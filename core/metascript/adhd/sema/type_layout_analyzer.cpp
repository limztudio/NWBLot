// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/operator_semantics.h>

#include "type_layout_analyzer.h"
#include "type_comparator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeLayoutAnalyzer::isNonRootMotherArenaType(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 8u || m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return false;
    const ClassDecl* cls = m_symbols.findClass(unwrapped->name);
    if(!cls)
        return !m_symbols.hasTypeSymbol(unwrapped->name) && IsArenaFirstCtorType(unwrapped->name) && !SymbolTable::IsArenaBackedContainer(unwrapped->name);
    if(!cls->canBorrowMotherArena())
        return false;
    if(cls->needsClassArena || !cls->systemTypeName.empty() || cls->arenaCtorMode == 1u)
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

b8 TypeLayoutAnalyzer::isArenaBackedValueWrapper(const TypeRef& type)const noexcept{
    const TypeRef* raw = m_symbols.unwrapTypeAlias(&type);
    if(!raw || m_symbols.resolveRefMode(&type) != ParamPassingMode::None || m_symbols.hasTypeSymbol(raw->name))
        return false;
    return IsOptionalOrExpectedGenericType(raw->name) && requiresMotherArena(*raw);
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
    if(const ClassDecl* cls = m_symbols.findClass(ifaceRef.name); cls && cls->isExtern && !cls->isCopyConstructible)
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

b8 TypeLayoutAnalyzer::supportsTypeOperation(const TypeRef& type, const b8 requireCopy, const b8 assign, const ValueTypeVisit* const active)const noexcept{
    if(m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return true;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return true;
    const MStringView name = unwrapped->name;
    if(const ClassDecl* cls = m_symbols.findClass(name))
        return supportsClassOperation(*cls, cls->fields, unwrapped, requireCopy, assign, active);
    if(m_symbols.hasTypeSymbol(name))
        return true;
    if(IsMoveOnlyTypeName(name))
        return !requireCopy;
    if(IsSmartPointerDerefType(name))
        return true;
    if(const BuiltinObjectTypeInfo* obj = FindBuiltinObjectType(name); obj && (!obj->isCopyMoveConstructible || (assign && !obj->isAssignable)))
        return false;
    if(const BuiltinGenericTypeInfo* generic = FindBuiltinGenericType(name)){
        if(!generic->isCopyMoveConstructible)
            return false;
        for(const TypeArg& arg : unwrapped->templateArgs){
            if(arg.type && !supportsTypeOperation(*arg.type, requireCopy, assign, active))
                return false;
        }
    }
    return true;
}

b8 TypeLayoutAnalyzer::supportsClassOperation(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const TypeRef* const instance, const b8 requireCopy, const b8 assign, const ValueTypeVisit* const active)const noexcept{
    if(decl.isExtensible || decl.concreteBase || (requireCopy && decl.isMoveOnly) || !decl.subsystemTypeName.empty() || !decl.systemTypeName.empty() || decl.isAbstract)
        return false;
    for(const TypeRef* iface : decl.implementedInterfaces){
        if(iface && interfaceInheritsNoCopy(*iface) && (requireCopy || assign || !decl.isMoveOnly || !IsNonCopyableBaseInterfaceName(iface->name)))
            return false;
    }
    if(decl.isExtern){
        if(requireCopy)
            return decl.isCopyConstructible && (!assign || decl.isCopyAssignable);
        return decl.isMoveConstructible && (!assign || decl.isMoveAssignable);
    }
    if(assign && SymbolTable::HasCustomAssignOperator(decl))
        return false;
    for(const MethodDecl* method : decl.methods){
        if(method && method->isDestructor)
            return false;
    }

    usize repeatedSpecializations = 0u;
    for(const ValueTypeVisit* visit = active; visit; visit = visit->parent){
        if(&visit->decl != &decl)
            continue;
        if(decl.templateParams.empty() || TypeComparator(m_symbols).areEquivalent(visit->instance, instance))
            return true;
        if(++repeatedSpecializations == 64u)
            return false;
    }
    const ValueTypeVisit current{ decl, instance, active };
    for(const FieldDecl* field : fields){
        if(!field || field->isStatic || !field->type)
            continue;
        const TypeRef* type = instance && !decl.templateParams.empty() && !instance->templateArgs.empty()
            ? m_symbols.substituteTypeParams(field->type, decl.templateParams, instance)
            : field->type;
        if(type && !supportsTypeOperation(*type, requireCopy, assign, &current))
            return false;
    }
    return true;
}

b8 TypeLayoutAnalyzer::isTypeConstructible(const TypeRef& type, const b8 requireCopy)const noexcept{
    return supportsTypeOperation(type, requireCopy, false, nullptr);
}

b8 TypeLayoutAnalyzer::isTypeAssignable(const TypeRef& type, const b8 requireCopy)const noexcept{
    return supportsTypeOperation(type, requireCopy, true, nullptr);
}

b8 TypeLayoutAnalyzer::isClassCopyConstructible(const ClassDecl& decl, const Span<const FieldDecl* const> fields)const noexcept{
    return supportsClassOperation(decl, fields, nullptr, true, false, nullptr);
}

b8 TypeLayoutAnalyzer::isClassMoveConstructible(const ClassDecl& decl, const Span<const FieldDecl* const> fields)const noexcept{
    return supportsClassOperation(decl, fields, nullptr, false, false, nullptr);
}

b8 TypeLayoutAnalyzer::isClassCopyAssignable(const ClassDecl& decl, const Span<const FieldDecl* const> fields)const noexcept{
    return supportsClassOperation(decl, fields, nullptr, true, true, nullptr);
}

b8 TypeLayoutAnalyzer::isClassMoveAssignable(const ClassDecl& decl, const Span<const FieldDecl* const> fields)const noexcept{
    return supportsClassOperation(decl, fields, nullptr, false, true, nullptr);
}

b8 TypeLayoutAnalyzer::isClassNothrowDefaultConstructible(const ClassDecl& decl, const Span<const FieldDecl* const> fields, const u32 depth)const noexcept{
    if(depth >= 8u)
        return false;
    if(decl.concreteBase){
        const ClassDecl& base = *decl.concreteBase;
        b8 baseHasUserCtor = false;
        b8 baseZeroCtorUnwinds = false;
        for(const MethodDecl* method : base.methods){
            if(method && method->isConstructor){
                baseHasUserCtor = true;
                if(method->minArgCount == 0u && method->allowsCppUnwind)
                    baseZeroCtorUnwinds = true;
            }
        }
        if(baseZeroCtorUnwinds)
            return false;
        if(!baseHasUserCtor && !base.isExtern && !isClassNothrowDefaultConstructible(base, base.fields, depth + 1u))
            return false;
    }
    for(const FieldDecl* field : fields){
        if(!field || field->isStatic || !field->type)
            continue;
        if(m_symbols.resolveRefMode(field->type) != ParamPassingMode::None)
            continue;
        const TypeRef* checkType = m_symbols.unwrapTypeAlias(field->type);
        while(checkType && !m_symbols.hasTypeSymbol(checkType->name) && IsFixedArrayContainerType(checkType->name)){
            if(checkType->templateArgs.empty() || !checkType->templateArgs[0].type){
                checkType = nullptr;
                break;
            }
            checkType = m_symbols.unwrapTypeAlias(checkType->templateArgs[0].type);
        }
        if(!checkType)
            continue;
        const MStringView baseFieldType = m_symbols.resolveBaseTypeName(checkType->name);
        if(const ClassDecl* fieldCls = m_symbols.findClass(baseFieldType)){
            b8 fieldHasUserCtor = false;
            for(const MethodDecl* method : fieldCls->methods){
                if(!method || !method->isConstructor)
                    continue;
                fieldHasUserCtor = true;
                if(field->hasCtorInit){
                    const usize argCount = field->ctorArgs.size();
                    if(argCount >= method->minArgCount && argCount <= method->params.size() && method->allowsCppUnwind)
                        return false;
                }
                else if(field->defaultExpr){
                    if(method->allowsCppUnwind)
                        return false;
                }
                else if(method->minArgCount == 0u && method->allowsCppUnwind){
                    return false;
                }
            }
            if(!fieldHasUserCtor && !fieldCls->isExtern && !isClassNothrowDefaultConstructible(*fieldCls, fieldCls->fields, depth + 1u))
                return false;
        }
    }
    return true;
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

