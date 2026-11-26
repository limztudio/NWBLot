// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout_optimizer.h"

#include "template_instantiation_collector.h"

#include "type_comparator.h"
#include "type_layout_analyzer.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_layout_optimizer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsInlineValueGenericType(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info->byteSize == 0u;
    return false;
}

[[nodiscard]] b8 TypeReferencesClassByValue(const TypeRef& type, const MStringView targetClassName, const SymbolTable& symbols, const u32 depth = 0u)noexcept{
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

[[nodiscard]] b8 IsTypeBinarySerializable(const TypeRef& type, const SymbolTable& symbols, const u32 depth, MStringView& outReason)noexcept{
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
    if(IsNonSerializableBuiltinType(name)){
        outReason = "'[serialize]' class cannot contain non-transient dynamic heap, view, or atomic members; mark with '[transient]' or use fixed-size types";
        return false;
    }

    if(IsInlineValueGenericType(name)){
        for(const TypeArg& targ : unwrapped->templateArgs){
            if(targ.type && !IsTypeBinarySerializable(*targ.type, symbols, depth + 1u, outReason))
                return false;
        }
        return true;
    }

    if(const ClassDecl* subCls = symbols.findClass(name)){
        if(subCls->isExtern && subCls->isBinarySerializable)
            return true;
        if(symbols.hasPolymorphicInterface(*subCls)){
            outReason = "'[serialize]' class cannot contain non-transient polymorphic class members implementing an interface";
            return false;
        }
        for(const FieldDecl* subField : subCls->fields){
            if(subField && !subField->isTransient && subField->type){
                const TypeRef* subFieldType = symbols.substituteTypeParams(subField->type, subCls->templateParams, unwrapped);
                if(subFieldType && !IsTypeBinarySerializable(*subFieldType, symbols, depth + 1u, outReason))
                    return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LayoutOptimizer::SortAndPackFields(const b8 keepFieldOrder, const b8 packSerialize, const usize minimumAlignment, const usize headerPointerCount, MVector<FieldDecl*>& orderedFields, usize& outTotalSize, usize& outTotalAlign)noexcept{
    if(!keepFieldOrder){
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
    else{
        for(usize i = 1u; i < orderedFields.size(); ++i){
            FieldDecl* key = orderedFields[i];
            usize j = i;
            while(j > 0u && orderedFields[j - 1u]->originalIndex > key->originalIndex){
                orderedFields[j] = orderedFields[j - 1u];
                --j;
            }
            orderedFields[j] = key;
        }
    }

    usize currentOffset = headerPointerCount * 8u;
    usize maxAlign = packSerialize ? 1u : (headerPointerCount > 0u ? 8u : 1u);
    maxAlign = Max(maxAlign, minimumAlignment);
    for(FieldDecl* field : orderedFields){
        const usize align = (packSerialize && !field->isTransient) ? 1u : field->resolvedAlign;
        if(align > maxAlign)
            maxAlign = align;
        currentOffset = ::AlignUp(currentOffset, align);
        field->resolvedOffset = currentOffset;
        currentOffset += field->resolvedSize;
    }

    outTotalAlign = maxAlign;
    outTotalSize = currentOffset == 0u ? maxAlign : ::AlignUp(currentOffset, maxAlign);
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
    auto recomputeClassLayouts = [&]() -> b8{
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
        return changed;
    };
    for(usize iter = 0u; iter < unit.classes.size(); ++iter){
        if(!recomputeClassLayouts())
            break;
    }
    TemplateInstantiationCollector(m_arena, m_symbols, *this).collectAllTemplateInstantiations(unit);
    for(const ClassTemplateInstantiation* inst : unit.templateInstantiations){
        if(!inst || !inst->templateDecl || !inst->templateDecl->isSerialize)
            continue;
        for(const FieldDecl* clonedField : inst->orderedFields){
            if(!clonedField || clonedField->isTransient || !clonedField->type)
                continue;
            MStringView serializeErr;
            if(!__hidden_adhd_layout_optimizer::IsTypeBinarySerializable(*clonedField->type, m_symbols, 0u, serializeErr)){
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
            const b8 prevArena = inst->needsClassArena;
            for(FieldDecl* clonedField : inst->orderedFields){
                if(clonedField && clonedField->type)
                    markFieldArenaUsage(*clonedField->type, *clonedField, *inst->templateDecl, inst->needsClassArena);
            }
            const b8 hasMotherArenaPointer = inst->templateDecl->canBorrowMotherArena() && inst->needsClassArena;
            finalizeClassFieldLayouts(*inst->templateDecl, m_symbols.hasPolymorphicBase(*inst->templateDecl), hasMotherArenaPointer, inst->orderedFields, inst->totalSize, inst->totalAlign);
            if(inst->totalSize != prevSize || inst->totalAlign != prevAlign || inst->needsClassArena != prevArena)
                changed = true;
        }
        if(recomputeClassLayouts())
            changed = true;
        if(!changed)
            break;
    }
    return ok;
}

void LayoutOptimizer::resolveTypeSizeAndAlign(const TypeRef& type, const u32 depth, usize& outSize, usize& outAlign)const noexcept{
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
            resolveTypeSizeAndAlign(*unwrapped, depth + 1u, outSize, outAlign);
            return;
        }
    }

    if(const EnumDecl* enumDecl = m_symbols.findEnum(name)){
        outSize = enumDecl->underlyingSize;
        outAlign = enumDecl->underlyingAlign;
        return;
    }

    if(const ClassDecl* classDecl = m_symbols.findClass(name)){
        if(classDecl->hasExplicitLayout){
            outSize = classDecl->totalSize;
            outAlign = classDecl->totalAlign;
            return;
        }
        if(!type.templateArgs.empty() && m_unit){
            for(const ClassTemplateInstantiation* inst : m_unit->templateInstantiations){
                if(inst && inst->templateDecl == classDecl && TypeComparator(m_symbols).areTemplateArgListsEqual(inst->templateArgs, type.templateArgs)){
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

    if(!m_symbols.hasTypeSymbol(name)){
        if(FindBuiltinFixedLayout(name, outSize, outAlign))
            return;

        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name)){
            const auto resolveInner = [&](const TypeRef& innerType, const u32 innerDepth, usize& innerSize, usize& innerAlign)noexcept{
                resolveTypeSizeAndAlign(innerType, innerDepth, innerSize, innerAlign);
            };
            const auto parseCount = [&](const MStringView countText)noexcept -> usize{
                return ParseUnsignedConstant(countText, m_symbols);
            };
            if(ResolveInlineGenericLayout(*genericInfo, type, depth, outSize, outAlign, resolveInner, parseCount))
                return;
        }

        if(const BuiltinEnumInfo* builtinEnum = FindBuiltinEnum(name)){
            outSize = builtinEnum->byteSize;
            outAlign = builtinEnum->byteAlign;
            return;
        }
    }

    outSize = 8u;
    outAlign = 8u;
}

void LayoutOptimizer::markFieldArenaUsage(const TypeRef& fieldType, FieldDecl& field, const ClassDecl& classDecl, b8& inOutNeedsClassArena)const noexcept{
    if(classDecl.isSerialize || !classDecl.subsystemTypeName.empty())
        return;
    if(m_symbols.resolveRefMode(&fieldType) == ParamPassingMode::None){
        const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&fieldType);
        const TypeLayoutAnalyzer layoutAnalyzer(m_symbols);
        if(unwrapped && SymbolTable::IsArenaBackedContainer(unwrapped->name)){
            field.usesClassArena = true;
            field.arenaFirstDefaultInit = IsArenaFirstCtorType(unwrapped->name);
            inOutNeedsClassArena = true;
            return;
        }
        if(unwrapped && layoutAnalyzer.isNonRootMotherArenaType(*unwrapped)){
            field.usesClassArena = true;
            field.arenaFirstDefaultInit = true;
            inOutNeedsClassArena = true;
            return;
        }
        if(unwrapped && layoutAnalyzer.isFixedArrayNeedingArenaDefaultInit(*unwrapped)){
            field.usesClassArena = true;
            inOutNeedsClassArena = true;
            return;
        }
        if(layoutAnalyzer.requiresMotherArena(fieldType))
            inOutNeedsClassArena = true;
    }
}

b8 LayoutOptimizer::computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors){
    if(classDecl.isExtern){
        if(!classDecl.hasExplicitLayout){
            const auto resolveField = [&](const TypeRef& fieldType, usize& fieldSize, usize& fieldAlign)noexcept{
                resolveTypeSizeAndAlign(fieldType, fieldSize, fieldAlign);
            };
            ComputeSequentialExternClassLayout(classDecl, m_symbols.hasPolymorphicBase(classDecl), resolveField, classDecl.totalSize, classDecl.totalAlign);
        }
        return true;
    }

    if(!classDecl.isImported && classDecl.isSerialize && m_symbols.hasPolymorphicInterface(classDecl)){
        ReportAdhdDiagnostic(m_arena, errors, classDecl.span, "'[serialize]' class cannot implement an interface (virtual table pointers cannot be binary serialized)");
        return false;
    }

    if(!classDecl.systemTypeName.empty() || (classDecl.concreteBase && classDecl.concreteBase->canBorrowMotherArena() && classDecl.concreteBase->needsClassArena))
        classDecl.needsClassArena = true;

    classDecl.orderedFields.clear();
    classDecl.orderedFields.reserve(classDecl.fields.size());

    for(FieldDecl* field : classDecl.fields){
        if(!field->type || field->isStatic)
            continue;

        if(!classDecl.isImported && __hidden_adhd_layout_optimizer::TypeReferencesClassByValue(*field->type, classDecl.name, m_symbols)){
            ReportAdhdDiagnostic(m_arena, errors, field->span, "recursive by-value class member layout is prohibited; use 'in'/'mut' reference or 'Vector<T>'");
            return false;
        }

        markFieldArenaUsage(*field->type, *field, classDecl, classDecl.needsClassArena);

        if(!classDecl.isImported && classDecl.isSerialize && !field->isTransient){
            MStringView serializeErr;
            if(!__hidden_adhd_layout_optimizer::IsTypeBinarySerializable(*field->type, m_symbols, 0u, serializeErr)){
                ReportAdhdDiagnostic(m_arena, errors, field->span, serializeErr);
                return false;
            }
        }

        classDecl.orderedFields.push_back(field);
    }

    const b8 hasMotherArenaPointer = classDecl.canBorrowMotherArena() && classDecl.needsClassArena;
    finalizeClassFieldLayouts(classDecl, m_symbols.hasPolymorphicBase(classDecl), hasMotherArenaPointer, classDecl.orderedFields, classDecl.totalSize, classDecl.totalAlign);
    return true;
}

void LayoutOptimizer::finalizeClassFieldLayouts(const ClassDecl& decl, const b8 hasVtablePointer, const b8 hasMotherArenaPointer, MVector<FieldDecl*>& orderedFields, usize& outTotalSize, usize& outTotalAlign)const noexcept{
    const b8 packSerialize = decl.isSerialize && !decl.recordAlignment && TypeLayoutAnalyzer(m_symbols).shouldPackSerializeClass(orderedFields);
    for(FieldDecl* field : orderedFields){
        if(!field || !field->type)
            continue;
        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*field->type, fieldSize, fieldAlign);
        field->resolvedSize = fieldSize;
        field->resolvedAlign = (packSerialize && !field->isTransient) ? 1u : fieldAlign;
    }
    const usize baseBytes = decl.concreteBase ? decl.concreteBase->totalSize : (hasVtablePointer || decl.isExtensible ? 8u : 0u);
    const usize headerPointerCount = (baseBytes + 7u) / 8u + (hasMotherArenaPointer ? 1u : 0u);
    const b8 keepFieldOrder = TypeLayoutAnalyzer(m_symbols).requiresDeclarationFieldOrder(decl, Span<const FieldDecl* const>(orderedFields.data(), orderedFields.size()));
    SortAndPackFields(keepFieldOrder, packSerialize, decl.concreteBase ? Max(decl.recordAlignment, decl.concreteBase->totalAlign) : decl.recordAlignment, headerPointerCount, orderedFields, outTotalSize, outTotalAlign);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

