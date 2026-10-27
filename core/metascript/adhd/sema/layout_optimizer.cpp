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

[[nodiscard]] usize ParseUnsignedConstant(const MStringView text, const SymbolTable& symbols)noexcept{
    if(text.empty())
        return 1u;

    if(const Stmt* globalConst = symbols.findGlobalConstant(text)){
        if(globalConst->expr && globalConst->expr->kind == ExprKind::IntegerLiteral)
            return ParseUnsignedConstant(globalConst->expr->text, symbols);
    }

    usize val = 0u;
    for(usize i = 0u; i < text.size(); ++i){
        const MChar c = text[i];
        if(c >= '0' && c <= '9')
            val = val * 10u + static_cast<usize>(c - '0');
        else
            break;
    }
    return val > 0u ? val : 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutOptimizer::optimizeTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    bool ok = true;
    for(ClassDecl* classDecl : unit.classes){
        for(const FieldDecl* f : classDecl->fields){
            if(f && f->type)
                collectTemplateInstantiationsInType(*f->type, unit);
        }
        for(const MethodDecl* m : classDecl->methods){
            if(!m)
                continue;
            if(m->returnType)
                collectTemplateInstantiationsInType(*m->returnType, unit);
            for(const ParamDecl& p : m->params){
                if(p.type)
                    collectTemplateInstantiationsInType(*p.type, unit);
            }
            for(const Stmt* s : m->body){
                if(s && s->kind == StmtKind::VarDecl && s->varType)
                    collectTemplateInstantiationsInType(*s->varType, unit);
            }
        }
    }
    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn)
            continue;
        if(fn->returnType)
            collectTemplateInstantiationsInType(*fn->returnType, unit);
        for(const ParamDecl& p : fn->params){
            if(p.type)
                collectTemplateInstantiationsInType(*p.type, unit);
        }
        for(const Stmt* s : fn->body){
            if(s && s->kind == StmtKind::VarDecl && s->varType)
                collectTemplateInstantiationsInType(*s->varType, unit);
        }
    }
    for(ClassDecl* classDecl : unit.classes){
        if(!computeClassLayout(*classDecl, errors))
            ok = false;
    }
    return ok;
}

void LayoutOptimizer::collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit){
    for(const TypeArg& arg : type.templateArgs){
        if(arg.type)
            collectTemplateInstantiationsInType(*arg.type, unit);
    }
    if(const ClassDecl* cls = m_symbols.findClass(type.name)){
        if(!cls->templateParams.empty() && type.templateArgs.size() == cls->templateParams.size())
            instantiateClassTemplate(*cls, type.templateArgs, unit);
    }
}

void LayoutOptimizer::instantiateClassTemplate(
    const ClassDecl& templateDecl,
    const MVector<TypeArg>& templateArgs,
    TranslationUnit& unit
){
    for(const ClassTemplateInstantiation* existing : unit.templateInstantiations){
        if(existing->templateDecl != &templateDecl || existing->templateArgs.size() != templateArgs.size())
            continue;
        bool sameArgs = true;
        for(usize i = 0u; i < templateArgs.size(); ++i){
            const MStringView lhsName = existing->templateArgs[i].type ? existing->templateArgs[i].type->name : existing->templateArgs[i].constantText;
            const MStringView rhsName = templateArgs[i].type ? templateArgs[i].type->name : templateArgs[i].constantText;
            if(lhsName != rhsName){
                sameArgs = false;
                break;
            }
        }
        if(sameArgs)
            return;
    }

    ClassTemplateInstantiation* inst = AllocateAstNode<ClassTemplateInstantiation>(m_arena);
    inst->templateDecl = &templateDecl;
    for(const TypeArg& targ : templateArgs)
        inst->templateArgs.push_back(targ);

    for(const FieldDecl* origField : templateDecl.fields){
        if(!origField || !origField->type)
            continue;

        FieldDecl* clonedField = AllocateAstNode<FieldDecl>(m_arena);
        clonedField->access = origField->access;
        clonedField->name = origField->name;
        clonedField->defaultExpr = origField->defaultExpr;
        clonedField->span = origField->span;
        clonedField->originalIndex = origField->originalIndex;
        clonedField->hasCtorInit = origField->hasCtorInit;
        clonedField->usesClassArena = origField->usesClassArena;
        clonedField->isTransient = origField->isTransient;
        for(Expr* arg : origField->ctorArgs)
            clonedField->ctorArgs.push_back(arg);

        TypeRef* resolvedType = origField->type;
        for(usize p = 0u; p < templateDecl.templateParams.size(); ++p){
            if(origField->type->name == templateDecl.templateParams[p].name && templateArgs[p].type){
                resolvedType = templateArgs[p].type;
                break;
            }
        }
        clonedField->type = resolvedType;

        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*resolvedType, fieldSize, fieldAlign);
        clonedField->resolvedSize = fieldSize;
        clonedField->resolvedAlign = templateDecl.isSerialize ? 1u : fieldAlign;
        inst->orderedFields.push_back(clonedField);
    }

    if(!templateDecl.isSerialize){
        for(usize i = 1u; i < inst->orderedFields.size(); ++i){
            FieldDecl* key = inst->orderedFields[i];
            usize j = i;
            while(j > 0u){
                const FieldDecl* prev = inst->orderedFields[j - 1u];
                const bool keyBeforePrev = (key->resolvedAlign > prev->resolvedAlign)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize > prev->resolvedSize)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize == prev->resolvedSize && key->originalIndex < prev->originalIndex);
                if(!keyBeforePrev)
                    break;
                inst->orderedFields[j] = inst->orderedFields[j - 1u];
                --j;
            }
            inst->orderedFields[j] = key;
        }
    }

    usize currentOffset = templateDecl.implementedInterfaces.empty() ? 0u : 8u;
    usize maxAlign = templateDecl.isSerialize ? 1u : (templateDecl.implementedInterfaces.empty() ? 1u : 8u);
    for(FieldDecl* field : inst->orderedFields){
        const usize align = templateDecl.isSerialize ? 1u : field->resolvedAlign;
        if(align > maxAlign)
            maxAlign = align;
        currentOffset = __hidden_adhd_layout_optimizer::AlignUp(currentOffset, align);
        field->resolvedOffset = currentOffset;
        currentOffset += field->resolvedSize;
    }

    inst->totalAlign = maxAlign;
    inst->totalSize = currentOffset == 0u ? 1u : __hidden_adhd_layout_optimizer::AlignUp(currentOffset, maxAlign);
    unit.templateInstantiations.push_back(inst);
}

