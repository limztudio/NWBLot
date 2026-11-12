// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"

#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include <core/metascript/adhd/ast/operator_semantics.h>

#include "binary_expression_checker.h"
#include "lambda_expression_checker.h"

#include "borrow_lifetime_analyzer.h"
#include "concrete_type_analyzer.h"
#include "lambda_capture_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeChecker::checkExpr(ClassDecl* ownerClass, const Span<const ScopedVarInfo> scope, const b8 isMutationTarget, MethodDecl& method, Expr& expr, MVector<ParseError>& errors){
    b8 ok = true;

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::StringLiteral:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot pass literal or temporary to a 'mut' target");
            ok = false;
        }
        break;
    }
    case ExprKind::Identifier:{
        const ScopedVarInfo* local = FindScopedVar(scope, expr.text);
        const BuiltinGlobalConstantInfo* builtinConst = (!local && !m_symbols.findGlobalConstant(expr.text) && !HasConstValueTemplateParam(ownerClass, &method, expr.text))
            ? FindBuiltinGlobalConstant(expr.text)
            : nullptr;
        expr.isBuiltinConstant = (builtinConst != nullptr);
        if(builtinConst){
            recordRequiredHeader(builtinConst->requiredHeader);
            recordBuiltinTypeHeader(builtinConst->adhdTypeName);
        }
        if(local){
            const ParamPassingMode::Enum resolvedLocalRefMode = m_symbols.resolveRefMode(local->type);
            if((!local->isParam && local->mode != ParamPassingMode::None) || resolvedLocalRefMode != ParamPassingMode::None)
                expr.derefNotNull = true;

            if(isMutationTarget){
                if(local->mode == ParamPassingMode::In || (local->isParam && resolvedLocalRefMode == ParamPassingMode::In)){
                    reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                    ok = false;
                }
                else if(local->isConst){
                    reportError(errors, expr.span, "cannot mutate 'const' variable");
                    ok = false;
                }
                else if(m_parallelDepth > 0u && local->declaredParallelDepth < m_parallelDepth){
                    if(IsNonThreadSafeContainer(resolveScopedVarBaseType(*local))){
                        reportError(errors, expr.span, "shared 'mut' access to non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                        ok = false;
                    }
                }
            }
        }
        else{
            b8 reportedUnqualifiedMember = false;
            if(ownerClass && (SymbolTable::FindClassField(*ownerClass, expr.text) || m_symbols.findClassMethod(*ownerClass, expr.text))){
                reportError(errors, expr.span, "class member must be accessed explicitly via 'this.member' in adhd");
                ok = false;
                reportedUnqualifiedMember = true;
            }
            if(!reportedUnqualifiedMember && isMutationTarget){
                if(m_symbols.findGlobalConstant(expr.text) || HasConstValueTemplateParam(ownerClass, &method, expr.text))
                    reportError(errors, expr.span, "cannot mutate 'const' variable");
                else
                    reportError(errors, expr.span, "cannot mutate non-variable identifier");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::ThisMember:{
        if(!validateThisAccess(ownerClass, expr.span, scope, errors)){
            ok = false;
            if(!ownerClass)
                break;
        }
        const b8 isConstThis = isThisConstInScope(method, scope);
        if(expr.text.empty()){
            if(isMutationTarget && isConstThis){
                reportError(errors, expr.span, "cannot mutate 'this' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
            break;
        }
        const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, expr.text);
        const MethodDecl* memberMethod = m_symbols.findClassMethod(*ownerClass, expr.text);
        b8 isInheritedBuiltinMethod = ((!ownerClass->assetTypeName.empty() || !ownerClass->subsystemTypeName.empty())
            && FindBuiltinReceiverMethod("IAsset", expr.text) != nullptr)
            || (!ownerClass->systemTypeName.empty() && FindBuiltinReceiverMethod("ISystem", expr.text) != nullptr);
        if(!isInheritedBuiltinMethod){
            for(const TypeRef* iface : ownerClass->implementedInterfaces){
                if(!iface)
                    continue;
                const MStringView ifaceName = m_symbols.resolveBaseTypeName(iface->name);
                if(!m_symbols.findInterface(ifaceName) && FindBuiltinReceiverMethod(ifaceName, expr.text) != nullptr){
                    isInheritedBuiltinMethod = true;
                    break;
                }
            }
        }
        if(!field && !memberMethod && !isInheritedBuiltinMethod){
            reportError(errors, expr.span, "unknown member on 'this'");
            ok = false;
            break;
        }
        const ParamPassingMode::Enum fieldRefMode = field ? m_symbols.resolveRefMode(field->type) : ParamPassingMode::None;
        if(fieldRefMode != ParamPassingMode::None)
            expr.derefNotNull = true;

        if(isMutationTarget){
            if(isConstThis && fieldRefMode != ParamPassingMode::Mut){
                reportError(errors, expr.span, "cannot mutate 'this.member' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
            else if(fieldRefMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' reference member");
                ok = false;
            }
            else if(field && field->type && IsNonThreadSafeContainer(m_symbols.resolveBaseTypeName(field->type->name))){
                if(m_inLambda)
                    m_activeLambdaMutatesContainer = true;
                if(m_parallelDepth > 0u){
                    reportError(errors, expr.span, "shared 'mut' access to non-thread-safe member container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                    ok = false;
                }
            }
        }
        break;
    }
    case ExprKind::ScopeAccess:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot mutate or pass scope-qualified constant/enumerator to a 'mut' target");
            ok = false;
        }
        if(!expr.scopeChain.empty()){
            if(expr.text == "this"){
                if(!validateThisAccess(ownerClass, expr.span, scope, errors))
                    ok = false;
            }
            else if(expr.lhs && !checkExpr(ownerClass, scope, false, method, *expr.lhs, errors)){
                ok = false;
            }
            const b8 hadVarOrMemberLhs = (expr.text == "this") || (expr.lhs != nullptr);
            const ClassDecl* accessedCls = nullptr;
            const FieldDecl* accessedField = nullptr;
            const MStringView resolvedEnumName = resolveScopeAccessEnumTypeName(ownerClass, &method, expr, scope, &accessedCls, &accessedField);
            if(accessedCls && accessedField && ownerClass != accessedCls && accessedField->access != AccessSpecifier::Public){
                reportError(errors, expr.span, "cannot access 'private' or 'protected' field outside of its declaring class");
                ok = false;
            }
            if(!resolvedEnumName.empty())
                expr.scopeChain[0] = resolvedEnumName;
            expr.text = {};
            expr.lhs = nullptr;

            if(const EnumDecl* enumDecl = m_symbols.findEnum(expr.scopeChain[0])){
                if(expr.scopeChain.size() != 2u){
                    reportError(errors, expr.span, "invalid enum/flag scope access; expected 'TypeName::Entry'");
                    ok = false;
                }
                else{
                    const MStringView entryName = expr.scopeChain[1];
                    const EnumEntry* matchedEntry = nullptr;
                    for(const EnumEntry& entry : enumDecl->entries){
                        if(entry.name == entryName){
                            matchedEntry = &entry;
                            break;
                        }
                    }
                    if(!matchedEntry){
                        reportError(errors, expr.span, "unknown enumerator in enum/flag scope access");
                        ok = false;
                    }
                }
            }
            else if(!m_symbols.hasTypeSymbol(expr.scopeChain[0]) && FindBuiltinEnum(expr.scopeChain[0]) != nullptr){
                const BuiltinEnumInfo* builtinEnum = FindBuiltinEnum(expr.scopeChain[0]);
                recordRequiredHeader(builtinEnum->requiredHeader);
                if(expr.scopeChain.size() != 2u){
                    reportError(errors, expr.span, "invalid enum/flag scope access; expected 'TypeName::Entry'");
                    ok = false;
                }
                else if(!HasBuiltinEnumEntry(builtinEnum->adhdName, expr.scopeChain[1])){
                    if(const BuiltinGlobalConstantInfo* scopedConst = FindBuiltinScopedConstant(builtinEnum->adhdName, expr.scopeChain[1])){
                        expr.isBuiltinConstant = true;
                        recordRequiredHeader(scopedConst->requiredHeader);
                        recordBuiltinTypeHeader(scopedConst->adhdTypeName);
                    }
                    else if(const BuiltinFunctionInfo* scopedFn = FindBuiltinScopedFunction(builtinEnum->adhdName, expr.scopeChain[1])){
                        recordRequiredHeader(scopedFn->requiredHeader);
                    }
                    else{
                        reportError(errors, expr.span, "unknown enumerator in enum/flag scope access");
                        ok = false;
                    }
                }
            }
            else if(
                !hadVarOrMemberLhs
                && expr.scopeChain.size() == 2u
                && FindScopedVar(scope, expr.scopeChain[0]) == nullptr
                && m_symbols.findGlobalConstant(expr.scopeChain[0]) == nullptr
                && !m_symbols.hasTypeSymbol(expr.scopeChain[0])
                && FindBuiltinScopedConstant(expr.scopeChain[0], expr.scopeChain[1]) != nullptr
            ){
                const BuiltinGlobalConstantInfo* scopedConst = FindBuiltinScopedConstant(expr.scopeChain[0], expr.scopeChain[1]);
                expr.isBuiltinConstant = true;
                recordBuiltinTypeHeader(expr.scopeChain[0]);
                recordRequiredHeader(scopedConst->requiredHeader);
                recordBuiltinTypeHeader(scopedConst->adhdTypeName);
            }
            else if(
                !hadVarOrMemberLhs
                && expr.scopeChain.size() == 2u
                && FindScopedVar(scope, expr.scopeChain[0]) == nullptr
                && m_symbols.findGlobalConstant(expr.scopeChain[0]) == nullptr
                && !m_symbols.hasTypeSymbol(expr.scopeChain[0])
                && FindBuiltinScopedFunction(expr.scopeChain[0], expr.scopeChain[1]) != nullptr
            ){
                const BuiltinFunctionInfo* scopedFn = FindBuiltinScopedFunction(expr.scopeChain[0], expr.scopeChain[1]);
                recordBuiltinTypeHeader(expr.scopeChain[0]);
                recordRequiredHeader(scopedFn->requiredHeader);
            }
            else if(
                const ClassDecl* scopedCls = (!hadVarOrMemberLhs && expr.scopeChain.size() == 2u)
                    ? m_symbols.findClass(m_symbols.resolveBaseTypeName(expr.scopeChain[0]))
                    : nullptr;
                scopedCls && (
                    (!scopedCls->assetTypeName.empty() && (expr.scopeChain[1] == "AssetTypeName" || expr.scopeChain[1] == "s_AssetTypeName" || expr.scopeChain[1] == "s_AssetTypeText"))
                    || (!scopedCls->systemTypeName.empty() && (expr.scopeChain[1] == "SystemTypeName" || expr.scopeChain[1] == "s_SystemTypeName" || expr.scopeChain[1] == "s_SystemTypeText"))
                    || (!scopedCls->componentTypeName.empty() && (expr.scopeChain[1] == "ComponentTypeName" || expr.scopeChain[1] == "s_ComponentTypeName" || expr.scopeChain[1] == "s_ComponentTypeText"))
                )
            ){
            }
            else if(
                const InterfaceDecl* scopedIface = (!hadVarOrMemberLhs && expr.scopeChain.size() == 2u)
                    ? m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.scopeChain[0]))
                    : nullptr;
                scopedIface && !scopedIface->assetTypeName.empty() && (expr.scopeChain[1] == "AssetTypeName" || expr.scopeChain[1] == "s_AssetTypeName" || expr.scopeChain[1] == "s_AssetTypeText")
            ){
            }
            else if(
                !m_hasTopLevelNativeBlocks
                || hadVarOrMemberLhs
                || expr.scopeChain.size() != 2u
                || FindScopedVar(scope, expr.scopeChain[0]) != nullptr
                || m_symbols.findGlobalConstant(expr.scopeChain[0]) != nullptr
                || m_symbols.findClass(expr.scopeChain[0]) != nullptr
                || m_symbols.findInterface(expr.scopeChain[0]) != nullptr
                || BuiltinTypeSemantics::IsScalarType(expr.scopeChain[0])
                || IsStringLikeType(expr.scopeChain[0])
                || FindBuiltinGenericType(expr.scopeChain[0]) != nullptr
                || SymbolTable::IsSimdType(expr.scopeChain[0])
                || SymbolTable::IsUnalignedVectorType(expr.scopeChain[0])
            ){
                reportError(errors, expr.span, "invalid scope resolution '::'; only 'enum' and 'flag' types and variables support '::' access in adhd");
                ok = false;
            }
            else if(FindBuiltinObjectType(expr.scopeChain[0]) != nullptr){
                recordBuiltinTypeHeader(expr.scopeChain[0]);
            }
        }
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier && !FindScopedVar(scope, expr.lhs->text) && !m_symbols.findGlobalConstant(expr.lhs->text)){
            const MStringView resolvedBaseType = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(BuiltinTypeSemantics::IsNumericScalarType(resolvedBaseType) && expr.lhs->callTemplateArgs.empty() && BuiltinTypeSemantics::TryMapScalarLimitMember(expr.text)){
                if(isMutationTarget){
                    reportError(errors, expr.span, "cannot mutate a scalar type limit constant");
                    ok = false;
                }
                break;
            }
            if(m_symbols.hasTypeSymbol(resolvedBaseType) || BuiltinTypeSemantics::IsBuiltinTypeName(resolvedBaseType)){
                reportError(errors, expr.span, "cannot use '.' member access on a type name; use 'TypeName::Entry' for enum/flag values or an instance variable");
                ok = false;
                break;
            }
        }
        if(expr.lhs && !checkExpr(ownerClass, scope, isMutationTarget, method, *expr.lhs, errors))
            ok = false;

        if(expr.lhs){
            if(expr.lhs->derefNotNull){
                expr.lhs->derefNotNull = false;
                expr.isPointerMemberAccess = true;
            }
            const TypeRef* lhsTypeRef = nullptr;
            const MStringView baseType = inferExprBaseType(ownerClass, &method, *expr.lhs, scope, &lhsTypeRef);
            if(const BuiltinSimdVectorInfo* simdInfo = FindBuiltinSimdVectorType(baseType)){
                b8 isSwizzlePattern = false;
                b8 validLanes = true;
                if(CheckSwizzlePattern(expr.text, simdInfo->lanes, isSwizzlePattern, validLanes) && isSwizzlePattern){
                    if(!validLanes){
                        reportError(errors, expr.span, "invalid SIMD swizzle component for vector width or mixed xyzw/rgba set");
                        ok = false;
                    }
                    else if(expr.text.size() > 1u && simdInfo->isMixed){
                        reportError(errors, expr.span, "multi-lane SIMD swizzling is not supported on mixed float/integer vector types");
                        ok = false;
                    }
                    else if(expr.text.size() > 1u && !SymbolTable::IsSimdRegisterVectorType(baseType)){
                        reportError(errors, expr.span, "multi-lane SIMD swizzling requires 'VectorSingle' or 'VectorDouble'; memory-layout vector types only support single-lane access");
                        ok = false;
                    }
                    else if(expr.text.size() > 1u){
                        expr.isSwizzle = true;
                    }
                    else if(expr.isMutArg && SymbolTable::IsSimdRegisterVectorType(baseType)){
                        reportError(errors, expr.span, "cannot pass a lane of 'VectorSingle' or 'VectorDouble' to a 'mut' parameter");
                        ok = false;
                    }
                }
            }
            else if(u32 unalignedLanes = 0u; SymbolTable::IsUnalignedVectorType(baseType, unalignedLanes)){
                b8 isSwizzlePattern = false;
                b8 validLanes = true;
                if(!CheckSwizzlePattern(expr.text, unalignedLanes, isSwizzlePattern, validLanes) || !isSwizzlePattern || !validLanes || expr.text.size() != 1u){
                    reportError(errors, expr.span, "invalid component access on unaligned vector type; only single-lane .x/.y/.z/.w or .r/.g/.b/.a within vector width is supported");
                    ok = false;
                }
            }
            else if(const ClassDecl* targetCls = m_symbols.findClass(baseType)){
                const FieldDecl* field = SymbolTable::FindClassField(*targetCls, expr.text);
                const MethodDecl* memberMethod = m_symbols.findClassMethod(*targetCls, expr.text);
                b8 isInheritedBuiltinMethod = ((!targetCls->assetTypeName.empty() || !targetCls->subsystemTypeName.empty())
                    && FindBuiltinReceiverMethod("IAsset", expr.text) != nullptr)
                    || (!targetCls->systemTypeName.empty() && FindBuiltinReceiverMethod("ISystem", expr.text) != nullptr);
                if(!isInheritedBuiltinMethod){
                    for(const TypeRef* iface : targetCls->implementedInterfaces){
                        if(!iface)
                            continue;
                        const MStringView ifaceName = m_symbols.resolveBaseTypeName(iface->name);
                        if(!m_symbols.findInterface(ifaceName) && FindBuiltinReceiverMethod(ifaceName, expr.text) != nullptr){
                            isInheritedBuiltinMethod = true;
                            break;
                        }
                    }
                }
                if(!field && !memberMethod && !isInheritedBuiltinMethod){
                    reportError(errors, expr.span, "unknown member on class");
                    ok = false;
                }
                else if(field && !memberMethod){
                    if(ownerClass != targetCls && field->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot access 'private' or 'protected' field outside of its declaring class");
                        ok = false;
                    }
                    const TypeRef* ft = m_symbols.substituteTypeParams(field->type, targetCls->templateParams, lhsTypeRef);
                    const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(ft);
                    if(fieldRefMode != ParamPassingMode::None)
                        expr.derefNotNull = true;
                    if(isMutationTarget && fieldRefMode == ParamPassingMode::In){
                        reportError(errors, expr.span, "cannot mutate 'in' reference member");
                        ok = false;
                    }
                    else if(isMutationTarget && m_parallelDepth > 0u && ft && IsNonThreadSafeContainer(m_symbols.resolveBaseTypeName(ft->name))){
                        b8 isTaskLocalOwner = false;
                        if(expr.lhs->kind == ExprKind::Identifier){
                            if(const ScopedVarInfo* lhsLocal = FindScopedVar(scope, expr.lhs->text))
                                isTaskLocalOwner = (lhsLocal->declaredParallelDepth == m_parallelDepth);
                        }
                        if(!isTaskLocalOwner){
                            reportError(errors, expr.span, "shared 'mut' access to non-thread-safe member container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                            ok = false;
                        }
                    }
                }
            }
            else if(!m_symbols.hasTypeSymbol(baseType)){
                if(const BuiltinFieldInfo* bf = FindBuiltinField(baseType, expr.text)){
                    recordBuiltinTypeHeader(bf->fieldTypeName);
                    recordBuiltinTypeHeader(bf->elemTypeName);
                }
            }
        }
        break;
    }
    case ExprKind::Index:{
        const u32 savedParallelDepth = m_parallelDepth;
        const TypeRef* indexedElemType = inferExprTypeRef(ownerClass, &method, expr, scope);
        const ParamPassingMode::Enum elemRefMode = m_symbols.resolveRefMode(indexedElemType);
        b8 lhsNeedsMut = isMutationTarget && elemRefMode != ParamPassingMode::Mut;
        b8 rhsNeedsMut = false;
        const TypeRef* containerTypeRef = nullptr;
        const MStringView lhsBaseType = expr.lhs
            ? inferExprBaseType(ownerClass, &method, *expr.lhs, scope, &containerTypeRef)
            : MStringView{};
        const BuiltinGenericTypeInfo* genericInfo = m_symbols.hasTypeSymbol(lhsBaseType) ? nullptr : FindBuiltinGenericType(lhsBaseType);
        const BuiltinReceiverMethodInfo* builtinOpIdx = m_symbols.hasTypeSymbol(lhsBaseType) ? nullptr : FindBuiltinReceiverMethod(lhsBaseType, "[]");
        if(builtinOpIdx)
            recordBuiltinTypeHeader(builtinOpIdx->returnTypeName);
        if(!(genericInfo && genericInfo->isReflectedMap && genericInfo->isNonThreadSafeContainer))
            m_parallelDepth = 0u;
        u32 vecIdxLanes = 0u;
        if(const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(lhsBaseType); simdVecInfo && simdVecInfo->isMixed){
            reportError(errors, expr.span, "dynamic indexing '[]' is not supported on mixed float/integer SIMD vector types; use explicit .x/.y/.z/.w lane access");
            ok = false;
        }
        else if(SymbolTable::IsSimdMatrixType(lhsBaseType))
            expr.isMatrixRowIndex = true;
        else if(SymbolTable::IsSimdStorageVectorType(lhsBaseType, vecIdxLanes) || SymbolTable::IsUnalignedVectorType(lhsBaseType, vecIdxLanes))
            expr.isVectorRawIndex = true;
        else if(SymbolTable::IsSimdRegisterVectorType(lhsBaseType) && expr.isMutArg){
            reportError(errors, expr.span, "cannot pass an indexed lane of 'VectorSingle' or 'VectorDouble' to a 'mut' parameter");
            ok = false;
        }
        const b8 recvIsConst = expr.lhs ? isReceiverConst(ownerClass, method, *expr.lhs, scope) : false;
        const ClassDecl* opDeclCls = nullptr;
        const MethodDecl* customOpIdx = resolveCustomOperator(ownerClass, &method, containerTypeRef, lhsBaseType, "[]", 1u, recvIsConst, &opDeclCls);
        if(!customOpIdx){
            if(opDeclCls || m_symbols.findInterface(lhsBaseType) || FindTemplateParam(ownerClass, &method, lhsBaseType)){
                reportError(errors, expr.span, "indexing custom class, interface, or template parameter requires 'operator[]' to be defined");
                ok = false;
            }
            else if(!lhsBaseType.empty() && ConcreteTypeAnalyzer(m_symbols).isKnownConcreteTypeName(lhsBaseType)){
                const b8 isBuiltinIndexable = builtinOpIdx != nullptr
                    || (genericInfo && (genericInfo->isIndexableSequence || genericInfo->isReflectedMap))
                    || IsStringLikeType(lhsBaseType)
                    || SymbolTable::IsSimdType(lhsBaseType)
                    || SymbolTable::IsUnalignedVectorType(lhsBaseType);
                if(builtinOpIdx && builtinOpIdx->isMutating)
                    lhsNeedsMut = true;
                if(!isBuiltinIndexable){
                    reportError(errors, expr.span, "type does not support subscripting with 'operator[]'");
                    ok = false;
                }
            }
        }
        else if(customOpIdx){
            if(opDeclCls && ownerClass != opDeclCls && customOpIdx->access != AccessSpecifier::Public){
                reportError(errors, expr.span, "cannot call 'private' or 'protected' operator outside of its declaring class");
                ok = false;
            }
            lhsNeedsMut = !customOpIdx->isConst;
            if(!customOpIdx->params.empty() && customOpIdx->params[0].mode == ParamPassingMode::Mut)
                rhsNeedsMut = true;
        }
        if(expr.lhs && !checkExpr(ownerClass, scope, lhsNeedsMut, method, *expr.lhs, errors))
            ok = false;
        m_parallelDepth = savedParallelDepth;
        if(expr.rhs && !checkExpr(ownerClass, scope, rhsNeedsMut, method, *expr.rhs, errors))
            ok = false;

        if(!customOpIdx && elemRefMode != ParamPassingMode::None)
            expr.derefNotNull = true;
        if(!customOpIdx && genericInfo && genericInfo->isReflectedMap && !recvIsConst)
            expr.useAdhdMapIndex = true;
        if(!customOpIdx && IsStringLikeType(lhsBaseType)){
            if(isMutationTarget){
                if(!HasMutableStringElements(lhsBaseType)){
                    reportError(errors, expr.span, "cannot mutate elements of 'StringView' or 'CompactString'");
                    ok = false;
                }
                else{
                    expr.isStringByteMutRef = true;
                }
            }
            else{
                expr.isStringByteRead = true;
            }
        }
        if(isMutationTarget){
            if(elemRefMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                ok = false;
            }
            else if((customOpIdx && customOpIdx->returnType && elemRefMode == ParamPassingMode::None) || (builtinOpIdx && !builtinOpIdx->isMutating)){
                reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::Call:
        if(!checkCallExpr(ownerClass, scope, isMutationTarget, method, expr, errors))
            ok = false;
        break;
    case ExprKind::Unary:{
        b8 mutates = OperatorSemantics::IsIncrementOrDecrementOperatorToken(expr.op);
        if(expr.lhs){
            const TypeRef* lhsTypeRef = nullptr;
            const MStringView lhsType = inferExprBaseType(ownerClass, &method, *expr.lhs, scope, &lhsTypeRef);
            const MStringView opSym = OperatorSemantics::MapTokenTypeToOperatorSymbol(expr.op);
            const b8 recvIsConst = isReceiverConst(ownerClass, method, *expr.lhs, scope);
            const ClassDecl* opDeclCls = nullptr;
            const MethodDecl* opMethod = resolveCustomOperator(ownerClass, &method, lhsTypeRef, lhsType, opSym, 0u, recvIsConst, &opDeclCls);
            if(const EnumDecl* lhsEnum = m_symbols.findEnum(lhsType)){
                if(expr.op == TokenType::Tilde && !lhsEnum->isFlag){
                    reportError(errors, expr.span, "bitwise '~' is only supported on 'flag' types, not 'enum' types");
                    ok = false;
                }
                else if(OperatorSemantics::IsIncrementOrDecrementOperatorToken(expr.op)){
                    reportError(errors, expr.span, "increment and decrement operators are not supported on 'enum' or 'flag' types");
                    ok = false;
                }
            }
            else if(const BuiltinEnumInfo* lhsBuiltinEnum = FindBuiltinEnum(lhsType)){
                if(expr.op == TokenType::Tilde && !lhsBuiltinEnum->isFlag){
                    reportError(errors, expr.span, "bitwise '~' is only supported on 'flag' types, not 'enum' types");
                    ok = false;
                }
                else if(OperatorSemantics::IsIncrementOrDecrementOperatorToken(expr.op)){
                    reportError(errors, expr.span, "increment and decrement operators are not supported on 'enum' or 'flag' types");
                    ok = false;
                }
            }
            if(!opMethod){
                if(opDeclCls || m_symbols.findInterface(lhsType) || FindTemplateParam(ownerClass, &method, lhsType)){
                    reportError(errors, expr.span, "unary operator is not defined on operand type");
                    ok = false;
                }
                else if((expr.op == TokenType::Minus || expr.op == TokenType::Tilde) && (SymbolTable::IsSimdStorageType(lhsType) || SymbolTable::IsUnalignedVectorType(lhsType))){
                    reportError(errors, expr.span, "SIMD unary operators require 'VectorSingle', 'VectorDouble', 'MatrixSingle', or 'MatrixDouble'; load memory-layout values first");
                    ok = false;
                }
                else if(expr.op == TokenType::Minus && !lhsType.empty() && !BuiltinTypeSemantics::IsNumericScalarType(lhsType) && !SymbolTable::IsSimdRegisterType(lhsType) && ConcreteTypeAnalyzer(m_symbols).isKnownConcreteTypeName(lhsType)){
                    reportError(errors, expr.span, "unary '-' requires a numeric scalar or SIMD operand");
                    ok = false;
                }
                else if((expr.op == TokenType::PlusPlus || expr.op == TokenType::MinusMinus) && !lhsType.empty() && !BuiltinTypeSemantics::IsNumericScalarType(lhsType) && ConcreteTypeAnalyzer(m_symbols).isKnownConcreteTypeName(lhsType)){
                    reportError(errors, expr.span, "increment and decrement operators require a numeric scalar operand");
                    ok = false;
                }
                else if(expr.op == TokenType::Tilde && !lhsType.empty() && !m_symbols.findEnum(lhsType) && !FindBuiltinEnum(lhsType) && !BuiltinTypeSemantics::IsIntegerScalarType(lhsType) && !SymbolTable::IsSimdRegisterVectorType(lhsType) && ConcreteTypeAnalyzer(m_symbols).isKnownConcreteTypeName(lhsType)){
                    reportError(errors, expr.span, "bitwise '~' requires an integer, SIMD, or 'flag' operand");
                    ok = false;
                }
            }
            else if(opMethod){
                if(opDeclCls && ownerClass != opDeclCls && opMethod->access != AccessSpecifier::Public){
                    reportError(errors, expr.span, "cannot call 'private' or 'protected' operator outside of its declaring class");
                    ok = false;
                }
                if(!opMethod->isConst)
                    mutates = true;
            }
            if(!checkExpr(ownerClass, scope, mutates, method, *expr.lhs, errors))
                ok = false;
        }
        const TypeRef* unaryRetType = inferExprTypeRef(ownerClass, &method, expr, scope);
        const ParamPassingMode::Enum unaryRetRefMode = m_symbols.resolveRefMode(unaryRetType);
        if(isMutationTarget && unaryRetRefMode != ParamPassingMode::Mut){
            reportError(errors, expr.span, "cannot pass unary expression result to a 'mut' target");
            ok = false;
        }
        break;
    }
    case ExprKind::Binary:
        if(isMutationTarget && !OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op)){
            reportError(errors, expr.span, "cannot pass temporary or rvalue expression to a 'mut' target");
            ok = false;
        }
        if(!BinaryExpressionChecker(*this).check(ownerClass, scope, method, expr, errors))
            ok = false;
        break;
    case ExprKind::Cast:
    case ExprKind::BitCast:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot pass cast result (rvalue) to a 'mut' target");
            ok = false;
        }
        if(expr.targetType){
            if(!checkTypeRef(*expr.targetType, ownerClass, &method, errors))
                ok = false;
            if(m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.targetType->name))){
                reportError(errors, expr.span, "casting to an 'interface' type is prohibited in adhd");
                ok = false;
            }
        }
        if(expr.lhs){
            if(!checkExpr(ownerClass, scope, false, method, *expr.lhs, errors))
                ok = false;
            const MStringView srcType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
            if(!srcType.empty() && m_symbols.findInterface(srcType)){
                reportError(errors, expr.span, "downcasting from an 'interface' type is prohibited in adhd");
                ok = false;
            }
            if(expr.kind == ExprKind::BitCast && expr.targetType && !srcType.empty()){
                const MStringView dstType = m_symbols.resolveBaseTypeName(expr.targetType->name);
                const auto hasKnownFixedLayout = [&](const MStringView baseName)noexcept -> b8{
                    if(baseName.empty() || FindTemplateParam(ownerClass, &method, baseName))
                        return false;
                    return BuiltinTypeSemantics::IsScalarType(baseName)
                        || SymbolTable::IsSimdType(baseName)
                        || SymbolTable::IsUnalignedVectorType(baseName)
                        || m_symbols.findEnum(baseName) != nullptr
                        || FindBuiltinEnum(baseName) != nullptr;
                };
                if(m_symbols.resolveRefMode(expr.targetType) == ParamPassingMode::None && hasKnownFixedLayout(dstType) && hasKnownFixedLayout(srcType)){
                    TypeRef srcTypeRef(m_arena);
                    srcTypeRef.name = srcType;
                    usize dstSize = 0u;
                    usize dstAlign = 0u;
                    usize srcSize = 0u;
                    usize srcAlign = 0u;
                    m_layoutOptimizer.resolveTypeSizeAndAlign(*expr.targetType, dstSize, dstAlign);
                    m_layoutOptimizer.resolveTypeSizeAndAlign(srcTypeRef, srcSize, srcAlign);
                    if(dstSize != srcSize){
                        reportError(errors, expr.span, "bit_cast requires source and target types to have identical byte sizes");
                        ok = false;
                    }
                }
            }
        }
        break;
    }
    case ExprKind::Lambda:
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot pass lambda temporary to a 'mut' target");
            ok = false;
        }
        if(!LambdaExpressionChecker(*this).check(ownerClass, scope, method, expr, errors))
            ok = false;
        break;
    default:
        break;
    }

    const MStringView inferredType = inferExprTypeName(ownerClass, &method, expr, scope);
    if(SymbolTable::IsSimdType(inferredType))
        expr.resolvedSimdType = inferredType;
    else if(u32 unalignedLanes = 0u; SymbolTable::IsUnalignedVectorType(inferredType, unalignedLanes))
        expr.resolvedUnalignedType = inferredType;

    expr.resolvedScalarType = {};
    if(expr.kind == ExprKind::Binary && expr.lhs && expr.rhs && !OperatorSemantics::IsBoolReturnOperatorToken(expr.op)){
        const MStringView lhsType = expr.lhs->resolvedScalarType;
        const MStringView rhsType = expr.rhs->resolvedScalarType;
        if(BuiltinTypeSemantics::IsScalarType(lhsType) && BuiltinTypeSemantics::IsScalarType(rhsType)){
            if(OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op))
                expr.resolvedScalarType = lhsType;
            else if(lhsType == "f64" || rhsType == "f64")
                expr.resolvedScalarType = "f64";
            else if(lhsType == "f32" || rhsType == "f32")
                expr.resolvedScalarType = "f32";
            else
                expr.resolvedScalarType = inferredType;
        }
    }
    else if(expr.kind == ExprKind::Unary && expr.lhs && !OperatorSemantics::IsBoolReturnOperatorToken(expr.op))
        expr.resolvedScalarType = expr.lhs->resolvedScalarType;
    else if(expr.kind == ExprKind::Call && expr.isBuiltinCall && expr.lhs && expr.lhs->kind == ExprKind::Identifier
        && !expr.arguments.empty() && expr.arguments[0] && !expr.arguments[0]->resolvedScalarType.empty()
        && FindBuiltinFunction(expr.lhs->text) && FindBuiltinFunction(expr.lhs->text)->returnRule == BuiltinFunctionReturnRule::PromoteSimd){
        expr.resolvedScalarType = expr.arguments[0]->resolvedScalarType;
        if(expr.lhs->text == "Lerp"){
            for(const Expr* argument : expr.arguments){
                if(!argument || !BuiltinTypeSemantics::IsScalarType(argument->resolvedScalarType)){
                    expr.resolvedScalarType = {};
                    break;
                }
                if(argument->resolvedScalarType == "f64")
                    expr.resolvedScalarType = "f64";
                else if(argument->resolvedScalarType == "f32" && expr.resolvedScalarType != "f64")
                    expr.resolvedScalarType = "f32";
            }
        }
        else if(expr.lhs->text != "Rsqrt" && !expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
            expr.resolvedScalarType = m_symbols.resolveBaseTypeName(expr.callTemplateArgs[0].type->name);
    }
    else if(BuiltinTypeSemantics::IsNumericScalarType(inferredType) || inferredType == "b8")
        expr.resolvedScalarType = inferredType;

    return ok;
}

