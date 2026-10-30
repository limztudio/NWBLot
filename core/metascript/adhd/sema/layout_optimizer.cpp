// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout_optimizer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_layout_optimizer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr usize AlignUp(const usize offset, const usize alignment)noexcept{
    if(alignment <= 1u)
        return offset;
    return (offset + alignment - 1u) & ~(alignment - 1u);
}

[[nodiscard]] b8 IsInlineValueGenericType(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->byteSize == 0u;
    return false;
}

[[nodiscard]] b8 TypeReferencesClassByValue(
    const TypeRef& type,
    const MStringView targetClassName,
    const SymbolTable& symbols,
    const u32 depth = 0u
){
    if(depth > 16u || type.refMode != ParamPassingMode::None)
        return false;
    const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type);
    if(!unwrapped || unwrapped->refMode != ParamPassingMode::None || unwrapped->isFuncType)
        return false;
    const MStringView name = unwrapped->name;
    if(name == targetClassName)
        return true;
    if(IsInlineValueGenericType(name)){
        for(const TypeArg& targ : unwrapped->templateArgs){
            if(targ.type && TypeReferencesClassByValue(*targ.type, targetClassName, symbols, depth + 1u))
                return true;
        }
        return false;
    }
    if(const ClassDecl* subCls = symbols.findClass(name)){
        for(const FieldDecl* subField : subCls->fields){
            if(!subField || !subField->type)
                continue;
            const TypeRef* subFieldType = symbols.substituteTypeParams(subField->type, subCls->templateParams, unwrapped);
            if(subFieldType && TypeReferencesClassByValue(*subFieldType, targetClassName, symbols, depth + 1u))
                return true;
        }
    }
    return false;
}

[[nodiscard]] b8 IsTypeBinarySerializable(
    const TypeRef& type,
    const SymbolTable& symbols,
    MStringView& outReason,
    const u32 depth = 0u
){
    if(depth >= 16u)
        return true;

    if(type.refMode != ParamPassingMode::None){
        outReason = "'[serialize]' class cannot contain non-transient 'in'/'mut' reference members";
        return false;
    }

    const TypeRef* unwrapped = symbols.unwrapTypeAlias(&type);
    if(!unwrapped)
        return true;

    if(unwrapped->refMode != ParamPassingMode::None){
        outReason = "'[serialize]' class cannot contain non-transient 'in'/'mut' reference members";
        return false;
    }

    if(unwrapped->isFuncType){
        outReason = "'[serialize]' class cannot contain non-transient dynamic heap, view, or callable members; mark with '[transient]' or use fixed-size types";
        return false;
    }

    const MStringView name = unwrapped->name;
    if(SymbolTable::isArenaBackedContainer(name) || name == "Path" || name == "StringView" || name == "Span" || name == "Atomic"){
        outReason = "'[serialize]' class cannot contain non-transient dynamic heap, view, or atomic members; mark with '[transient]' or use fixed-size types";
        return false;
    }

    if(IsInlineValueGenericType(name)){
        for(const TypeArg& targ : unwrapped->templateArgs){
            if(targ.type && !IsTypeBinarySerializable(*targ.type, symbols, outReason, depth + 1u))
                return false;
        }
        return true;
    }

    if(const ClassDecl* subCls = symbols.findClass(name)){
        if(!subCls->implementedInterfaces.empty()){
            outReason = "'[serialize]' class cannot contain non-transient polymorphic class members implementing an interface";
            return false;
        }
        for(const FieldDecl* subField : subCls->fields){
            if(subField && !subField->isTransient && subField->type){
                const TypeRef* subFieldType = symbols.substituteTypeParams(subField->type, subCls->templateParams, unwrapped);
                if(subFieldType && !IsTypeBinarySerializable(*subFieldType, symbols, outReason, depth + 1u))
                    return false;
            }
        }
    }

    return true;
}

