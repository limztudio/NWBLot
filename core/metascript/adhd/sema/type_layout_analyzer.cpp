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

namespace __hidden_adhd_type_layout_analyzer{

[[nodiscard]] b8 IsOrderIndependentBuiltinGeneric(const BuiltinGenericTypeInfo& generic, const MStringView name)noexcept{
    if(generic.isBinarySerializable || (generic.isBorrowView && generic.isSmallValue))
        return true;
    if(generic.inlineLayoutKind != InlineLayoutKind::None || name == "CompressedPair")
        return true;
    return generic.isArenaBacked && generic.isCopyMoveConstructible;
}

[[nodiscard]] b8 HasAnyUserConstructor(const ClassDecl& cls)noexcept{
    for(const MethodDecl* method : cls.methods){
        if(method && method->isConstructor)
            return true;
    }
    return false;
}

[[nodiscard]] b8 ExprHasConstructorThisDependency(const Expr* const expr)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::ThisMember)
        return true;
    if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& capture : expr->lambdaCaptures){
            if(capture.name == "this")
                return true;
        }
    }
    if(ExprHasConstructorThisDependency(expr->lhs) || ExprHasConstructorThisDependency(expr->rhs))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprHasConstructorThisDependency(arg))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtsHaveConstructorSiblingReference(const Span<const Stmt* const> stmts)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(IsThisMemberSimpleAssignStmt(*stmt)){
            if(ExprHasConstructorThisDependency(stmt->expr->rhs))
                return true;
            continue;
        }
        if(ExprHasConstructorThisDependency(stmt->expr)
            || ExprHasConstructorThisDependency(stmt->condition)
            || ExprHasConstructorThisDependency(stmt->stepExpr))
            return true;
        if(stmt->initStmt){
            const Stmt* const initPtr = stmt->initStmt;
            if(StmtsHaveConstructorSiblingReference(Span<const Stmt* const>(&initPtr, 1u)))
                return true;
        }
        for(const Expr* arg : stmt->arguments){
            if(ExprHasConstructorThisDependency(arg))
                return true;
        }
        if(StmtsHaveConstructorSiblingReference(stmt->statements)
            || StmtsHaveConstructorSiblingReference(stmt->elseStatements))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(ExprHasConstructorThisDependency(sc.matchExpr)
                || StmtsHaveConstructorSiblingReference(sc.statements))
                return true;
        }
    }
    return false;
}

[[nodiscard]] b8 IsOrderIndependentScalarOrStorageBuiltin(const MStringView name)noexcept{
    return BuiltinTypeSemantics::IsScalarType(name)
        || FindBuiltinSimdVectorType(name) != nullptr
        || FindBuiltinSimdMatrixType(name) != nullptr
        || FindBuiltinUnalignedVectorType(name) != nullptr
        || FindBuiltinEnum(name) != nullptr;
}

};

b8 TypeLayoutAnalyzer::areFieldDeclsAndInitsOrderIndependent(const Span<const FieldDecl* const> fields, const ClassDecl* const substOwner, const TypeRef* const instance, const Span<const TypeArg> templateArgs, const u32 depth)const noexcept{
    const u32 childDepth = depth + 1u;
    for(const FieldDecl* field : fields){
        if(!field || field->isStatic || !field->type)
            continue;
        const TypeRef* subType = field->type;
        if(substOwner && !substOwner->templateParams.empty()){
            subType = instance
                ? m_symbols.substituteTypeParams(field->type, substOwner->templateParams, instance)
                : m_symbols.substituteTypeParams(*field->type, substOwner->templateParams, templateArgs);
        }
        if(!subType || !isOrderIndependentFieldType(*subType, childDepth))
            return false;
        if(field->defaultExpr && !isOrderIndependentInitExpr(field->defaultExpr, childDepth))
            return false;
        if(field->hasCtorInit){
            const TypeRef* unwrappedFieldType = m_symbols.unwrapTypeAlias(subType);
            if(unwrappedFieldType){
                if(const ClassDecl* fieldCls = m_symbols.findClass(unwrappedFieldType->name); fieldCls && !fieldCls->isExtern && __hidden_adhd_type_layout_analyzer::HasAnyUserConstructor(*fieldCls))
                    return false;
            }
            for(const Expr* arg : field->ctorArgs){
                if(!isOrderIndependentInitExpr(arg, childDepth))
                    return false;
            }
        }
    }
    return true;
}