bool LayoutOptimizer::resolveTypeSizeAndAlign(const TypeRef& type, usize& outSize, usize& outAlign)const noexcept{
    if(type.refMode != ParamPassingMode::None){
        outSize = 8u;
        outAlign = 8u;
        return true;
    }

    if(type.isFuncType){
        outSize = (type.inferredCaptureBytes == 0u) ? 8u : (type.inferredCaptureBytes + 8u);
        outAlign = 8u;
        return true;
    }

    const MStringView name = type.name;

    if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(name)){
        if(aliasDecl->aliasedType)
            return resolveTypeSizeAndAlign(*aliasDecl->aliasedType, outSize, outAlign);
    }

    if(name == "bool" || name == "i8" || name == "u8"){
        outSize = 1u;
        outAlign = 1u;
        return true;
    }
    if(name == "i16" || name == "u16"){
        outSize = 2u;
        outAlign = 2u;
        return true;
    }
    if(name == "i32" || name == "u32" || name == "f32"){
        outSize = 4u;
        outAlign = 4u;
        return true;
    }
    if(name == "i64" || name == "u64" || name == "isize" || name == "usize" || name == "f64"){
        outSize = 8u;
        outAlign = 8u;
        return true;
    }

    if(name == "f32x4" || name == "i32x4" || name == "u32x4" || name == "f32x3_i32" || name == "f32x3_u32" || name == "f64x2"){
        outSize = 16u;
        outAlign = 16u;
        return true;
    }
    if(name == "f32x3x4"){
        outSize = 48u;
        outAlign = 16u;
        return true;
    }
    if(name == "f32x4x4"){
        outSize = 64u;
        outAlign = 16u;
        return true;
    }
    if(name == "f64x3" || name == "f64x4" || name == "i64x4" || name == "u64x4" || name == "f64x3_i64" || name == "f64x3_u64"){
        outSize = 32u;
        outAlign = 32u;
        return true;
    }
    if(name == "f64x3x3" || name == "f64x3x4"){
        outSize = 96u;
        outAlign = 32u;
        return true;
    }
    if(name == "f64x4x4"){
        outSize = 128u;
        outAlign = 32u;
        return true;
    }

    if(name == "Name" || name == "AssetRef"){
        outSize = 8u;
        outAlign = 8u;
        return true;
    }
    if(name == "StringView" || name == "Span"){
        outSize = 16u;
        outAlign = 8u;
        return true;
    }
    if(name == "String" || name == "Path"){
        outSize = 32u;
        outAlign = 8u;
        return true;
    }
    if(name == "CompactString"){
        outSize = 64u;
        outAlign = 8u;
        return true;
    }

    if(name == "Atomic"){
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            return resolveTypeSizeAndAlign(*type.templateArgs[0].type, outSize, outAlign);
        outSize = 8u;
        outAlign = 8u;
        return true;
    }

    if(name == "Optional"){
        if(!type.templateArgs.empty() && type.templateArgs[0].type){
            usize innerSize = 8u;
            usize innerAlign = 8u;
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, innerSize, innerAlign);
            outAlign = innerAlign;
            outSize = __hidden_adhd_layout_optimizer::AlignUp(innerSize + 1u, innerAlign);
            return true;
        }
        outSize = 16u;
        outAlign = 8u;
        return true;
    }

    if(name == "Expected"){
        outSize = 24u;
        outAlign = 8u;
        return true;
    }

    if(name == "Array"){
        usize elemSize = 4u;
        usize elemAlign = 4u;
        usize count = 1u;
        if(!type.templateArgs.empty() && type.templateArgs[0].type)
            resolveTypeSizeAndAlign(*type.templateArgs[0].type, elemSize, elemAlign);
        if(type.templateArgs.size() >= 2u)
            count = __hidden_adhd_layout_optimizer::ParseUnsignedConstant(type.templateArgs[1].constantText, m_symbols);
        outSize = elemSize * count;
        outAlign = elemAlign;
        return true;
    }

    if(SymbolTable::isArenaBackedContainer(name)){
        outSize = 32u;
        outAlign = 8u;
        return true;
    }

    if(const EnumDecl* enumDecl = m_symbols.findEnum(name)){
        outSize = enumDecl->underlyingSize;
        outAlign = enumDecl->underlyingAlign;
        return true;
    }

    if(const ClassDecl* classDecl = m_symbols.findClass(name)){
        outSize = classDecl->totalSize;
        outAlign = classDecl->totalAlign;
        return true;
    }

    outSize = 8u;
    outAlign = 8u;
    return true;
}

