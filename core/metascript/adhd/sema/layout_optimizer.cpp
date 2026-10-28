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

[[nodiscard]] bool TypeReferencesClassByValue(
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
    if(name == "Array" || name == "FixedVector" || name == "Optional"){
        if(!unwrapped->templateArgs.empty() && unwrapped->templateArgs[0].type)
            return TypeReferencesClassByValue(*unwrapped->templateArgs[0].type, targetClassName, symbols, depth + 1u);
        return false;
    }
    if(const ClassDecl* subCls = symbols.findClass(name)){
        const bool hasTemplateSub = !subCls->templateParams.empty() && unwrapped->templateArgs.size() == subCls->templateParams.size();
        for(const FieldDecl* subField : subCls->fields){
            if(!subField || !subField->type)
                continue;
            const TypeRef* subFieldType = hasTemplateSub
                ? symbols.substituteTypeParams(*subField->type, subCls->templateParams, unwrapped->templateArgs)
                : subField->type;
            if(subFieldType && TypeReferencesClassByValue(*subFieldType, targetClassName, symbols, depth + 1u))
                return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsTypeBinarySerializable(
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
    if(SymbolTable::isArenaBackedContainer(name) || name == "Path" || name == "StringView" || name == "Span"){
        outReason = "'[serialize]' class cannot contain non-transient dynamic heap or view members; mark with '[transient]' or use fixed-size types";
        return false;
    }

    if(name == "Array" || name == "FixedVector" || name == "Optional"){
        if(!unwrapped->templateArgs.empty() && unwrapped->templateArgs[0].type){
            if(!IsTypeBinarySerializable(*unwrapped->templateArgs[0].type, symbols, outReason, depth + 1u))
                return false;
        }
        return true;
    }

    if(const ClassDecl* subCls = symbols.findClass(name)){
        if(!subCls->implementedInterfaces.empty()){
            outReason = "'[serialize]' class cannot contain non-transient polymorphic class members implementing an interface";
            return false;
        }
        const bool hasTemplateSub = !subCls->templateParams.empty() && unwrapped->templateArgs.size() == subCls->templateParams.size();
        for(const FieldDecl* subField : subCls->fields){
            if(subField && !subField->isTransient && subField->type){
                const TypeRef* subFieldType = hasTemplateSub
                    ? symbols.substituteTypeParams(*subField->type, subCls->templateParams, unwrapped->templateArgs)
                    : subField->type;
                if(subFieldType && !IsTypeBinarySerializable(*subFieldType, symbols, outReason, depth + 1u))
                    return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutOptimizer::optimizeTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    m_unit = &unit;
    bool ok = true;
    for(ClassDecl* classDecl : unit.classes){
        if(!computeClassLayout(*classDecl, errors))
            ok = false;
    }
    if(!ok)
        return false;
    MVector<ParseError> silentErrors(m_arena);
    for(usize iter = 0u; iter < unit.classes.size(); ++iter){
        bool changed = false;
        for(ClassDecl* classDecl : unit.classes){
            const usize prevSize = classDecl->totalSize;
            const usize prevAlign = classDecl->totalAlign;
            const bool prevArena = classDecl->needsClassArena;
            (void)computeClassLayout(*classDecl, silentErrors);
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
        bool changed = false;
        for(ClassTemplateInstantiation* inst : unit.templateInstantiations){
            if(!inst || !inst->templateDecl)
                continue;
            const usize prevSize = inst->totalSize;
            const usize prevAlign = inst->totalAlign;
            for(FieldDecl* clonedField : inst->orderedFields){
                if(!clonedField || !clonedField->type)
                    continue;
                usize fieldSize = 8u;
                usize fieldAlign = 8u;
                resolveTypeSizeAndAlign(*clonedField->type, fieldSize, fieldAlign);
                clonedField->resolvedSize = fieldSize;
                clonedField->resolvedAlign = inst->templateDecl->isSerialize ? 1u : fieldAlign;
            }
            sortAndPackFields(
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
            const bool prevArena = classDecl->needsClassArena;
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

void LayoutOptimizer::sortAndPackFields(
    MVector<FieldDecl*>& orderedFields,
    const bool isSerialize,
    const bool hasInterfaces,
    usize& outTotalSize,
    usize& outTotalAlign
)noexcept{
    if(!isSerialize){
        for(usize i = 1u; i < orderedFields.size(); ++i){
            FieldDecl* key = orderedFields[i];
            usize j = i;
            while(j > 0u){
                const FieldDecl* prev = orderedFields[j - 1u];
                const bool keyBeforePrev = (key->resolvedAlign > prev->resolvedAlign)
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
        const usize align = isSerialize ? 1u : field->resolvedAlign;
        if(align > maxAlign)
            maxAlign = align;
        currentOffset = __hidden_adhd_layout_optimizer::AlignUp(currentOffset, align);
        field->resolvedOffset = currentOffset;
        currentOffset += field->resolvedSize;
    }

    outTotalAlign = maxAlign;
    outTotalSize = currentOffset == 0u ? 1u : __hidden_adhd_layout_optimizer::AlignUp(currentOffset, maxAlign);
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

    if(name == "bool" || name == "i8" || name == "u8"){
        outSize = 1u;
        outAlign = 1u;
        return;
    }
    if(name == "i16" || name == "u16"){
        outSize = 2u;
        outAlign = 2u;
        return;
    }
    if(name == "i32" || name == "u32" || name == "f32"){
        outSize = 4u;
        outAlign = 4u;
        return;
    }
    if(name == "i64" || name == "u64" || name == "isize" || name == "usize" || name == "f64"){
        outSize = 8u;
        outAlign = 8u;
        return;
    }

    if(name == "f32x4" || name == "i32x4" || name == "u32x4" || name == "f32x3_i32" || name == "f32x3_u32"){
        outSize = 16u;
        outAlign = 16u;
        return;
    }
    if(name == "f32x3x4"){
        outSize = 48u;
        outAlign = 16u;
        return;
    }
    if(name == "f32x4x4"){
        outSize = 64u;
        outAlign = 16u;
        return;
    }
    if(name == "f64x2" || name == "f64x3" || name == "f64x4" || name == "i64x4" || name == "u64x4" || name == "f64x3_i64" || name == "f64x3_u64"){
        outSize = 32u;
        outAlign = 32u;
        return;
    }
    if(name == "f64x3x3" || name == "f64x3x4"){
        outSize = 96u;
        outAlign = 32u;
        return;
    }
    if(name == "f64x4x4"){
        outSize = 128u;
        outAlign = 32u;
        return;
    }

    if(name == "Name" || name == "AssetRef"){
        outSize = 8u;
        outAlign = 8u;
        return;
    }
    if(name == "StringView" || name == "Span"){
        outSize = 16u;
        outAlign = 8u;
        return;
    }
    if(name == "String" || name == "Path"){
        outSize = 32u;
        outAlign = 8u;
        return;
    }
    if(name == "CompactString"){
        outSize = 256u;
        outAlign = 1u;
        return;
    }

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
        outSize = 24u;
        outAlign = 8u;
        return;
    }

    if(name == "Array"){
        usize elemSize = 4u;
        usize elemAlign = 4u;
        usize count = 1u;
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, elemSize, elemAlign, depth + 1u);
        if(type.templateArgs.size() >= 2u){
            const MStringView countText = type.templateArgs[1].type ? type.templateArgs[1].type->name : type.templateArgs[1].constantText;
            count = ParseUnsignedConstant(countText, m_symbols);
        }
        outSize = elemSize * count;
        outAlign = elemAlign;
        return;
    }

    if(name == "FixedVector"){
        usize elemSize = 4u;
        usize elemAlign = 4u;
        usize count = 1u;
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, elemSize, elemAlign, depth + 1u);
        if(type.templateArgs.size() >= 2u){
            const MStringView countText = type.templateArgs[1].type ? type.templateArgs[1].type->name : type.templateArgs[1].constantText;
            count = ParseUnsignedConstant(countText, m_symbols);
        }
        outAlign = elemAlign > 4u ? elemAlign : 4u;
        outSize = __hidden_adhd_layout_optimizer::AlignUp(elemSize * count + 4u, outAlign);
        return;
    }

    if(SymbolTable::isArenaBackedContainer(name)){
        outSize = 32u;
        outAlign = 8u;
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

bool LayoutOptimizer::computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors){
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

        const MStringView baseFieldTypeName = m_symbols.resolveBaseTypeName(field->type->name);
        if(m_symbols.resolveRefMode(field->type) == ParamPassingMode::None && SymbolTable::isArenaBackedContainer(baseFieldTypeName)){
            field->usesClassArena = true;
            classDecl.needsClassArena = true;
        }
        else if(TypeContainsArenaBackedContainer(*field->type, m_symbols)){
            classDecl.needsClassArena = true;
        }

        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*field->type, fieldSize, fieldAlign);

        if(classDecl.isSerialize && !field->isTransient){
            MStringView serializeErr;
            if(!__hidden_adhd_layout_optimizer::IsTypeBinarySerializable(*field->type, m_symbols, serializeErr)){
                ReportAdhdDiagnostic(m_arena, errors, field->span, serializeErr);
                return false;
            }
            field->resolvedSize = fieldSize;
            field->resolvedAlign = 1u;
        }
        else{
            field->resolvedSize = fieldSize;
            field->resolvedAlign = fieldAlign;
        }

        classDecl.orderedFields.push_back(field);
    }

    sortAndPackFields(
        classDecl.orderedFields,
        classDecl.isSerialize,
        !classDecl.implementedInterfaces.empty(),
        classDecl.totalSize,
        classDecl.totalAlign
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