b8 TypeLayoutAnalyzer::areClassFieldsAndInitsOrderIndependent(const ClassDecl& cls, const TypeRef* const instance, const Span<const TypeArg> templateArgs, const u32 depth)const noexcept{
    if(cls.isExtern){
        return !cls.hasExplicitDestructor
            && cls.isBinarySerializable
            && cls.isCopyConstructible
            && !cls.isAbstract
            && !cls.isPolymorphicBase;
    }
    if(cls.isExtensible
        || cls.concreteBase
        || !cls.assetTypeName.empty()
        || !cls.systemTypeName.empty()
        || !cls.subsystemTypeName.empty()
        || m_symbols.hasPolymorphicInterface(cls))
        return false;
    for(const TypeRef* iface : cls.implementedInterfaces){
        if(iface && interfaceInheritsNoCopy(*iface))
            return false;
    }
    for(const MethodDecl* method : cls.methods){
        if(!method)
            continue;
        if(method->isDestructor || (method->isConstructor && method->minArgCount == 0u))
            return false;
    }
    if(!cls.templateParams.empty() && !instance && templateArgs.empty())
        return false;
    return areFieldDeclsAndInitsOrderIndependent(Span<const FieldDecl* const>(cls.fields.data(), cls.fields.size()), &cls, instance, templateArgs, depth);
}

b8 TypeLayoutAnalyzer::isOrderIndependentFieldType(const TypeRef& type, const u32 depth)const noexcept{
    if(depth >= 16u)
        return false;
    if(m_symbols.resolveRefMode(&type) != ParamPassingMode::None)
        return true;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&type);
    if(!unwrapped)
        return false;
    if(unwrapped->refMode != ParamPassingMode::None)
        return true;
    if(unwrapped->isFuncType)
        return false;

    const MStringView name = unwrapped->name;
    if(m_symbols.findEnum(name))
        return true;
    if(const ClassDecl* cls = m_symbols.findClass(name))
        return areClassFieldsAndInitsOrderIndependent(*cls, unwrapped, {}, depth);

    if(!m_symbols.hasTypeSymbol(name)){
        if(__hidden_adhd_type_layout_analyzer::IsOrderIndependentScalarOrStorageBuiltin(name))
            return true;
        if(const BuiltinObjectTypeInfo* obj = FindBuiltinObjectType(name)){
            return obj->isCopyMoveConstructible
                && !obj->isAbstract
                && !obj->isPolymorphicBase
                && (obj->isBinarySerializable || obj->isSmallValue || obj->isArenaBacked);
        }
        if(const BuiltinGenericTypeInfo* generic = FindBuiltinGenericType(name)){
            if(!__hidden_adhd_type_layout_analyzer::IsOrderIndependentBuiltinGeneric(*generic, name))
                return false;
            for(const TypeArg& targ : unwrapped->templateArgs){
                if(targ.type && !isOrderIndependentFieldType(*targ.type, depth + 1u))
                    return false;
            }
            return true;
        }
    }
    return false;
}