bool LayoutOptimizer::computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors){
    if(classDecl.isSerialize && !classDecl.implementedInterfaces.empty()){
        reportError(errors, classDecl.span, "'[serialize]' class cannot implement an interface (virtual table pointers cannot be binary serialized)");
        return false;
    }

    classDecl.orderedFields.clear();
    classDecl.orderedFields.reserve(classDecl.fields.size());

    for(FieldDecl* field : classDecl.fields){
        if(!field->type)
            continue;

        if(SymbolTable::isArenaBackedContainer(field->type->name)){
            field->usesClassArena = true;
            classDecl.needsClassArena = true;
        }

        usize fieldSize = 8u;
        usize fieldAlign = 8u;
        resolveTypeSizeAndAlign(*field->type, fieldSize, fieldAlign);

        if(classDecl.isSerialize && !field->isTransient){
            if(field->type->refMode != ParamPassingMode::None){
                reportError(errors, field->span, "'[serialize]' class cannot contain non-transient 'in'/'mut' reference members");
                return false;
            }
            if(SymbolTable::isArenaBackedContainer(field->type->name) || field->type->name == "String" || field->type->name == "StringView" || field->type->name == "Span"){
                reportError(errors, field->span, "'[serialize]' class cannot contain non-transient dynamic heap or view members; mark with '[transient]' or use fixed-size types");
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

    if(!classDecl.isSerialize){
        for(usize i = 1u; i < classDecl.orderedFields.size(); ++i){
            FieldDecl* key = classDecl.orderedFields[i];
            usize j = i;
            while(j > 0u){
                const FieldDecl* prev = classDecl.orderedFields[j - 1u];
                const bool keyBeforePrev = (key->resolvedAlign > prev->resolvedAlign)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize > prev->resolvedSize)
                    || (key->resolvedAlign == prev->resolvedAlign && key->resolvedSize == prev->resolvedSize && key->originalIndex < prev->originalIndex);
                if(!keyBeforePrev)
                    break;
                classDecl.orderedFields[j] = classDecl.orderedFields[j - 1u];
                --j;
            }
            classDecl.orderedFields[j] = key;
        }
    }

    usize currentOffset = 0u;
    usize maxAlign = classDecl.isSerialize ? 1u : (classDecl.implementedInterfaces.empty() ? 1u : 8u);

    if(!classDecl.implementedInterfaces.empty())
        currentOffset = 8u;

    for(FieldDecl* field : classDecl.orderedFields){
        const usize align = classDecl.isSerialize ? 1u : field->resolvedAlign;
        if(align > maxAlign)
            maxAlign = align;
        currentOffset = __hidden_adhd_layout_optimizer::AlignUp(currentOffset, align);
        field->resolvedOffset = currentOffset;
        currentOffset += field->resolvedSize;
    }

    classDecl.totalAlign = maxAlign;
    classDecl.totalSize = currentOffset == 0u ? 1u : __hidden_adhd_layout_optimizer::AlignUp(currentOffset, maxAlign);
    return true;
}

void LayoutOptimizer::reportError(MVector<ParseError>& errors, const SourceSpan span, const MStringView message)const{
    errors.push_back(ParseError{span.line, span.column, MString(message.data(), message.size(), m_arena)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

