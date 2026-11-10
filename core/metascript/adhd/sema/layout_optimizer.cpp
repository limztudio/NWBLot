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


inline constexpr usize s_BareOptionalFallbackBytes = 16u;
inline constexpr usize s_BareExpectedFallbackBytes = 24u;

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
        if(!subCls->implementedInterfaces.empty()){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LayoutOptimizer::SortAndPackFields(const b8 isSerialize, const b8 packSerialize, const b8 hasInterfaces, MVector<FieldDecl*>& orderedFields, usize& outTotalSize, usize& outTotalAlign)noexcept{
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
    usize maxAlign = packSerialize ? 1u : (hasInterfaces ? 8u : 1u);
    for(FieldDecl* field : orderedFields){
        const usize align = (packSerialize && !field->isTransient) ? 1u : field->resolvedAlign;
        if(align > maxAlign)
            maxAlign = align;
        currentOffset = ::AlignUp(currentOffset, align);
        field->resolvedOffset = currentOffset;
        currentOffset += field->resolvedSize;
    }

    outTotalAlign = maxAlign;
    outTotalSize = currentOffset == 0u ? 1u : ::AlignUp(currentOffset, maxAlign);
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
            finalizeClassFieldLayouts(inst->templateDecl->isSerialize, !inst->templateDecl->implementedInterfaces.empty(), inst->orderedFields, inst->totalSize, inst->totalAlign);
            if(inst->totalSize != prevSize || inst->totalAlign != prevAlign)
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

    if(FindBuiltinFixedLayout(name, outSize, outAlign))
        return;

    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(name)){
        switch(genericInfo->inlineLayoutKind){
        case InlineLayoutKind::Atomic:{
            if(!type.templateArgs.empty() && type.templateArgs[0].type){
                resolveTypeSizeAndAlign(*type.templateArgs[0].type, depth + 1u, outSize, outAlign);
                return;
            }
            outSize = 8u;
            outAlign = 8u;
            return;
        }
        case InlineLayoutKind::Optional:{
            if(!type.templateArgs.empty() && type.templateArgs[0].type){
                usize innerSize = 8u;
                usize innerAlign = 8u;
                resolveTypeSizeAndAlign(*type.templateArgs[0].type, depth + 1u, innerSize, innerAlign);
                outAlign = innerAlign;
                outSize = ::AlignUp(innerSize + 1u, innerAlign);
                return;
            }
            outSize = __hidden_adhd_layout_optimizer::s_BareOptionalFallbackBytes;
            outAlign = 8u;
            return;
        }
        case InlineLayoutKind::Expected:{
            if(!type.templateArgs.empty() && type.templateArgs[0].type){
                usize valSize = 8u;
                usize valAlign = 8u;
                usize errSize = 1u;
                usize errAlign = 1u;
                resolveTypeSizeAndAlign(*type.templateArgs[0].type, depth + 1u, valSize, valAlign);
                if(type.templateArgs.size() >= 2u && type.templateArgs[1].type)
                    resolveTypeSizeAndAlign(*type.templateArgs[1].type, depth + 1u, errSize, errAlign);
                outAlign = valAlign > errAlign ? valAlign : errAlign;
                const usize payloadSize = valSize > errSize ? valSize : errSize;
                outSize = ::AlignUp(payloadSize + 1u, outAlign);
                return;
            }
            outSize = __hidden_adhd_layout_optimizer::s_BareExpectedFallbackBytes;
            outAlign = 8u;
            return;
        }
        case InlineLayoutKind::Array:
        case InlineLayoutKind::FixedVector:{
            usize elemSize = 4u;
            usize elemAlign = 4u;
            usize count = 1u;
            if(!type.templateArgs.empty() && type.templateArgs[0].type)
                resolveTypeSizeAndAlign(*type.templateArgs[0].type, depth + 1u, elemSize, elemAlign);
            if(type.templateArgs.size() >= 2u)
                count = ParseUnsignedConstant(type.templateArgs[1].constantOrTypeName(), m_symbols);
            if(genericInfo->inlineLayoutKind == InlineLayoutKind::Array){
                outSize = elemSize * count;
                outAlign = elemAlign;
            }
            else{
                outAlign = elemAlign > 4u ? elemAlign : 4u;
                outSize = ::AlignUp(elemSize * count + 4u, outAlign);
            }
            return;
        }
        case InlineLayoutKind::None:
            break;
        }
    }

    if(const EnumDecl* enumDecl = m_symbols.findEnum(name)){
        outSize = enumDecl->underlyingSize;
        outAlign = enumDecl->underlyingAlign;
        return;
    }
    if(const BuiltinEnumInfo* builtinEnum = FindBuiltinEnum(name)){
        outSize = builtinEnum->byteSize;
        outAlign = builtinEnum->byteAlign;
        return;
    }

    if(const ClassDecl* classDecl = m_symbols.findClass(name)){
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

    outSize = 8u;
    outAlign = 8u;
}

void LayoutOptimizer::markFieldArenaUsage(const TypeRef& fieldType, FieldDecl& field, ClassDecl& classDecl)const noexcept{
    if(!classDecl.subsystemTypeName.empty() || !classDecl.componentAssetTypeName.empty())
        return;
    if(m_symbols.resolveRefMode(&fieldType) == ParamPassingMode::None && SymbolTable::IsArenaBackedContainer(m_symbols.resolveBaseTypeName(fieldType.name))){
        const MStringView baseFieldName = m_symbols.resolveBaseTypeName(fieldType.name);
        field.usesClassArena = true;
        field.arenaFirstDefaultInit = IsArenaFirstCtorType(baseFieldName);
        classDecl.needsClassArena = true;
    }
    else if(TypeLayoutAnalyzer(m_symbols).containsArenaBackedContainer(fieldType)){
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

        markFieldArenaUsage(*field->type, *field, classDecl);

        if(classDecl.isSerialize && !field->isTransient){
            MStringView serializeErr;
            if(!__hidden_adhd_layout_optimizer::IsTypeBinarySerializable(*field->type, m_symbols, 0u, serializeErr)){
                ReportAdhdDiagnostic(m_arena, errors, field->span, serializeErr);
                return false;
            }
        }

        classDecl.orderedFields.push_back(field);
    }

    finalizeClassFieldLayouts(classDecl.isSerialize, !classDecl.implementedInterfaces.empty(), classDecl.orderedFields, classDecl.totalSize, classDecl.totalAlign);
    return true;
}

void LayoutOptimizer::finalizeClassFieldLayouts(const b8 isSerialize, const b8 hasVtablePointer, MVector<FieldDecl*>& orderedFields, usize& outTotalSize, usize& outTotalAlign)const noexcept{
    const b8 packSerialize = isSerialize && TypeLayoutAnalyzer(m_symbols).shouldPackSerializeClass(orderedFields);
    for(FieldDecl* field : orderedFields){
        if(!field || !field->type)
            continue;
        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*field->type, fieldSize, fieldAlign);
        field->resolvedSize = fieldSize;
        field->resolvedAlign = (packSerialize && !field->isTransient) ? 1u : fieldAlign;
    }
    SortAndPackFields(isSerialize, packSerialize, hasVtablePointer, orderedFields, outTotalSize, outTotalAlign);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