b8 TypeLayoutAnalyzer::isOrderIndependentCallInitExpr(const Expr& expr, const u32 depth)const noexcept{
    if(depth >= 32u || !expr.lhs || expr.lhs->kind != ExprKind::Identifier)
        return false;
    const MStringView callee = expr.lhs->text;
    if(m_symbols.findFreeFunction(callee) != nullptr || FindBuiltinFunction(callee) != nullptr)
        return false;

    const Span<const TypeArg> typeArgs = !expr.callTemplateArgs.empty()
        ? Span<const TypeArg>(expr.callTemplateArgs.data(), expr.callTemplateArgs.size())
        : Span<const TypeArg>(expr.deducedTemplateArgs.data(), expr.deducedTemplateArgs.size());

    b8 validTypeCtor = false;
    if(m_symbols.findEnum(callee) || __hidden_adhd_type_layout_analyzer::IsOrderIndependentScalarOrStorageBuiltin(callee)){
        validTypeCtor = typeArgs.empty();
    }
    else if(const ClassDecl* cls = m_symbols.findClass(callee)){
        validTypeCtor = (cls->isExtern || !__hidden_adhd_type_layout_analyzer::HasAnyUserConstructor(*cls))
            && areClassFieldsAndInitsOrderIndependent(*cls, nullptr, typeArgs, depth + 1u);
    }
    else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(callee)){
        if(aliasDecl->aliasedType && aliasDecl->templateParams.empty()){
            const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(aliasDecl->aliasedType);
            if(unwrapped && isOrderIndependentFieldType(*unwrapped, depth + 1u)){
                const ClassDecl* aliasedCls = m_symbols.findClass(unwrapped->name);
                validTypeCtor = !aliasedCls || aliasedCls->isExtern || !__hidden_adhd_type_layout_analyzer::HasAnyUserConstructor(*aliasedCls);
            }
        }
    }
    else if(!m_symbols.hasTypeSymbol(callee)){
        if(const BuiltinObjectTypeInfo* obj = FindBuiltinObjectType(callee)){
            validTypeCtor = typeArgs.empty()
                && obj->isCopyMoveConstructible
                && !obj->isAbstract
                && !obj->isPolymorphicBase
                && (obj->isBinarySerializable || obj->isSmallValue || obj->isArenaBacked);
        }
        else if(const BuiltinGenericTypeInfo* generic = FindBuiltinGenericType(callee)){
            validTypeCtor = __hidden_adhd_type_layout_analyzer::IsOrderIndependentBuiltinGeneric(*generic, callee);
            for(const TypeArg& targ : typeArgs){
                if(targ.type && !isOrderIndependentFieldType(*targ.type, depth + 1u)){
                    validTypeCtor = false;
                    break;
                }
            }
        }
    }

    if(!validTypeCtor)
        return false;
    for(const Expr* arg : expr.arguments){
        if(!isOrderIndependentInitExpr(arg, depth + 1u))
            return false;
    }
    return true;
}

b8 TypeLayoutAnalyzer::isOrderIndependentInitExpr(const Expr* const expr, const u32 depth)const noexcept{
    if(!expr)
        return true;
    if(depth >= 32u)
        return false;
    if(IsLiteralExprKind(expr->kind) || expr->kind == ExprKind::ScopeAccess || expr->kind == ExprKind::LayoutQuery)
        return true;
    switch(expr->kind){
    case ExprKind::Identifier:
        return expr->isBuiltinConstant || m_symbols.findGlobalConstant(expr->text) != nullptr;
    case ExprKind::Unary:
        return !OperatorSemantics::IsIncrementOrDecrementOperatorToken(expr->op)
            && isOrderIndependentInitExpr(expr->lhs, depth + 1u);
    case ExprKind::Binary:{
        const OperatorTokenInfo* opInfo = OperatorSemantics::FindOperatorToken(expr->op);
        return opInfo && !opInfo->isAssignment
            && isOrderIndependentInitExpr(expr->lhs, depth + 1u)
            && isOrderIndependentInitExpr(expr->rhs, depth + 1u);
    }
    case ExprKind::Cast:
    case ExprKind::BitCast:
    case ExprKind::MemberAccess:
        return isOrderIndependentInitExpr(expr->lhs, depth + 1u);
    case ExprKind::Call:
        return isOrderIndependentCallInitExpr(*expr, depth);
    default:
        return false;
    }
}

b8 TypeLayoutAnalyzer::requiresDeclarationFieldOrder(const ClassDecl& decl, const Span<const FieldDecl* const> fields)const noexcept{
    if(decl.isSerialize
        || decl.recordAlignment != 0u
        || decl.isExtensible
        || decl.concreteBase != nullptr
        || !decl.assetTypeName.empty()
        || !decl.systemTypeName.empty()
        || !decl.subsystemTypeName.empty()
        || m_symbols.hasPolymorphicInterface(decl))
        return true;

    for(const TypeRef* iface : decl.implementedInterfaces){
        if(iface && interfaceInheritsNoCopy(*iface))
            return true;
    }

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        if(method->isDestructor)
            return true;
        if(method->isConstructor && (!method->hasBody || __hidden_adhd_type_layout_analyzer::StmtsHaveConstructorSiblingReference(method->body)))
            return true;
    }

    return !areFieldDeclsAndInitsOrderIndependent(fields, nullptr, nullptr, {}, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