b8 TypeChecker::isReceiverConst(const ClassDecl* ownerClass, const MethodDecl& method, const Expr& recv, const Span<const ScopedVarInfo> scope)const noexcept{
    if(recv.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = FindScopedVar(scope, recv.text)){
            if(local->mode == ParamPassingMode::In || local->isConst)
                return true;
            const ParamPassingMode::Enum localRefMode = m_symbols.resolveRefMode(local->type);
            if(localRefMode != ParamPassingMode::None)
                return localRefMode == ParamPassingMode::In;
            return false;
        }
        if(m_symbols.findGlobalConstant(recv.text))
            return true;
    }
    else if(recv.kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, recv.text)){
            const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(field->type);
            if(fieldRefMode != ParamPassingMode::None)
                return fieldRefMode == ParamPassingMode::In;
        }
        return isThisConstInScope(method, scope);
    }
    else if(recv.kind == ExprKind::MemberAccess && recv.lhs){
        if(const TypeRef* ft = inferExprTypeRef(ownerClass, &method, recv, scope)){
            const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(ft);
            if(fieldRefMode != ParamPassingMode::None)
                return fieldRefMode == ParamPassingMode::In;
        }
        return isReceiverConst(ownerClass, method, *recv.lhs, scope);
    }
    else if(recv.kind == ExprKind::Index || recv.kind == ExprKind::Call){
        if(const TypeRef* rt = inferExprTypeRef(ownerClass, &method, recv, scope)){
            const ParamPassingMode::Enum refMode = m_symbols.resolveRefMode(rt);
            if(refMode != ParamPassingMode::None)
                return refMode == ParamPassingMode::In;
        }
        if(recv.kind == ExprKind::Index && recv.lhs)
            return isReceiverConst(ownerClass, method, *recv.lhs, scope);
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