[[nodiscard]] b8 ClassDependsOnClassByValue(
    const ClassDecl& cls,
    const MStringView targetClassName,
    const SymbolTable& symbols,
    const u32 depth = 0u
)noexcept{
    for(const FieldDecl* field : cls.fields){
        if(field && field->type && TypeRefReferencesSymbolByValue(*field->type, targetClassName, symbols, depth))
            return true;
    }
    return false;
}

void VisitTypeAliasTopo(
    const usize idx,
    const Span<const TypeAliasDecl* const> aliases,
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
    const Span<const InterfaceDecl* const> interfaces,
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
    const Span<const ClassDecl* const> classes,
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

[[nodiscard]] b8 HasTemplateParamName(const Span<const TemplateParamDecl> params, const MStringView name)noexcept{
    for(const TemplateParamDecl& p : params){
        if(p.name == name)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeRefReferencesSymbol(const TypeRef& type, const MStringView targetName, const u32 depth)noexcept{
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

b8 TypeRefReferencesSymbolByValue(
    const TypeRef& type,
    const MStringView targetName,
    const SymbolTable& symbols,
    const u32 depth
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
        if(!cls->templateParams.empty() && __hidden_adhd_layout_optimizer::ClassDependsOnClassByValue(*cls, targetName, symbols, depth + 1u))
            return true;
    }
    for(const TypeArg& targ : type.templateArgs){
        if(targ.type && TypeRefReferencesSymbolByValue(*targ.type, targetName, symbols, depth + 1u))
            return true;
    }
    return false;
}

b8 InstantiationDependsOnClassByValue(
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

void SortTypeAliasesTopologically(
    const Span<const TypeAliasDecl* const> aliases,
    MetaArena& arena,
    MVector<const TypeAliasDecl*>& outSorted
){
    outSorted.clear();
    if(aliases.empty())
        return;
    MVector<u8> state(aliases.size(), 0u, arena);
    outSorted.reserve(aliases.size());
    for(usize i = 0u; i < aliases.size(); ++i)
        __hidden_adhd_layout_optimizer::VisitTypeAliasTopo(i, aliases, state, outSorted);
}

void SortInterfacesTopologically(
    const Span<const InterfaceDecl* const> interfaces,
    const SymbolTable& symbols,
    MetaArena& arena,
    MVector<const InterfaceDecl*>& outSorted
){
    outSorted.clear();
    if(interfaces.empty())
        return;
    MVector<u8> state(interfaces.size(), 0u, arena);
    outSorted.reserve(interfaces.size());
    for(usize i = 0u; i < interfaces.size(); ++i)
        __hidden_adhd_layout_optimizer::VisitInterfaceTopo(i, interfaces, symbols, state, outSorted);
}

void SortClassesTopologically(
    const Span<const ClassDecl* const> classes,
    const SymbolTable& symbols,
    MetaArena& arena,
    MVector<const ClassDecl*>& outSorted
){
    outSorted.clear();
    if(classes.empty())
        return;
    MVector<u8> state(classes.size(), 0u, arena);
    outSorted.reserve(classes.size());
    for(usize i = 0u; i < classes.size(); ++i)
        __hidden_adhd_layout_optimizer::VisitClassTopo(i, classes, symbols, state, outSorted);
}

void CollectExternalForwardDeclClasses(
    const TypeRef& type,
    const SymbolTable& symbols,
    const Span<const TemplateParamDecl> outerTemplateParams,
    const Span<const TemplateParamDecl> innerTemplateParams,
    MVector<MStringView>& outNames,
    const b8 inRefOrAssetContext,
    const u32 depth
){
    if(depth > 16u || type.isAuto)
        return;

    if(type.isFuncType){
        if(type.funcReturnType){
            CollectExternalForwardDeclClasses(
                *type.funcReturnType,
                symbols,
                outerTemplateParams,
                innerTemplateParams,
                outNames,
                type.funcReturnType->refMode != ParamPassingMode::None,
                depth + 1u
            );
        }
        for(const FuncParamType& fp : type.funcParams){
            if(fp.type){
                CollectExternalForwardDeclClasses(
                    *fp.type,
                    symbols,
                    outerTemplateParams,
                    innerTemplateParams,
                    outNames,
                    true,
                    depth + 1u
                );
            }
        }
        return;
    }

    const b8 isRefOrAsset = inRefOrAssetContext || (type.refMode != ParamPassingMode::None);
    if(isRefOrAsset
        && !type.name.empty()
        && type.templateArgs.empty()
        && !__hidden_adhd_layout_optimizer::HasTemplateParamName(outerTemplateParams, type.name)
        && !__hidden_adhd_layout_optimizer::HasTemplateParamName(innerTemplateParams, type.name)
        && !IsKnownConcreteTypeName(type.name, symbols))
    {
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

    const b8 childInAssetContext = (type.name == "AssetRef");
    for(const TypeArg& targ : type.templateArgs){
        if(targ.type){
            CollectExternalForwardDeclClasses(
                *targ.type,
                symbols,
                outerTemplateParams,
                innerTemplateParams,
                outNames,
                childInAssetContext || (targ.type->refMode != ParamPassingMode::None),
                depth + 1u
            );
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LayoutOptimizer::sortAndPackFields(
    MVector<FieldDecl*>& orderedFields,
    const b8 isSerialize,
    const b8 hasInterfaces,
    usize& outTotalSize,
    usize& outTotalAlign
)noexcept{
    if(!isSerialize){
        for(usize i = 1u; i < orderedFields.size(); ++i){
            FieldDecl* key = orderedFields[i];
            usize j = i;
            while(j > 0u){
                const FieldDecl* prev = orderedFields[j - 1u];
                const b8 keyBeforePrev = (key->resolvedAlign > prev->resolvedAlign)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize > prev->resolvedSize)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize == prev->resolvedSize && key->originalIndex < prev->originalIndex);
                if(!keyBeforePrev)
                    break;
                orderedFields[j] = orderedFields[j - 1u];
                --j;
            }
            orderedFields[j] = key;
        }
    }

    usize currentOffset = hasInterfaces ? 8u : 0u;
    usize maxAlign = isSerialize ? 1u : (hasInterfaces ? 8u : 1u);
    for(FieldDecl* field : orderedFields){
        const usize align = (isSerialize && !field->isTransient) ? 1u : field->resolvedAlign;
        if(align > maxAlign)
            maxAlign = align;
        currentOffset = __hidden_adhd_layout_optimizer::AlignUp(currentOffset, align);
        field->resolvedOffset = currentOffset;
        currentOffset += field->resolvedSize;
    }

    outTotalAlign = maxAlign;
    outTotalSize = currentOffset == 0u ? 1u : __hidden_adhd_layout_optimizer::AlignUp(currentOffset, maxAlign);
}

b8 LayoutOptimizer::optimizeTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    m_unit = &unit;
    b8 ok = true;
    for(ClassDecl* classDecl : unit.classes){
        if(!computeClassLayout(*classDecl, errors))
            ok = false;
    }
    if(!ok)
        return false;
    MVector<ParseError> silentErrors(m_arena);
    for(usize iter = 0u; iter < unit.classes.size(); ++iter){
        b8 changed = false;
        for(ClassDecl* classDecl : unit.classes){
            const usize prevSize = classDecl->totalSize;
            const usize prevAlign = classDecl->totalAlign;
            const b8 prevArena = classDecl->needsClassArena;
            if(!computeClassLayout(*classDecl, silentErrors))
                ok = false;
            if(classDecl->totalSize != prevSize || classDecl->totalAlign != prevAlign || classDecl->needsClassArena != prevArena)
                changed = true;
        }
        if(!changed)
            break;
    }
    collectAllTemplateInstantiations(unit);
    for(const ClassTemplateInstantiation* inst : unit.templateInstantiations){
        if(!inst || !inst->templateDecl || !inst->templateDecl->isSerialize)
            continue;
        for(const FieldDecl* clonedField : inst->orderedFields){
            if(!clonedField || clonedField->isTransient || !clonedField->type)
                continue;
            MStringView serializeErr;
            if(!__hidden_adhd_layout_optimizer::IsTypeBinarySerializable(*clonedField->type, m_symbols, serializeErr)){
                ReportAdhdDiagnostic(m_arena, errors, clonedField->span, serializeErr);
                return false;
            }
        }
    }
    const usize maxPostPasses = unit.classes.size() + unit.templateInstantiations.size() + 1u;
    for(usize iter = 0u; iter < maxPostPasses; ++iter){
        b8 changed = false;
        for(ClassTemplateInstantiation* inst : unit.templateInstantiations){
            if(!inst || !inst->templateDecl)
                continue;
            const usize prevSize = inst->totalSize;
            const usize prevAlign = inst->totalAlign;
            finalizeClassFieldLayouts(
                inst->orderedFields,
                inst->templateDecl->isSerialize,
                !inst->templateDecl->implementedInterfaces.empty(),
                inst->totalSize,
                inst->totalAlign
            );
            if(inst->totalSize != prevSize || inst->totalAlign != prevAlign)
                changed = true;
        }
        for(ClassDecl* classDecl : unit.classes){
            const usize prevSize = classDecl->totalSize;
            const usize prevAlign = classDecl->totalAlign;
            const b8 prevArena = classDecl->needsClassArena;
            if(!computeClassLayout(*classDecl, silentErrors))
                ok = false;
            if(classDecl->totalSize != prevSize || classDecl->totalAlign != prevAlign || classDecl->needsClassArena != prevArena)
                changed = true;
        }
        if(!changed)
            break;
    }
    return ok;
}

void LayoutOptimizer::resolveTypeSizeAndAlign(const TypeRef& type, usize& outSize, usize& outAlign, const u32 depth)const noexcept{
    if(depth > 16u || m_symbols.resolveRefMode(&type) != ParamPassingMode::None){
        outSize = 8u;
        outAlign = 8u;
        return;
    }

    if(type.isFuncType){
        outSize = (type.inferredCaptureBytes == 0u) ? 8u : (type.inferredCaptureBytes + 8u);
        outAlign = 8u;
        return;
    }

    const MStringView name = type.name;

    if(const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type)){
        if(unwrapped != &type){
            resolveTypeSizeAndAlign(*unwrapped, outSize, outAlign, depth + 1u);
            return;
        }
    }

    if(FindBuiltinFixedLayout(name, outSize, outAlign))
        return;

    if(name == "Atomic"){
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, outSize, outAlign, depth + 1u);
            return;
        }
        outSize = 8u;
        outAlign = 8u;
        return;
    }

    if(name == "Optional"){
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            usize innerSize = 8u;
            usize innerAlign = 8u;
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, innerSize, innerAlign, depth + 1u);
            outAlign = innerAlign;
            outSize = __hidden_adhd_layout_optimizer::AlignUp(innerSize + 1u, innerAlign);
            return;
        }
        outSize = 16u;
        outAlign = 8u;
        return;
    }

    if(name == "Expected"){
        if(type.templateArgs.size() >= 2u && type.templateArgs[0].type && type.templateArgs[1].type){
            usize valSize = 8u;
            usize valAlign = 8u;
            usize errSize = 8u;
            usize errAlign = 8u;
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, valSize, valAlign, depth + 1u);
            resolveTypeSizeAndAlign(*type.templateArgs[1].type, errSize, errAlign, depth + 1u);
            outAlign = valAlign > errAlign ? valAlign : errAlign;
            const usize payloadSize = valSize > errSize ? valSize : errSize;
            outSize = __hidden_adhd_layout_optimizer::AlignUp(payloadSize + 1u, outAlign);
            return;
        }
        outSize = 24u;
        outAlign = 8u;
        return;
    }

    if(name == "Array" || name == "FixedVector"){
        usize elemSize = 4u;
        usize elemAlign = 4u;
        usize count = 1u;
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, elemSize, elemAlign, depth + 1u);
        if(type.templateArgs.size() >= 2u){
            const MStringView countText = type.templateArgs[1].type ? type.templateArgs[1].type->name : type.templateArgs[1].constantText;
            count = ParseUnsignedConstant(countText, m_symbols);
        }
        if(name == "Array"){
            outSize = elemSize * count;
            outAlign = elemAlign;
        }
        else{
            outAlign = elemAlign > 4u ? elemAlign : 4u;
            outSize = __hidden_adhd_layout_optimizer::AlignUp(elemSize * count + 4u, outAlign);
        }
        return;
    }

    if(const EnumDecl* enumDecl = m_symbols.findEnum(name)){
        outSize = enumDecl->underlyingSize;
        outAlign = enumDecl->underlyingAlign;
        return;
    }

    if(const ClassDecl* classDecl = m_symbols.findClass(name)){
        if(!type.templateArgs.empty() && m_unit){
            for(const ClassTemplateInstantiation* inst : m_unit->templateInstantiations){
                if(inst && inst->templateDecl == classDecl && AreTemplateArgListsEqual(inst->templateArgs, type.templateArgs, m_symbols)){
                    outSize = inst->totalSize;
                    outAlign = inst->totalAlign;
                    return;
                }
            }
        }
        outSize = classDecl->totalSize;
        outAlign = classDecl->totalAlign;
        return;
    }

    outSize = 8u;
    outAlign = 8u;
}

void LayoutOptimizer::markFieldArenaUsage(FieldDecl& field, const TypeRef& fieldType, ClassDecl& classDecl)const noexcept{
    if(!classDecl.subsystemTypeName.empty() || !classDecl.componentAssetTypeName.empty())
        return;
    if(m_symbols.resolveRefMode(&fieldType) == ParamPassingMode::None
        && SymbolTable::isArenaBackedContainer(m_symbols.resolveBaseTypeName(fieldType.name)))
    {
        field.usesClassArena = true;
        classDecl.needsClassArena = true;
    }
    else if(TypeContainsArenaBackedContainer(fieldType, m_symbols)){
        classDecl.needsClassArena = true;
    }
}

b8 LayoutOptimizer::computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors){
    if(classDecl.isSerialize && !classDecl.implementedInterfaces.empty()){
        ReportAdhdDiagnostic(m_arena, errors, classDecl.span, "'[serialize]' class cannot implement an interface (virtual table pointers cannot be binary serialized)");
        return false;
    }

    classDecl.orderedFields.clear();
    classDecl.orderedFields.reserve(classDecl.fields.size());

    for(FieldDecl* field : classDecl.fields){
        if(!field->type)
            continue;

        if(__hidden_adhd_layout_optimizer::TypeReferencesClassByValue(*field->type, classDecl.name, m_symbols)){
            ReportAdhdDiagnostic(m_arena, errors, field->span, "recursive by-value class member layout is prohibited; use 'in'/'mut' reference or 'Vector<T>'");
            return false;
        }

        markFieldArenaUsage(*field, *field->type, classDecl);

        if(classDecl.isSerialize && !field->isTransient){
            MStringView serializeErr;
            if(!__hidden_adhd_layout_optimizer::IsTypeBinarySerializable(*field->type, m_symbols, serializeErr)){
                ReportAdhdDiagnostic(m_arena, errors, field->span, serializeErr);
                return false;
            }
        }

        classDecl.orderedFields.push_back(field);
    }

    finalizeClassFieldLayouts(
        classDecl.orderedFields,
        classDecl.isSerialize,
        !classDecl.implementedInterfaces.empty(),
        classDecl.totalSize,
        classDecl.totalAlign
    );
    return true;
}

void LayoutOptimizer::finalizeClassFieldLayouts(
    MVector<FieldDecl*>& orderedFields,
    const b8 isSerialize,
    const b8 hasVtablePointer,
    usize& outTotalSize,
    usize& outTotalAlign
)const noexcept{
    for(FieldDecl* field : orderedFields){
        if(!field || !field->type)
            continue;
        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*field->type, fieldSize, fieldAlign);
        field->resolvedSize = fieldSize;
        field->resolvedAlign = (isSerialize && !field->isTransient) ? 1u : fieldAlign;
    }
    sortAndPackFields(orderedFields, isSerialize, hasVtablePointer, outTotalSize, outTotalAlign);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

