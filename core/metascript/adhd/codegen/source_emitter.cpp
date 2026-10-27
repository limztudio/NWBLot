// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendTaskOptions(MString& out, const Stmt& stmt){
    out.append("::NWB::Core::CpuTaskOptions{{}, ::NWB::Core::CpuTaskCost::");
    switch(stmt.parallelCore){
    case ParallelCoreKind::Heavy: out.append("Heavy"); break;
    case ParallelCoreKind::Light: out.append("Light"); break;
    case ParallelCoreKind::Any:
    case ParallelCoreKind::MainThread: out.append("Any"); break;
    }
    out.append(", ::NWB::Core::CpuTaskPriority::");
    switch(stmt.parallelPriority){
    case ParallelPriorityKind::Critical: out.append("Critical"); break;
    case ParallelPriorityKind::Normal: out.append("Normal"); break;
    case ParallelPriorityKind::Background: out.append("Background"); break;
    }
    out.append(", ::NWB::Core::CpuTaskTarget::");
    if(stmt.parallelCore == ParallelCoreKind::MainThread)
        out.append("MainThread");
    else
        out.append("Worker");
    out.push_back('}');
}

[[nodiscard]] MStringView MapBuiltinMathFunction(const MStringView name)noexcept{
    if(name == "min") return "::Min";
    if(name == "max") return "::Max";
    if(name == "abs") return "::Abs";
    if(name == "clamp") return "::Clamp";
    if(name == "floor") return "::Floor";
    if(name == "ceil") return "::Ceil";
    if(name == "round") return "::Truncate";
    if(name == "sqrt") return "::Sqrt";
    if(name == "rsqrt") return "::VectorReciprocalSqrt";
    if(name == "lerp") return "::VectorLerp";
    if(name == "dot") return "::Vector4Dot";
    if(name == "cross") return "::Vector3Cross";
    if(name == "length") return "::Vector4Length";
    if(name == "lengthSq") return "::Vector4LengthSq";
    if(name == "normalize") return "::Vector4Normalize";
    if(name == "mul") return "::MatrixMultiply";
    if(name == "transpose") return "::MatrixTranspose";
    if(name == "inverse") return "::MatrixInverse";
    if(name == "sin") return "::Sin";
    if(name == "cos") return "::Cos";
    if(name == "tan") return "::Tan";
    return {};
}

[[nodiscard]] bool IsRegisterPromotableSimdType(const MStringView simdType)noexcept{
    return simdType == "f32x4"
        || simdType == "i32x4"
        || simdType == "u32x4"
        || simdType == "f64x2"
        || simdType == "f64x3"
        || simdType == "f64x4"
        || simdType == "i64x4"
        || simdType == "u64x4"
        || simdType == "f32x3x4"
        || simdType == "f32x4x4"
        || simdType == "f64x3x3"
        || simdType == "f64x3x4"
        || simdType == "f64x4x4"
    ;
}

[[nodiscard]] bool IsDoubleSimdType(const MStringView simdType)noexcept{
    return simdType == "f64x2"
        || simdType == "f64x3"
        || simdType == "f64x4"
        || simdType == "i64x4"
        || simdType == "u64x4"
        || simdType == "f64x3x3"
        || simdType == "f64x3x4"
        || simdType == "f64x4x4"
    ;
}

[[nodiscard]] bool IsIntSimdType(const MStringView simdType)noexcept{
    return simdType == "i32x4"
        || simdType == "u32x4"
        || simdType == "i64x4"
        || simdType == "u64x4"
    ;
}

[[nodiscard]] bool IsSignedIntSimdType(const MStringView simdType)noexcept{
    return simdType == "i32x4" || simdType == "i64x4";
}

[[nodiscard]] bool IsFloatSimdVectorType(const MStringView simdType)noexcept{
    return simdType == "f32x4"
        || simdType == "f64x2"
        || simdType == "f64x3"
        || simdType == "f64x4"
    ;
}

[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept{
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle && expr.text.size() >= 2u && expr.text.size() <= 4u)
        return static_cast<u32>(expr.text.size());
    if(expr.resolvedSimdType == "f64x2")
        return 2u;
    if(expr.resolvedSimdType == "f64x3")
        return 3u;
    return 4u;
}

[[nodiscard]] bool IsUnitFloatLiteral(const Expr& expr)noexcept{
    if(expr.kind != ExprKind::FloatLiteral && expr.kind != ExprKind::DoubleLiteral)
        return false;
    return expr.text == "1.0f" || expr.text == "1.0" || expr.text == "1.f";
}

[[nodiscard]] MStringView SimdRegisterCppType(const MStringView simdType)noexcept{
    if(simdType == "f32x3x4" || simdType == "f32x4x4")
        return "::SIMDMatrix";
    if(simdType == "f64x3x3" || simdType == "f64x3x4" || simdType == "f64x4x4")
        return "::SIMDMatrixDouble";
    if(IsDoubleSimdType(simdType))
        return "::SIMDVectorDouble";
    return "::SIMDVectorSingle";
}

[[nodiscard]] MStringView SimdLoadFunctionCpp(const MStringView simdType)noexcept{
    if(simdType == "i32x4" || simdType == "u32x4")
        return "::LoadInt";
    if(simdType == "i64x4" || simdType == "u64x4")
        return "::LoadDoubleInt";
    if(IsDoubleSimdType(simdType))
        return "::LoadDouble";
    return "::LoadFloat";
}

[[nodiscard]] MStringView SimdStoreFunctionCpp(const MStringView simdType)noexcept{
    if(simdType == "i32x4" || simdType == "u32x4")
        return "::StoreInt";
    if(simdType == "i64x4" || simdType == "u64x4")
        return "::StoreDoubleInt";
    if(IsDoubleSimdType(simdType))
        return "::StoreDouble";
    return "::StoreFloat";
}

[[nodiscard]] bool IsSimdAssignmentOp(const TokenType::Enum op)noexcept{
    switch(op){
    case TokenType::Equal:
    case TokenType::PlusEqual:
    case TokenType::MinusEqual:
    case TokenType::StarEqual:
    case TokenType::SlashEqual:
    case TokenType::AmpersandEqual:
    case TokenType::PipeEqual:
    case TokenType::CaretEqual:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] MStringView SimdCompoundScalarOpSymbol(const TokenType::Enum op)noexcept{
    switch(op){
    case TokenType::PlusEqual: return " + ";
    case TokenType::MinusEqual: return " - ";
    case TokenType::StarEqual: return " * ";
    case TokenType::SlashEqual: return " / ";
    case TokenType::AmpersandEqual: return " & ";
    case TokenType::PipeEqual: return " | ";
    case TokenType::CaretEqual: return " ^ ";
    default: return {};
    }
}

[[nodiscard]] bool IsSingleSwizzleLane(const MStringView text, u32& outLane)noexcept{
    if(text.size() != 1u)
        return false;
    switch(text[0]){
    case 'x':
    case 'r':
        outLane = 0u;
        return true;
    case 'y':
    case 'g':
        outLane = 1u;
        return true;
    case 'z':
    case 'b':
        outLane = 2u;
        return true;
    case 'w':
    case 'a':
        outLane = 3u;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] u32 MapSwizzleCharToLane(const char c)noexcept{
    switch(c){
    case 'x':
    case 'r':
        return 0u;
    case 'y':
    case 'g':
        return 1u;
    case 'z':
    case 'b':
        return 2u;
    default:
        return 3u;
    }
}

[[nodiscard]] bool ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept{
    if(!expr)
        return false;

    if(expr->kind == ExprKind::MemberAccess && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
        u32 lane = 0u;
        if(expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane))
            return true;
    }

    if(expr->kind == ExprKind::Index && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Unary && IsRegisterPromotableSimdType(expr->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Binary && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        if(expr->op != TokenType::Equal)
            return true;
        if(ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }

    if(expr->kind == ExprKind::Binary && IsSimdAssignmentOp(expr->op) && expr->lhs){
        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr->lhs->text, lane))
        {
            return true;
        }
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType))
        {
            return true;
        }
    }

    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr->lhs->text;
        if(symbols.isSimdType(callee) && IsRegisterPromotableSimdType(expr->resolvedSimdType))
            return true;
        if(!MapBuiltinMathFunction(callee).empty()){
            if(IsRegisterPromotableSimdType(expr->resolvedSimdType))
                return true;
            for(const Expr* arg : expr->arguments){
                if(arg && IsRegisterPromotableSimdType(arg->resolvedSimdType))
                    return true;
            }
        }
    }

    if(ExprContainsSimdMath(expr->lhs, symbols) || ExprContainsSimdMath(expr->rhs, symbols))
        return true;

    for(const Expr* arg : expr->arguments){
        if(ExprContainsSimdMath(arg, symbols))
            return true;
    }
    return false;
}

[[nodiscard]] bool StmtsContainSimdMath(const MVector<Stmt*>& statements, const SymbolTable& symbols)noexcept;

[[nodiscard]] bool StmtContainsSimdMath(const Stmt* stmt, const SymbolTable& symbols)noexcept{
    if(!stmt)
        return false;

    switch(stmt->kind){
    case StmtKind::Block:
    case StmtKind::ParallelBlock:
        return StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::VarDecl:{
        if(stmt->isCtorInit && stmt->varType && IsRegisterPromotableSimdType(stmt->varType->name)){
            u32 lanes = 0u;
            if(symbols.isSimdVectorType(stmt->varType->name, lanes))
                return true;
        }
        if(ExprContainsSimdMath(stmt->expr, symbols))
            return true;
        for(const Expr* arg : stmt->arguments){
            if(ExprContainsSimdMath(arg, symbols))
                return true;
        }
        return false;
    }
    case StmtKind::ExprStmt:
    case StmtKind::Return:
    case StmtKind::ParallelCall:
        return ExprContainsSimdMath(stmt->expr, symbols);
    case StmtKind::If:
        return ExprContainsSimdMath(stmt->condition, symbols)
            || StmtsContainSimdMath(stmt->statements, symbols)
            || StmtsContainSimdMath(stmt->elseStatements, symbols);
    case StmtKind::Switch:{
        if(ExprContainsSimdMath(stmt->expr, symbols))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(ExprContainsSimdMath(sc.matchExpr, symbols) || StmtsContainSimdMath(sc.statements, symbols))
                return true;
        }
        return false;
    }
    case StmtKind::ForRange:
    case StmtKind::ParallelForRange:
        return ExprContainsSimdMath(stmt->expr, symbols) || StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::ForIndex:
    case StmtKind::ParallelForIndex:
        return StmtContainsSimdMath(stmt->initStmt, symbols)
            || ExprContainsSimdMath(stmt->condition, symbols)
            || ExprContainsSimdMath(stmt->stepExpr, symbols)
            || StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::While:
    case StmtKind::DoWhile:
        return ExprContainsSimdMath(stmt->condition, symbols) || StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::Diagnostic:{
        for(const Expr* arg : stmt->arguments){
            if(ExprContainsSimdMath(arg, symbols))
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

[[nodiscard]] bool StmtsContainSimdMath(const MVector<Stmt*>& statements, const SymbolTable& symbols)noexcept{
    for(const Stmt* stmt : statements){
        if(StmtContainsSimdMath(stmt, symbols))
            return true;
    }
    return false;
}

SimdVarBinding* FindOrCreateBinding(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const bool isThisMember,
    const MStringView name,
    const MStringView simdType
){
    for(SimdVarBinding& b : bindings){
        if(b.isThisMember == isThisMember && b.name == name)
            return &b;
    }

    bindings.emplace_back(arena);
    SimdVarBinding& created = bindings.back();
    created.isThisMember = isThisMember;
    created.name = name;
    created.simdType = simdType;
    created.regName.append(isThisMember ? "__adhd_simd_this_" : "__adhd_simd_");
    created.regName.append(name.data(), name.size());
    return &created;
}

void PreRegisterLocalSimdDecls(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const MVector<Stmt*>& statements,
    const SymbolTable& symbols
){
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        switch(stmt->kind){
        case StmtKind::VarDecl:{
            if(stmt->usesScratchArena || (stmt->varType && stmt->varType->refMode != ParamPassingMode::None))
                break;

            MStringView varSimdType;
            if(stmt->varType && IsRegisterPromotableSimdType(stmt->varType->name))
                varSimdType = stmt->varType->name;
            else if(stmt->expr && IsRegisterPromotableSimdType(stmt->expr->resolvedSimdType))
                varSimdType = stmt->expr->resolvedSimdType;

            if(!varSimdType.empty()){
                u32 vecLanes = 0u;
                if(!stmt->isCtorInit || symbols.isSimdVectorType(varSimdType, vecLanes)){
                    SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, false, stmt->name, varSimdType);
                    binding->isLocalDecl = true;
                }
            }
            break;
        }
        case StmtKind::Block:
        case StmtKind::ParallelBlock:
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            break;
        case StmtKind::If:
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            PreRegisterLocalSimdDecls(arena, bindings, stmt->elseStatements, symbols);
            break;
        case StmtKind::Switch:
            for(const SwitchCase& sc : stmt->switchCases)
                PreRegisterLocalSimdDecls(arena, bindings, sc.statements, symbols);
            break;
        case StmtKind::ForRange:
        case StmtKind::ParallelForRange:{
            if(stmt->varType && IsRegisterPromotableSimdType(stmt->varType->name)){
                SimdVarBinding* loopBinding = FindOrCreateBinding(arena, bindings, false, stmt->name, stmt->varType->name);
                loopBinding->isLoopElement = true;
            }
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            break;
        }
        case StmtKind::ForIndex:
        case StmtKind::ParallelForIndex:
        case StmtKind::While:
        case StmtKind::DoWhile:
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            break;
        default:
            break;
        }
    }
}

void AnalyzeSimdExprReads(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const Expr* expr,
    const SymbolTable& symbols,
    const bool requireStorage,
    const bool canUnconditionallyWrite
){
    if(!expr)
        return;

    if((expr->kind == ExprKind::Identifier || expr->kind == ExprKind::ThisMember) && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        const bool isThis = expr->kind == ExprKind::ThisMember;
        SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, isThis, expr->text, expr->resolvedSimdType);
        if(!binding->isLocalDecl && !binding->unconditionallyWrittenFirst)
            binding->readBeforeWrite = true;
        if(requireStorage)
            binding->needsStorageSync = true;
        return;
    }

    if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& cap : expr->lambdaCaptures){
            for(SimdVarBinding& b : bindings){
                if(!b.isThisMember && b.name == cap.name)
                    b.needsStorageSync = true;
            }
        }
        return;
    }

    if(expr->kind == ExprKind::MemberAccess){
        u32 lane = 0u;
        const bool isSimdSwizzleOrLane = expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)
            && (expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane));
        AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, !isSimdSwizzleOrLane, false);
        return;
    }

    if(expr->kind == ExprKind::Index){
        u32 vecLanes = 0u;
        const bool isSimdVecIndex = expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)
            && symbols.isSimdVectorType(expr->lhs->resolvedSimdType, vecLanes);
        AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, !isSimdVecIndex, false);
        AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
        return;
    }

    if(expr->kind == ExprKind::Call){
        bool isSimdIntrinsicOrCtor = false;
        if(expr->lhs && expr->lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr->lhs->text;
            if(symbols.isSimdType(callee) || !MapBuiltinMathFunction(callee).empty())
                isSimdIntrinsicOrCtor = true;
        }
        if(!isSimdIntrinsicOrCtor)
            AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, true, false);
        for(const Expr* arg : expr->arguments)
            AnalyzeSimdExprReads(arena, bindings, arg, symbols, !isSimdIntrinsicOrCtor, false);
        return;
    }

    if(expr->kind == ExprKind::Binary && IsSimdAssignmentOp(expr->op) && expr->lhs){
        if(IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
            AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
            if(expr->lhs->kind == ExprKind::Identifier || expr->lhs->kind == ExprKind::ThisMember){
                const bool isThis = expr->lhs->kind == ExprKind::ThisMember;
                SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, isThis, expr->lhs->text, expr->lhs->resolvedSimdType);
                if(!binding->isLocalDecl){
                    if(expr->op == TokenType::Equal && canUnconditionallyWrite && !binding->isLoopElement && !binding->readBeforeWrite && !binding->written)
                        binding->unconditionallyWrittenFirst = true;
                    else if(!binding->unconditionallyWrittenFirst)
                        binding->readBeforeWrite = true;
                }
                binding->written = true;
            }
            else{
                AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, true, false);
            }
            return;
        }

        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr->lhs->text, lane)
            && (expr->lhs->lhs->kind == ExprKind::Identifier || expr->lhs->lhs->kind == ExprKind::ThisMember))
        {
            AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
            const bool isThis = expr->lhs->lhs->kind == ExprKind::ThisMember;
            SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, isThis, expr->lhs->lhs->text, expr->lhs->lhs->resolvedSimdType);
            if(!binding->isLocalDecl && !binding->unconditionallyWrittenFirst)
                binding->readBeforeWrite = true;
            binding->written = true;
            return;
        }

        u32 vecLanes = 0u;
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && symbols.isSimdVectorType(expr->lhs->lhs->resolvedSimdType, vecLanes)
            && (expr->lhs->lhs->kind == ExprKind::Identifier || expr->lhs->lhs->kind == ExprKind::ThisMember))
        {
            AnalyzeSimdExprReads(arena, bindings, expr->lhs->rhs, symbols, false, false);
            AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
            const bool isThis = expr->lhs->lhs->kind == ExprKind::ThisMember;
            SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, isThis, expr->lhs->lhs->text, expr->lhs->lhs->resolvedSimdType);
            if(!binding->isLocalDecl && !binding->unconditionallyWrittenFirst)
                binding->readBeforeWrite = true;
            binding->written = true;
            return;
        }
    }

    AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, false, false);
    AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
}

void ScanStmtsForSimdBindings(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const MVector<Stmt*>& statements,
    const SymbolTable& symbols,
    const bool inConditionalOrLoop,
    bool& ioHasSeenPossibleReturn
){
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        const bool canUnconditionallyWrite = !inConditionalOrLoop && !ioHasSeenPossibleReturn;

        switch(stmt->kind){
        case StmtKind::VarDecl:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, false, false);
            for(const Expr* arg : stmt->arguments)
                AnalyzeSimdExprReads(arena, bindings, arg, symbols, false, false);
            break;
        }
        case StmtKind::ExprStmt:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, false, canUnconditionallyWrite);
            break;
        }
        case StmtKind::Return:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, false, false);
            ioHasSeenPossibleReturn = true;
            break;
        }
        case StmtKind::Block:{
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, inConditionalOrLoop, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::If:{
            AnalyzeSimdExprReads(arena, bindings, stmt->condition, symbols, false, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            ScanStmtsForSimdBindings(arena, bindings, stmt->elseStatements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::Switch:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, false, false);
            for(const SwitchCase& sc : stmt->switchCases){
                AnalyzeSimdExprReads(arena, bindings, sc.matchExpr, symbols, false, false);
                ScanStmtsForSimdBindings(arena, bindings, sc.statements, symbols, true, ioHasSeenPossibleReturn);
            }
            break;
        }
        case StmtKind::ForRange:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, true, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ForIndex:{
            if(stmt->initStmt){
                AnalyzeSimdExprReads(arena, bindings, stmt->initStmt->expr, symbols, false, false);
                for(const Expr* arg : stmt->initStmt->arguments)
                    AnalyzeSimdExprReads(arena, bindings, arg, symbols, false, false);
            }
            AnalyzeSimdExprReads(arena, bindings, stmt->condition, symbols, false, false);
            AnalyzeSimdExprReads(arena, bindings, stmt->stepExpr, symbols, false, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::While:
        case StmtKind::DoWhile:{
            AnalyzeSimdExprReads(arena, bindings, stmt->condition, symbols, false, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelBlock:{
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelForRange:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, true, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelForIndex:{
            if(stmt->initStmt){
                AnalyzeSimdExprReads(arena, bindings, stmt->initStmt->expr, symbols, false, false);
                for(const Expr* arg : stmt->initStmt->arguments)
                    AnalyzeSimdExprReads(arena, bindings, arg, symbols, false, false);
            }
            AnalyzeSimdExprReads(arena, bindings, stmt->condition, symbols, false, false);
            AnalyzeSimdExprReads(arena, bindings, stmt->stepExpr, symbols, false, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelCall:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, true, false);
            break;
        }
        case StmtKind::NativeBlock:{
            for(const NativeBinding& nb : stmt->nativeBindings)
                AnalyzeSimdExprReads(arena, bindings, nb.sourceExpr, symbols, true, false);
            break;
        }
        case StmtKind::Diagnostic:{
            for(const Expr* arg : stmt->arguments)
                AnalyzeSimdExprReads(arena, bindings, arg, symbols, true, false);
            break;
        }
        default:
            break;
        }
    }
}

void CollectScopedSimdBindings(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const MVector<Stmt*>& statements,
    const SymbolTable& symbols
){
    if(!StmtsContainSimdMath(statements, symbols))
        return;

    PreRegisterLocalSimdDecls(arena, bindings, statements, symbols);
    bool hasSeenPossibleReturn = false;
    ScanStmtsForSimdBindings(arena, bindings, statements, symbols, false, hasSeenPossibleReturn);
}

[[nodiscard]] bool IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept{
    if(!expr || expr->kind != ExprKind::Binary || expr->op != TokenType::Star || !expr->lhs || !expr->rhs)
        return false;
    if(expr->resolvedSimdType.empty() || !IsFloatSimdVectorType(expr->resolvedSimdType))
        return false;
    outMulA = expr->lhs;
    outMulB = expr->rhs;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString SourceEmitter::emitSource(
    const TranslationUnit& unit,
    const MStringView headerIncludePath,
    const MStringView namespacePath
)const{
    MString out(m_arena);
    out.reserve(4096u);

    ReflectionEmitter reflectionEmitter(m_symbols);

    out.append(s_AdhdGeneratedFileBanner.data(), s_AdhdGeneratedFileBanner.size());
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append("#include \"");
    out.append(headerIncludePath.data(), headerIncludePath.size());
    out.append("\"\r\n\r\n");
    out.append("#include <global/assert.h>\r\n");
    out.append("#include <core/common/log.h>\r\n");
    out.append("#include <core/task/cpu/scheduler.h>\r\n\r\n\r\n");
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl)
            continue;

        if(classDecl->templateParams.empty()){
            for(const MethodDecl* method : classDecl->methods){
                if(!method || method->isImported || !method->hasBody || method->isInline || !method->templateParams.empty())
                    continue;

                m_headerEmitter.emitMethodSignature(out, classDecl, *method, false);
                if(method->isConstructor)
                    m_headerEmitter.emitConstructorNotNullInitializers(out, classDecl->orderedFields, *method);
                out.append("{\r\n");
                emitMethodBodyBlock(out, *method, 1u);
                out.append("}\r\n\r\n");
            }
        }

        if(!classDecl->isImported && classDecl->templateParams.empty() && (classDecl->isSerialize || classDecl->isReflect || !classDecl->assetTypeName.empty()))
            reflectionEmitter.emitClassReflectionDefinitions(out, *classDecl);
    }

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported || !fn->hasBody || fn->isInline || !fn->templateParams.empty())
            continue;

        m_headerEmitter.emitMethodSignature(out, nullptr, *fn, false);
        out.append("{\r\n");
        emitMethodBodyBlock(out, *fn, 1u);
        out.append("}\r\n\r\n");
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}

void SourceEmitter::emitMethodBodyBlock(
    MString& out,
    const MethodDecl& method,
    const u32 indent
)const{
    if(method.usesScratchArena){
        emitIndent(out, indent);
        out.append("::NWB::Core::Alloc::ScratchArena<> __adhd_scratch;\r\n");
    }
    if(method.usesTaskScope){
        emitIndent(out, indent);
        out.append("static ::NWB::Core::CpuTaskScheduler s_AdhdTaskScheduler;\r\n");
        emitIndent(out, indent);
        out.append("::NWB::Core::CpuTaskScope __adhd_task_scope(s_AdhdTaskScheduler);\r\n");
        emitIndent(out, indent);
        out.append("::ScopeExit __adhd_scope_join([&__adhd_task_scope]()noexcept{ __adhd_task_scope.wait(); });\r\n");
    }

    emitScopedSimdBody(out, method.body, indent);
}

const SimdVarBinding* SourceEmitter::findSimdBinding(const bool isThisMember, const MStringView name)const noexcept{
    if(!m_activeSimdBindings)
        return nullptr;
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isThisMember == isThisMember && binding.name == name)
            return &binding;
    }
    return nullptr;
}

void SourceEmitter::emitSimdStoreForBinding(
    MString& out,
    const SimdVarBinding& binding,
    const u32 indent
)const{
    const MStringView storeFn = __hidden_adhd_source_emitter::SimdStoreFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    out.append(storeFn.data(), storeFn.size());
    out.push_back('(');
    out.append(binding.regName.data(), binding.regName.size());
    out.append(", ");
    if(binding.isThisMember)
        out.append("this->");
    out.append(binding.name.data(), binding.name.size());
    out.append(");\r\n");
}

void SourceEmitter::emitSimdLoadReloadForBinding(
    MString& out,
    const SimdVarBinding& binding,
    const u32 indent
)const{
    const MStringView loadFn = __hidden_adhd_source_emitter::SimdLoadFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    out.append(binding.regName.data(), binding.regName.size());
    out.append(" = ");
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    if(binding.isThisMember)
        out.append("this->");
    out.append(binding.name.data(), binding.name.size());
    out.append(");\r\n");
}

void SourceEmitter::emitSimdBlockStores(MString& out, const u32 indent, const bool loopElementsOnly)const{
    if(!m_activeSimdBindings)
        return;
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isLocalDecl || !binding.written)
            continue;
        if(loopElementsOnly && !binding.isLoopElement)
            continue;
        emitSimdStoreForBinding(out, binding, indent);
    }
}

void SourceEmitter::emitScopedSimdBody(
    MString& out,
    const MVector<Stmt*>& statements,
    const u32 indent
)const{
    MVector<SimdVarBinding> simdBindings(m_arena);
    __hidden_adhd_source_emitter::CollectScopedSimdBindings(m_arena, simdBindings, statements, m_symbols);

    const MVector<SimdVarBinding>* prevBindings = m_activeSimdBindings;
    m_activeSimdBindings = simdBindings.empty() ? nullptr : &simdBindings;

    for(const SimdVarBinding& binding : simdBindings){
        if(binding.isLocalDecl || binding.isLoopElement)
            continue;

        const MStringView regType = __hidden_adhd_source_emitter::SimdRegisterCppType(binding.simdType);
        const MStringView loadFn = __hidden_adhd_source_emitter::SimdLoadFunctionCpp(binding.simdType);

        emitIndent(out, indent);
        if(!binding.written && !binding.needsStorageSync)
            out.append("const ");
        out.append(regType.data(), regType.size());
        out.push_back(' ');
        out.append(binding.regName.data(), binding.regName.size());
        if(binding.readBeforeWrite || !binding.written){
            out.append(" = ");
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            if(binding.isThisMember)
                out.append("this->");
            out.append(binding.name.data(), binding.name.size());
            out.append(");\r\n");
        }
        else{
            out.append("{};\r\n");
        }
    }

    emitStatementList(out, statements, indent);

    const bool endsWithTerminal = !statements.empty() && statements.back() && (
        statements.back()->kind == StmtKind::Return
        || statements.back()->kind == StmtKind::Break
        || statements.back()->kind == StmtKind::Continue
    );
    if(!endsWithTerminal)
        emitSimdBlockStores(out, indent, false);

    m_activeSimdBindings = prevBindings;
}

void SourceEmitter::emitStatementList(
    MString& out,
    const MVector<Stmt*>& statements,
    const u32 indent
)const{
    for(const Stmt* stmt : statements){
        if(stmt)
            emitStmt(out, *stmt, indent);
    }
}

void SourceEmitter::emitStmt(
    MString& out,
    const Stmt& stmt,
    const u32 indent
)const{
    switch(stmt.kind){
    case StmtKind::Block:{
        emitIndent(out, indent);
        out.append("{\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::VarDecl:{
        const SimdVarBinding* simdLocal = findSimdBinding(false, stmt.name);
        if(simdLocal && simdLocal->isLocalDecl){
            const MStringView regType = __hidden_adhd_source_emitter::SimdRegisterCppType(simdLocal->simdType);
            emitIndent(out, indent);
            if(stmt.isConst && !simdLocal->written && !simdLocal->needsStorageSync)
                out.append("const ");
            out.append(regType.data(), regType.size());
            out.push_back(' ');
            out.append(simdLocal->regName.data(), simdLocal->regName.size());
            out.append(" = ");
            if(stmt.isCtorInit){
                emitSimdVectorCtorReg(out, simdLocal->simdType, stmt.arguments);
            }
            else if(stmt.expr){
                emitSimdRegExpr(out, *stmt.expr, simdLocal->simdType);
            }
            else{
                const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(simdLocal->simdType);
                const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(simdLocal->simdType);
                if(m_symbols.isSimdMatrixType(simdLocal->simdType))
                    out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
                else if(isInt)
                    out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
                else
                    out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
            }
            out.append(";\r\n");

            if(simdLocal->needsStorageSync){
                emitIndent(out, indent);
                if(stmt.varType)
                    m_headerEmitter.emitTypeCpp(out, *stmt.varType);
                else{
                    TypeRef inferredType(m_arena);
                    inferredType.name = simdLocal->simdType;
                    m_headerEmitter.emitTypeCpp(out, inferredType);
                }
                out.push_back(' ');
                out.append(stmt.name.data(), stmt.name.size());
                out.append("{};\r\n");
                emitSimdStoreForBinding(out, *simdLocal, indent);
            }
            break;
        }

        emitIndent(out, indent);
        if(stmt.isConst)
            out.append("const ");
        if(stmt.varType)
            m_headerEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.push_back(' ');
        out.append(stmt.name.data(), stmt.name.size());

        if(stmt.varType && stmt.varType->refMode != ParamPassingMode::None && stmt.expr){
            out.append(" = ::MakeNotNull(&(");
            emitExpr(out, *stmt.expr);
            out.append("));\r\n");
        }
        else if(stmt.isCtorInit){
            out.push_back('(');
            for(usize i = 0u; i < stmt.arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(stmt.arguments[i])
                    emitExpr(out, *stmt.arguments[i]);
            }
            if(stmt.usesScratchArena){
                if(!stmt.arguments.empty())
                    out.append(", ");
                out.append("__adhd_scratch");
            }
            out.append(");\r\n");
        }
        else if(stmt.expr){
            out.append(" = ");
            emitExpr(out, *stmt.expr);
            out.append(";\r\n");
        }
        else if(stmt.usesScratchArena){
            out.append("(__adhd_scratch);\r\n");
        }
        else{
            out.append("{};\r\n");
        }
        break;
    }
    case StmtKind::ExprStmt:{
        if(stmt.expr && stmt.expr->kind == ExprKind::Binary
            && __hidden_adhd_source_emitter::IsSimdAssignmentOp(stmt.expr->op)
            && stmt.expr->lhs && stmt.expr->rhs)
        {
            const Expr& lhs = *stmt.expr->lhs;
            const Expr& rhs = *stmt.expr->rhs;

            if(__hidden_adhd_source_emitter::IsRegisterPromotableSimdType(lhs.resolvedSimdType)){
                const MStringView simdType = lhs.resolvedSimdType;
                const SimdVarBinding* targetBinding = nullptr;
                if(lhs.kind == ExprKind::Identifier)
                    targetBinding = findSimdBinding(false, lhs.text);
                else if(lhs.kind == ExprKind::ThisMember)
                    targetBinding = findSimdBinding(true, lhs.text);

                const SimdVarBinding* rhsBinding = nullptr;
                if(rhs.kind == ExprKind::Identifier)
                    rhsBinding = findSimdBinding(false, rhs.text);
                else if(rhs.kind == ExprKind::ThisMember)
                    rhsBinding = findSimdBinding(true, rhs.text);

                const bool shouldLowerNonBindingAssign = !targetBinding && (
                    stmt.expr->op != TokenType::Equal
                    || rhsBinding != nullptr
                    || __hidden_adhd_source_emitter::ExprContainsSimdMath(&rhs, m_symbols)
                );

                if(targetBinding || shouldLowerNonBindingAssign){
                    const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(simdType);
                    const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(simdType);

                    emitIndent(out, indent);
                    if(targetBinding){
                        out.append(targetBinding->regName.data(), targetBinding->regName.size());
                        out.append(" = ");
                    }
                    else{
                        const MStringView storeFn = __hidden_adhd_source_emitter::SimdStoreFunctionCpp(simdType);
                        out.append(storeFn.data(), storeFn.size());
                        out.push_back('(');
                    }

                    auto emitLhsOperandReg = [&](){
                        if(targetBinding)
                            out.append(targetBinding->regName.data(), targetBinding->regName.size());
                        else
                            emitSimdRegExpr(out, lhs, simdType);
                    };

                    const Expr* mulA = nullptr;
                    const Expr* mulB = nullptr;
                    if(stmt.expr->op == TokenType::Equal){
                        emitSimdRegExpr(out, rhs, simdType);
                    }
                    else if(stmt.expr->op == TokenType::PlusEqual){
                        if(__hidden_adhd_source_emitter::IsSimdMulExpr(&rhs, mulA, mulB)){
                            out.append(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(");
                            emitSimdRegExpr(out, *mulA, simdType);
                            out.append(", ");
                            emitSimdRegExpr(out, *mulB, simdType);
                            out.append(", ");
                            emitLhsOperandReg();
                            out.push_back(')');
                        }
                        else{
                            if(isInt)
                                out.append(isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(");
                            else
                                out.append(isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(");
                            emitLhsOperandReg();
                            out.append(", ");
                            emitSimdRegExpr(out, rhs, simdType);
                            out.push_back(')');
                        }
                    }
                    else if(stmt.expr->op == TokenType::MinusEqual){
                        if(__hidden_adhd_source_emitter::IsSimdMulExpr(&rhs, mulA, mulB)){
                            out.append(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(");
                            emitSimdRegExpr(out, *mulA, simdType);
                            out.append(", ");
                            emitSimdRegExpr(out, *mulB, simdType);
                            out.append(", ");
                            emitLhsOperandReg();
                            out.push_back(')');
                        }
                        else{
                            out.append(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(");
                            emitLhsOperandReg();
                            out.append(", ");
                            emitSimdRegExpr(out, rhs, simdType);
                            out.push_back(')');
                        }
                    }
                    else if(stmt.expr->op == TokenType::StarEqual){
                        if(rhs.resolvedSimdType.empty() && !isInt){
                            out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                            emitLhsOperandReg();
                            out.append(", ");
                            emitExpr(out, rhs);
                            out.push_back(')');
                        }
                        else{
                            out.append(isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply(");
                            emitLhsOperandReg();
                            out.append(", ");
                            emitSimdRegExpr(out, rhs, simdType);
                            out.push_back(')');
                        }
                    }
                    else if(stmt.expr->op == TokenType::SlashEqual){
                        out.append(isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(");
                        emitLhsOperandReg();
                        out.append(", ");
                        emitSimdRegExpr(out, rhs, simdType);
                        out.push_back(')');
                    }
                    else if(stmt.expr->op == TokenType::AmpersandEqual){
                        out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                        emitLhsOperandReg();
                        out.append(", ");
                        emitSimdRegExpr(out, rhs, simdType);
                        out.push_back(')');
                    }
                    else if(stmt.expr->op == TokenType::PipeEqual){
                        out.append(isDouble ? "::VectorDoubleOrInt(" : "::VectorOrInt(");
                        emitLhsOperandReg();
                        out.append(", ");
                        emitSimdRegExpr(out, rhs, simdType);
                        out.push_back(')');
                    }
                    else if(stmt.expr->op == TokenType::CaretEqual){
                        out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                        emitLhsOperandReg();
                        out.append(", ");
                        emitSimdRegExpr(out, rhs, simdType);
                        out.push_back(')');
                    }

                    if(targetBinding){
                        out.append(";\r\n");
                        if(targetBinding->needsStorageSync)
                            emitSimdStoreForBinding(out, *targetBinding, indent);
                    }
                    else{
                        out.append(", ");
                        emitExpr(out, lhs);
                        out.append(");\r\n");
                    }
                    break;
                }
            }

            u32 lane = 0u;
            if(lhs.kind == ExprKind::MemberAccess && lhs.lhs
                && __hidden_adhd_source_emitter::IsRegisterPromotableSimdType(lhs.lhs->resolvedSimdType)
                && __hidden_adhd_source_emitter::IsSingleSwizzleLane(lhs.text, lane))
            {
                const SimdVarBinding* targetBinding = nullptr;
                if(lhs.lhs->kind == ExprKind::Identifier)
                    targetBinding = findSimdBinding(false, lhs.lhs->text);
                else if(lhs.lhs->kind == ExprKind::ThisMember)
                    targetBinding = findSimdBinding(true, lhs.lhs->text);

                if(targetBinding){
                    const MStringView simdType = targetBinding->simdType;
                    const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(simdType);
                    const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(simdType);
                    static constexpr const char* s_LaneSuffix[4] = { "X(", "Y(", "Z(", "W(" };

                    emitIndent(out, indent);
                    out.append(targetBinding->regName.data(), targetBinding->regName.size());
                    out.append(" = ");
                    if(isInt)
                        out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
                    else
                        out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
                    out.append(s_LaneSuffix[lane]);
                    out.append(targetBinding->regName.data(), targetBinding->regName.size());
                    out.append(", ");
                    if(isInt)
                        out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                    if(stmt.expr->op == TokenType::Equal){
                        emitExpr(out, rhs);
                    }
                    else{
                        const MStringView opSym = __hidden_adhd_source_emitter::SimdCompoundScalarOpSymbol(stmt.expr->op);
                        out.push_back('(');
                        emitExpr(out, lhs);
                        out.append(opSym.data(), opSym.size());
                        out.push_back('(');
                        emitExpr(out, rhs);
                        out.append("))");
                    }
                    if(isInt)
                        out.push_back(')');
                    out.append(");\r\n");

                    if(targetBinding->needsStorageSync)
                        emitSimdStoreForBinding(out, *targetBinding, indent);
                    break;
                }
            }

            u32 vecLanes = 0u;
            if(lhs.kind == ExprKind::Index && lhs.lhs && lhs.rhs
                && __hidden_adhd_source_emitter::IsRegisterPromotableSimdType(lhs.lhs->resolvedSimdType)
                && m_symbols.isSimdVectorType(lhs.lhs->resolvedSimdType, vecLanes))
            {
                const SimdVarBinding* targetBinding = nullptr;
                if(lhs.lhs->kind == ExprKind::Identifier)
                    targetBinding = findSimdBinding(false, lhs.lhs->text);
                else if(lhs.lhs->kind == ExprKind::ThisMember)
                    targetBinding = findSimdBinding(true, lhs.lhs->text);

                if(targetBinding){
                    const MStringView simdType = targetBinding->simdType;
                    const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(simdType);
                    const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(simdType);

                    emitIndent(out, indent);
                    out.append(targetBinding->regName.data(), targetBinding->regName.size());
                    out.append(" = ");
                    if(isInt)
                        out.append(isDouble ? "::VectorDoubleSetIntByIndex(" : "::VectorSetIntByIndex(");
                    else
                        out.append(isDouble ? "::VectorDoubleSetByIndex(" : "::VectorSetByIndex(");
                    out.append(targetBinding->regName.data(), targetBinding->regName.size());
                    out.append(", ");
                    if(isInt)
                        out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                    if(stmt.expr->op == TokenType::Equal){
                        emitExpr(out, rhs);
                    }
                    else{
                        const MStringView opSym = __hidden_adhd_source_emitter::SimdCompoundScalarOpSymbol(stmt.expr->op);
                        out.push_back('(');
                        emitExpr(out, lhs);
                        out.append(opSym.data(), opSym.size());
                        out.push_back('(');
                        emitExpr(out, rhs);
                        out.append("))");
                    }
                    if(isInt)
                        out.push_back(')');
                    out.append(", static_cast<usize>(");
                    emitExpr(out, *lhs.rhs);
                    out.append("));\r\n");

                    if(targetBinding->needsStorageSync)
                        emitSimdStoreForBinding(out, *targetBinding, indent);
                    break;
                }
            }
        }

        emitIndent(out, indent);
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(";\r\n");

        if(stmt.expr && stmt.expr->kind == ExprKind::Call){
            for(const Expr* arg : stmt.expr->arguments){
                if(!arg || !arg->isMutArg)
                    continue;
                const SimdVarBinding* mutBinding = nullptr;
                if(arg->kind == ExprKind::Identifier)
                    mutBinding = findSimdBinding(false, arg->text);
                else if(arg->kind == ExprKind::ThisMember)
                    mutBinding = findSimdBinding(true, arg->text);
                if(mutBinding)
                    emitSimdLoadReloadForBinding(out, *mutBinding, indent);
            }
        }
        break;
    }
    case StmtKind::Return:{
        emitSimdBlockStores(out, indent, false);
        if(stmt.expr && __hidden_adhd_source_emitter::IsRegisterPromotableSimdType(stmt.expr->resolvedSimdType)){
            const SimdVarBinding* retBinding = nullptr;
            if(stmt.expr->kind == ExprKind::Identifier)
                retBinding = findSimdBinding(false, stmt.expr->text);
            else if(stmt.expr->kind == ExprKind::ThisMember)
                retBinding = findSimdBinding(true, stmt.expr->text);

            const bool needsRegStore = (retBinding && retBinding->isLocalDecl && !retBinding->needsStorageSync)
                || __hidden_adhd_source_emitter::ExprContainsSimdMath(stmt.expr, m_symbols);
            if(needsRegStore){
                const MStringView simdType = stmt.expr->resolvedSimdType;
                const MStringView storeFn = __hidden_adhd_source_emitter::SimdStoreFunctionCpp(simdType);
                TypeRef retType(m_arena);
                retType.name = simdType;

                emitIndent(out, indent);
                m_headerEmitter.emitTypeCpp(out, retType);
                out.append(" __adhd_simd_ret{};\r\n");
                emitIndent(out, indent);
                out.append(storeFn.data(), storeFn.size());
                out.push_back('(');
                emitSimdRegExpr(out, *stmt.expr, simdType);
                out.append(", __adhd_simd_ret);\r\n");
                emitIndent(out, indent);
                out.append("return __adhd_simd_ret;\r\n");
                break;
            }
        }
        emitIndent(out, indent);
        out.append("return");
        if(stmt.expr){
            out.push_back(' ');
            emitExpr(out, *stmt.expr);
        }
        out.append(";\r\n");
        break;
    }
    case StmtKind::Break:{
        emitSimdBlockStores(out, indent, true);
        emitIndent(out, indent);
        out.append("break;\r\n");
        break;
    }
    case StmtKind::Continue:{
        emitSimdBlockStores(out, indent, true);
        emitIndent(out, indent);
        out.append("continue;\r\n");
        break;
    }
    case StmtKind::If:{
        emitIndent(out, indent);
        out.append("if(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append("){\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        if(!stmt.elseStatements.empty()){
            emitIndent(out, indent);
            out.append("else{\r\n");
            emitStatementList(out, stmt.elseStatements, indent + 1u);
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        break;
    }
    case StmtKind::Switch:{
        if(stmt.lowerSwitchToIfChain){
            bool firstBranch = true;
            for(const SwitchCase& sc : stmt.switchCases){
                emitIndent(out, indent);
                if(sc.isDefault){
                    out.append("else{\r\n");
                }
                else{
                    out.append(firstBranch ? "if(" : "else if(");
                    if(stmt.expr)
                        emitExpr(out, *stmt.expr);
                    out.append(" == ");
                    if(sc.matchExpr)
                        emitExpr(out, *sc.matchExpr);
                    out.append("){\r\n");
                    firstBranch = false;
                }
                for(const Stmt* child : sc.statements){
                    if(child && child->kind != StmtKind::Break)
                        emitStmt(out, *child, indent + 1u);
                }
                emitIndent(out, indent);
                out.append("}\r\n");
            }
        }
        else{
            emitIndent(out, indent);
            out.append("switch(");
            if(stmt.expr)
                emitExpr(out, *stmt.expr);
            out.append("){\r\n");
            for(const SwitchCase& sc : stmt.switchCases){
                emitIndent(out, indent);
                if(sc.isDefault){
                    out.append("default:{\r\n");
                }
                else{
                    out.append("case ");
                    if(sc.matchExpr)
                        emitExpr(out, *sc.matchExpr);
                    out.append(":{\r\n");
                }
                emitStatementList(out, sc.statements, indent + 1u);
                emitIndent(out, indent);
                out.append("}\r\n");
            }
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        break;
    }
    case StmtKind::ForRange:{
        emitIndent(out, indent);
        out.append("for(");
        if(stmt.rangeMode == ParamPassingMode::In)
            out.append("const ");
        if(stmt.varType)
            m_headerEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.append("& ");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" : ");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append("){\r\n");

        const SimdVarBinding* loopBinding = findSimdBinding(false, stmt.name);
        if(loopBinding && loopBinding->isLoopElement){
            const MStringView regType = __hidden_adhd_source_emitter::SimdRegisterCppType(loopBinding->simdType);
            const MStringView loadFn = __hidden_adhd_source_emitter::SimdLoadFunctionCpp(loopBinding->simdType);
            emitIndent(out, indent + 1u);
            if(!loopBinding->written && !loopBinding->needsStorageSync)
                out.append("const ");
            out.append(regType.data(), regType.size());
            out.push_back(' ');
            out.append(loopBinding->regName.data(), loopBinding->regName.size());
            out.append(" = ");
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            out.append(loopBinding->name.data(), loopBinding->name.size());
            out.append(");\r\n");
        }

        emitStatementList(out, stmt.statements, indent + 1u);

        if(loopBinding && loopBinding->isLoopElement && loopBinding->written){
            const bool endsWithTerminal = !stmt.statements.empty() && stmt.statements.back() && (
                stmt.statements.back()->kind == StmtKind::Return
                || stmt.statements.back()->kind == StmtKind::Break
                || stmt.statements.back()->kind == StmtKind::Continue
            );
            if(!endsWithTerminal)
                emitSimdStoreForBinding(out, *loopBinding, indent + 1u);
        }

        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::ParallelForRange:{
        emitIndent(out, indent);
        out.append("(void)__adhd_task_scope.parallelFor(0u, (");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(").size(), 1u, [&](const usize __adhd_idx)noexcept{\r\n");
        emitIndent(out, indent + 1u);
        if(stmt.rangeMode == ParamPassingMode::In)
            out.append("const ");
        if(stmt.varType)
            m_headerEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.append("& ");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = (");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(")[__adhd_idx];\r\n");

        const SimdVarBinding* loopBinding = findSimdBinding(false, stmt.name);
        if(loopBinding && loopBinding->isLoopElement){
            const MStringView regType = __hidden_adhd_source_emitter::SimdRegisterCppType(loopBinding->simdType);
            const MStringView loadFn = __hidden_adhd_source_emitter::SimdLoadFunctionCpp(loopBinding->simdType);
            emitIndent(out, indent + 1u);
            if(!loopBinding->written && !loopBinding->needsStorageSync)
                out.append("const ");
            out.append(regType.data(), regType.size());
            out.push_back(' ');
            out.append(loopBinding->regName.data(), loopBinding->regName.size());
            out.append(" = ");
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            out.append(loopBinding->name.data(), loopBinding->name.size());
            out.append(");\r\n");
        }

        emitStatementList(out, stmt.statements, indent + 1u);

        if(loopBinding && loopBinding->isLoopElement && loopBinding->written)
            emitSimdStoreForBinding(out, *loopBinding, indent + 1u);

        emitIndent(out, indent);
        out.append("}, ");
        __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ForIndex:{
        emitIndent(out, indent);
        out.append("for(");
        if(stmt.initStmt && stmt.initStmt->varType){
            m_headerEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.push_back(' ');
            out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
            out.append(" = ");
            if(stmt.initStmt->expr)
                emitExpr(out, *stmt.initStmt->expr);
        }
        out.append("; ");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append("; ");
        if(stmt.stepExpr)
            emitExpr(out, *stmt.stepExpr);
        out.append("){\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::ParallelForIndex:{
        emitIndent(out, indent);
        out.append("(void)__adhd_task_scope.parallelFor(static_cast<usize>(");
        if(stmt.initStmt && stmt.initStmt->expr)
            emitExpr(out, *stmt.initStmt->expr);
        else
            out.append("0u");
        out.append("), static_cast<usize>(");
        if(stmt.condition && stmt.condition->kind == ExprKind::Binary && stmt.condition->rhs){
            emitExpr(out, *stmt.condition->rhs);
            if(stmt.condition->op == TokenType::LessEqual)
                out.append(" + 1u");
        }
        else if(stmt.condition)
            emitExpr(out, *stmt.condition);
        else
            out.append("0u");
        out.append("), 1u, [&](const usize __adhd_idx)noexcept{\r\n");
        if(stmt.initStmt && stmt.initStmt->varType){
            emitIndent(out, indent + 1u);
            out.append("const ");
            m_headerEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.push_back(' ');
            out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
            out.append(" = static_cast<");
            m_headerEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.append(">(__adhd_idx);\r\n");
        }
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}, ");
        __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::While:{
        emitIndent(out, indent);
        out.append("while(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append("){\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::DoWhile:{
        emitIndent(out, indent);
        out.append("do{\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}while(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ParallelBlock:{
        if(!stmt.parallelDependencies.empty()){
            emitIndent(out, indent);
            out.append("const ::NWB::Core::CpuTaskHandle __adhd_deps_");
            out.append(stmt.name.data(), stmt.name.size());
            out.append("[] = { ");
            for(usize i = 0u; i < stmt.parallelDependencies.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                out.append("__adhd_task_");
                out.append(stmt.parallelDependencies[i].data(), stmt.parallelDependencies[i].size());
            }
            out.append(" };\r\n");
        }
        emitIndent(out, indent);
        out.append("const auto __adhd_submit_");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = __adhd_task_scope.submit([&]()noexcept{\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}, ");
        __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
        if(!stmt.parallelDependencies.empty()){
            out.append(", __adhd_deps_");
            out.append(stmt.name.data(), stmt.name.size());
            out.append(", ");
            AppendAdhdUnsigned(out, stmt.parallelDependencies.size());
            out.push_back('u');
        }
        out.append(");\r\n");
        emitIndent(out, indent);
        out.append("[[maybe_unused]] const ::NWB::Core::CpuTaskHandle __adhd_task_");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = __adhd_submit_");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(".has_value() ? *__adhd_submit_");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" : ::NWB::Core::CpuTaskHandle{};\r\n");
        break;
    }
    case StmtKind::ParallelCall:{
        if(stmt.expr && stmt.expr->kind == ExprKind::Call && !stmt.expr->arguments.empty()){
            emitIndent(out, indent);
            out.append("{\r\n");
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                const Expr* arg = stmt.expr->arguments[i];
                emitIndent(out, indent + 1u);
                if(arg && arg->isMutArg)
                    out.append("auto& __adhd_arg_");
                else
                    out.append("const auto __adhd_arg_");
                AppendAdhdUnsigned(out, i);
                out.append(" = (");
                if(arg)
                    emitExpr(out, *arg);
                out.append(");\r\n");
            }
            emitIndent(out, indent + 1u);
            out.append("(void)__adhd_task_scope.submit([&");
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                const Expr* arg = stmt.expr->arguments[i];
                if(!arg || !arg->isMutArg){
                    out.append(", __adhd_arg_");
                    AppendAdhdUnsigned(out, i);
                }
            }
            out.append("]()noexcept{ ");
            if(stmt.expr->lhs)
                emitExpr(out, *stmt.expr->lhs);
            out.push_back('(');
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                out.append("__adhd_arg_");
                AppendAdhdUnsigned(out, i);
            }
            out.append("); }, ");
            __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
            out.append(");\r\n");
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        else{
            emitIndent(out, indent);
            out.append("(void)__adhd_task_scope.submit([=]()noexcept{ ");
            if(stmt.expr)
                emitExpr(out, *stmt.expr);
            out.append("; }, ");
            __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
            out.append(");\r\n");
        }
        break;
    }
    case StmtKind::NativeBlock:{
        emitIndent(out, indent);
        out.append("{\r\n");
        for(const NativeBinding& binding : stmt.nativeBindings){
            if(binding.sourceExpr && binding.sourceExpr->kind == ExprKind::Identifier && binding.sourceExpr->text == binding.aliasName)
                continue;
            emitIndent(out, indent + 1u);
            if(binding.mode == ParamPassingMode::In)
                out.append("const auto& ");
            else
                out.append("auto& ");
            out.append(binding.aliasName.data(), binding.aliasName.size());
            out.append(" = (");
            if(binding.sourceExpr)
                emitExpr(out, *binding.sourceExpr);
            out.append(");\r\n");
        }
        emitIndent(out, indent + 1u);
        out.append(stmt.nativeCode.data(), stmt.nativeCode.size());
        out.append("\r\n");
        for(const NativeBinding& binding : stmt.nativeBindings){
            if(binding.mode != ParamPassingMode::Mut || !binding.sourceExpr)
                continue;
            const SimdVarBinding* mutBinding = nullptr;
            if(binding.sourceExpr->kind == ExprKind::Identifier)
                mutBinding = findSimdBinding(false, binding.sourceExpr->text);
            else if(binding.sourceExpr->kind == ExprKind::ThisMember)
                mutBinding = findSimdBinding(true, binding.sourceExpr->text);
            if(mutBinding)
                emitSimdLoadReloadForBinding(out, *mutBinding, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::Diagnostic:{
        emitIndent(out, indent);
        switch(stmt.diagnosticKind){
        case DiagnosticKind::Assert:
            if(stmt.arguments.size() > 1u)
                out.append(stmt.isEssential ? "NWB_FATAL_ASSERT_MSG(" : "NWB_ASSERT_MSG(");
            else
                out.append(stmt.isEssential ? "NWB_FATAL_ASSERT(" : "NWB_ASSERT(");
            break;
        case DiagnosticKind::Info:
            out.append(stmt.isEssential ? "NWB_LOGGER_ESSENTIAL_INFO(" : "NWB_LOGGER_INFO(");
            break;
        case DiagnosticKind::Warning:
            out.append(stmt.isEssential ? "NWB_LOGGER_CRITICAL_WARNING(" : "NWB_LOGGER_WARNING(");
            break;
        case DiagnosticKind::Error:
            out.append(stmt.isEssential ? "NWB_LOGGER_FATAL(" : "NWB_LOGGER_ERROR(");
            break;
        }
        for(usize i = 0u; i < stmt.arguments.size(); ++i){
            if(i > 0u)
                out.append(", ");
            if(stmt.arguments[i])
                emitExpr(out, *stmt.arguments[i]);
        }
        out.append(");\r\n");
        break;
    }
    }
}

void SourceEmitter::emitSimdVectorCtorReg(
    MString& out,
    const MStringView simdType,
    const MVector<Expr*>& arguments
)const{
    const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(simdType);
    const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(simdType);

    if(arguments.empty()){
        if(isInt)
            out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
        else
            out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
        return;
    }
    if(arguments.size() == 1u && arguments[0]){
        if(!arguments[0]->resolvedSimdType.empty()){
            emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        if(isInt){
            out.append(isDouble ? "::VectorDoubleReplicateInt(static_cast<u64>(" : "::VectorReplicateInt(static_cast<u32>(");
            emitExpr(out, *arguments[0]);
            out.append("))");
        }
        else{
            out.append(isDouble ? "::VectorDoubleReplicate(" : "::VectorReplicate(");
            emitExpr(out, *arguments[0]);
            out.push_back(')');
        }
        return;
    }

    if(isInt){
        out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
        for(usize i = 0u; i < 4u; ++i){
            if(i > 0u)
                out.append(", ");
            out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
            if(i < arguments.size() && arguments[i])
                emitExpr(out, *arguments[i]);
            else
                out.append("0u");
            out.push_back(')');
        }
        out.push_back(')');
        return;
    }

    out.append(isDouble ? "::VectorDoubleSet(" : "::VectorSet(");
    for(usize i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        if(i < arguments.size() && arguments[i])
            emitExpr(out, *arguments[i]);
        else
            out.append(isDouble ? "0.0" : "0.0f");
    }
    out.push_back(')');
}

void SourceEmitter::emitSimdRegExpr(
    MString& out,
    const Expr& expr,
    const MStringView expectedSimdType
)const{
    const MStringView simdType = !expr.resolvedSimdType.empty() ? expr.resolvedSimdType : expectedSimdType;
    const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(simdType);
    const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(simdType);

    if(expr.resolvedSimdType.empty() && !expectedSimdType.empty()){
        if(isInt){
            out.append(isDouble ? "::VectorDoubleReplicateInt(static_cast<u64>(" : "::VectorReplicateInt(static_cast<u32>(");
            emitExpr(out, expr);
            out.append("))");
        }
        else{
            out.append(isDouble ? "::VectorDoubleReplicate(" : "::VectorReplicate(");
            emitExpr(out, expr);
            out.push_back(')');
        }
        return;
    }

    switch(expr.kind){
    case ExprKind::Identifier:{
        const SimdVarBinding* binding = findSimdBinding(false, expr.text);
        if(binding){
            out.append(binding->regName.data(), binding->regName.size());
            return;
        }
        break;
    }
    case ExprKind::ThisMember:{
        const SimdVarBinding* binding = findSimdBinding(true, expr.text);
        if(binding){
            out.append(binding->regName.data(), binding->regName.size());
            return;
        }
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.isSwizzle && expr.lhs){
            const u32 len = static_cast<u32>(expr.text.size());
            const u32 e0 = len > 0u ? __hidden_adhd_source_emitter::MapSwizzleCharToLane(expr.text[0]) : 0u;
            const u32 e1 = len > 1u ? __hidden_adhd_source_emitter::MapSwizzleCharToLane(expr.text[1]) : 1u;
            const u32 e2 = len > 2u ? __hidden_adhd_source_emitter::MapSwizzleCharToLane(expr.text[2]) : 2u;
            const u32 e3 = len > 3u ? __hidden_adhd_source_emitter::MapSwizzleCharToLane(expr.text[3]) : 3u;
            if(e0 == 0u && e1 == 1u && e2 == 2u && e3 == 3u){
                emitSimdRegExpr(out, *expr.lhs, simdType);
                return;
            }
            out.append(isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
            AppendAdhdUnsigned(out, e0);
            out.append("u, ");
            AppendAdhdUnsigned(out, e1);
            out.append("u, ");
            AppendAdhdUnsigned(out, e2);
            out.append("u, ");
            AppendAdhdUnsigned(out, e3);
            out.append("u>(");
            emitSimdRegExpr(out, *expr.lhs, expr.lhs->resolvedSimdType);
            out.push_back(')');
            return;
        }
        break;
    }
    case ExprKind::Unary:{
        if(expr.lhs){
            if(expr.op == TokenType::Minus){
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Tilde){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(isDouble ? ", ::VectorDoubleTrueInt())" : ", ::VectorTrueInt())");
                return;
            }
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.lhs && expr.rhs){
            const Expr* mulA = nullptr;
            const Expr* mulB = nullptr;
            if(expr.op == TokenType::Plus){
                if(__hidden_adhd_source_emitter::IsSimdMulExpr(expr.lhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.push_back(')');
                    return;
                }
                if(__hidden_adhd_source_emitter::IsSimdMulExpr(expr.rhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    out.push_back(')');
                    return;
                }
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(");
                else
                    out.append(isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Minus){
                if(__hidden_adhd_source_emitter::IsSimdMulExpr(expr.lhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.push_back(')');
                    return;
                }
                if(__hidden_adhd_source_emitter::IsSimdMulExpr(expr.rhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Star){
                if(!expr.lhs->resolvedSimdType.empty() && expr.rhs->resolvedSimdType.empty() && !isInt){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    out.append(", ");
                    emitExpr(out, *expr.rhs);
                    out.push_back(')');
                    return;
                }
                if(expr.lhs->resolvedSimdType.empty() && !expr.rhs->resolvedSimdType.empty() && !isInt){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.append(", ");
                    emitExpr(out, *expr.lhs);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Slash){
                if(!isInt && expr.lhs->resolvedSimdType.empty() && !expr.rhs->resolvedSimdType.empty()
                    && __hidden_adhd_source_emitter::IsUnitFloatLiteral(*expr.lhs))
                {
                    out.append(isDouble ? "::VectorDoubleReciprocal(" : "::VectorReciprocal(");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Ampersand){
                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Pipe){
                out.append(isDouble ? "::VectorDoubleOrInt(" : "::VectorOrInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Caret){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
        }
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr.lhs->text;
            u32 ctorLanes = 0u;
            if(m_symbols.isSimdVectorType(callee, ctorLanes)){
                emitSimdVectorCtorReg(out, simdType, expr.arguments);
                return;
            }

            if(callee == "min" || callee == "max" || callee == "clamp"){
                if(callee == "min")
                    out.append(isDouble ? "::VectorDoubleMin(" : "::VectorMin(");
                else if(callee == "max")
                    out.append(isDouble ? "::VectorDoubleMax(" : "::VectorMax(");
                else
                    out.append(isDouble ? "::VectorDoubleClamp(" : "::VectorClamp(");
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(expr.arguments[i])
                        emitSimdRegExpr(out, *expr.arguments[i], simdType);
                }
                out.push_back(')');
                return;
            }
            if(callee == "abs" || callee == "floor" || callee == "ceil" || callee == "round"
                || callee == "sqrt" || callee == "rsqrt" || callee == "sin" || callee == "cos" || callee == "tan")
            {
                if(callee == "abs")
                    out.append(isDouble ? "::VectorDoubleAbs(" : "::VectorAbs(");
                else if(callee == "floor")
                    out.append(isDouble ? "::VectorDoubleFloor(" : "::VectorFloor(");
                else if(callee == "ceil")
                    out.append(isDouble ? "::VectorDoubleCeiling(" : "::VectorCeiling(");
                else if(callee == "round")
                    out.append(isDouble ? "::VectorDoubleRound(" : "::VectorRound(");
                else if(callee == "sqrt")
                    out.append(isDouble ? "::VectorDoubleSqrt(" : "::VectorSqrt(");
                else if(callee == "rsqrt")
                    out.append(isDouble ? "::VectorDoubleReciprocalSqrt(" : "::VectorReciprocalSqrt(");
                else if(callee == "sin")
                    out.append(isDouble ? "::VectorDoubleSin(" : "::VectorSin(");
                else if(callee == "cos")
                    out.append(isDouble ? "::VectorDoubleCos(" : "::VectorCos(");
                else
                    out.append(isDouble ? "::VectorDoubleTan(" : "::VectorTan(");
                if(!expr.arguments.empty() && expr.arguments[0])
                    emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.push_back(')');
                return;
            }
            if(callee == "lerp" && expr.arguments.size() == 3u && expr.arguments[0] && expr.arguments[1] && expr.arguments[2]){
                const bool vectorT = !expr.arguments[2]->resolvedSimdType.empty();
                if(vectorT)
                    out.append(isDouble ? "::VectorDoubleLerpV(" : "::VectorLerpV(");
                else
                    out.append(isDouble ? "::VectorDoubleLerp(" : "::VectorLerp(");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.arguments[1], simdType);
                out.append(", ");
                if(vectorT)
                    emitSimdRegExpr(out, *expr.arguments[2], simdType);
                else
                    emitExpr(out, *expr.arguments[2]);
                out.push_back(')');
                return;
            }
            if(callee == "cross" && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const u32 effLanes = __hidden_adhd_source_emitter::InferEffectiveSimdLanes(*expr.arguments[0]);
                if(effLanes == 2u)
                    out.append(isDouble ? "::VectorDouble2Cross(" : "::Vector2Cross(");
                else
                    out.append(isDouble ? "::VectorDouble3Cross(" : "::Vector3Cross(");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.arguments[1], simdType);
                out.push_back(')');
                return;
            }
            if(callee == "normalize" && !expr.arguments.empty() && expr.arguments[0]){
                const u32 effLanes = __hidden_adhd_source_emitter::InferEffectiveSimdLanes(*expr.arguments[0]);
                if(effLanes == 2u)
                    out.append(isDouble ? "::VectorDouble2Normalize(" : "::Vector2Normalize(");
                else if(effLanes == 3u)
                    out.append(isDouble ? "::VectorDouble3Normalize(" : "::Vector3Normalize(");
                else
                    out.append(isDouble ? "::VectorDouble4Normalize(" : "::Vector4Normalize(");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.push_back(')');
                return;
            }
            if(callee == "mul" && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const MStringView lhsSimd = expr.arguments[0]->resolvedSimdType;
                const MStringView rhsSimd = expr.arguments[1]->resolvedSimdType;
                const bool lhsIsMat = m_symbols.isSimdMatrixType(lhsSimd);
                const bool rhsIsMat = m_symbols.isSimdMatrixType(rhsSimd);
                if(lhsIsMat && rhsIsMat){
                    out.append(isDouble ? "::MatrixDoubleMultiply(" : "::MatrixMultiply(");
                    emitSimdRegExpr(out, *expr.arguments[0], lhsSimd);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.arguments[1], rhsSimd);
                    out.push_back(')');
                    return;
                }
                const Expr& vecExpr = lhsIsMat ? *expr.arguments[1] : *expr.arguments[0];
                const Expr& matExpr = lhsIsMat ? *expr.arguments[0] : *expr.arguments[1];
                const u32 effLanes = __hidden_adhd_source_emitter::InferEffectiveSimdLanes(vecExpr);
                if(effLanes == 2u)
                    out.append(isDouble ? "::VectorDouble2Transform(" : "::Vector2Transform(");
                else if(effLanes == 3u)
                    out.append(isDouble ? "::VectorDouble3Transform(" : "::Vector3Transform(");
                else
                    out.append(isDouble ? "::VectorDouble4Transform(" : "::Vector4Transform(");
                emitSimdRegExpr(out, vecExpr, vecExpr.resolvedSimdType);
                out.append(", ");
                emitSimdRegExpr(out, matExpr, matExpr.resolvedSimdType);
                out.push_back(')');
                return;
            }
            if(callee == "transpose" && !expr.arguments.empty() && expr.arguments[0]){
                const Expr& arg0 = *expr.arguments[0];
                if(arg0.kind == ExprKind::Call && arg0.lhs && arg0.lhs->kind == ExprKind::Identifier
                    && arg0.lhs->text == "mul" && arg0.arguments.size() == 2u && arg0.arguments[0] && arg0.arguments[1]
                    && m_symbols.isSimdMatrixType(arg0.arguments[0]->resolvedSimdType)
                    && m_symbols.isSimdMatrixType(arg0.arguments[1]->resolvedSimdType))
                {
                    out.append(isDouble ? "::MatrixDoubleMultiplyTranspose(" : "::MatrixMultiplyTranspose(");
                    emitSimdRegExpr(out, *arg0.arguments[0], arg0.arguments[0]->resolvedSimdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *arg0.arguments[1], arg0.arguments[1]->resolvedSimdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::MatrixDoubleTranspose(" : "::MatrixTranspose(");
                emitSimdRegExpr(out, arg0, simdType);
                out.push_back(')');
                return;
            }
            if(callee == "inverse" && !expr.arguments.empty() && expr.arguments[0]){
                out.append(isDouble ? "::MatrixDoubleInverse(nullptr, " : "::MatrixInverse(nullptr, ");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.push_back(')');
                return;
            }
        }
        break;
    }
    default:
        break;
    }

    const MStringView loadFn = __hidden_adhd_source_emitter::SimdLoadFunctionCpp(simdType);
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    emitExpr(out, expr);
    out.push_back(')');
}

void SourceEmitter::emitExpr(MString& out, const Expr& expr)const{
    if(expr.moveAtLastUse){
        out.append("::Move(");
        out.append(expr.text.data(), expr.text.size());
        out.push_back(')');
        return;
    }

    if(__hidden_adhd_source_emitter::IsRegisterPromotableSimdType(expr.resolvedSimdType)){
        const bool isDirectSimdRegOp = (expr.kind == ExprKind::MemberAccess && expr.isSwizzle)
            || expr.kind == ExprKind::Unary
            || (expr.kind == ExprKind::Binary && expr.op != TokenType::Equal)
            || (expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier
                && !__hidden_adhd_source_emitter::MapBuiltinMathFunction(expr.lhs->text).empty());
        if(isDirectSimdRegOp){
            const MStringView simdType = expr.resolvedSimdType;
            const MStringView storeFn = __hidden_adhd_source_emitter::SimdStoreFunctionCpp(simdType);
            TypeRef tmpType(m_arena);
            tmpType.name = simdType;

            out.append("([&]()noexcept{ ");
            m_headerEmitter.emitTypeCpp(out, tmpType);
            out.append(" __adhd_tmp{}; ");
            out.append(storeFn.data(), storeFn.size());
            out.push_back('(');
            emitSimdRegExpr(out, expr, simdType);
            out.append(", __adhd_tmp); return __adhd_tmp; }())");
            return;
        }
    }

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:{
        if(expr.derefNotNull){
            out.append("(*");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::StringLiteral:{
        if(expr.adaptToTChar){
            out.append("NWB_TEXT(\"");
            out.append(expr.text.data(), expr.text.size());
            out.append("\")");
        }
        else{
            out.push_back('"');
            out.append(expr.text.data(), expr.text.size());
            out.push_back('"');
        }
        break;
    }
    case ExprKind::ThisMember:{
        if(expr.derefNotNull){
            out.append("(*this->");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            out.append("this->");
            out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::MemberAccess:{
        u32 lane = 0u;
        if(expr.lhs && __hidden_adhd_source_emitter::IsRegisterPromotableSimdType(expr.lhs->resolvedSimdType)
            && __hidden_adhd_source_emitter::IsSingleSwizzleLane(expr.text, lane))
        {
            const SimdVarBinding* lhsBinding = nullptr;
            if(expr.lhs->kind == ExprKind::Identifier)
                lhsBinding = findSimdBinding(false, expr.lhs->text);
            else if(expr.lhs->kind == ExprKind::ThisMember)
                lhsBinding = findSimdBinding(true, expr.lhs->text);

            if(lhsBinding || __hidden_adhd_source_emitter::ExprContainsSimdMath(expr.lhs, m_symbols)){
                const MStringView lhsSimd = expr.lhs->resolvedSimdType;
                const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(lhsSimd);
                const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(lhsSimd);
                const bool isSignedInt = __hidden_adhd_source_emitter::IsSignedIntSimdType(lhsSimd);
                static constexpr const char* s_LaneSuffix[4] = { "X(", "Y(", "Z(", "W(" };

                if(isSignedInt)
                    out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
                else
                    out.append(isDouble ? "::VectorDoubleGet" : "::VectorGet");
                out.append(s_LaneSuffix[lane]);
                emitSimdRegExpr(out, *expr.lhs, lhsSimd);
                out.push_back(')');
                if(isSignedInt)
                    out.push_back(')');
                break;
            }
        }
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isPointerMemberAccess)
            out.append("->");
        else
            out.push_back('.');
        out.append(expr.text.data(), expr.text.size());
        break;
    }
    case ExprKind::ScopeAccess:{
        for(usize i = 0u; i < expr.scopeChain.size(); ++i){
            if(i > 0u)
                out.append("::");
            out.append(expr.scopeChain[i].data(), expr.scopeChain[i].size());
        }
        break;
    }
    case ExprKind::Index:{
        u32 vecLanes = 0u;
        if(expr.lhs && expr.rhs
            && __hidden_adhd_source_emitter::IsRegisterPromotableSimdType(expr.lhs->resolvedSimdType)
            && m_symbols.isSimdVectorType(expr.lhs->resolvedSimdType, vecLanes))
        {
            const SimdVarBinding* lhsBinding = nullptr;
            if(expr.lhs->kind == ExprKind::Identifier)
                lhsBinding = findSimdBinding(false, expr.lhs->text);
            else if(expr.lhs->kind == ExprKind::ThisMember)
                lhsBinding = findSimdBinding(true, expr.lhs->text);

            if(lhsBinding || __hidden_adhd_source_emitter::ExprContainsSimdMath(expr.lhs, m_symbols)){
                const MStringView lhsSimd = expr.lhs->resolvedSimdType;
                const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(lhsSimd);
                const bool isInt = __hidden_adhd_source_emitter::IsIntSimdType(lhsSimd);
                const bool isSignedInt = __hidden_adhd_source_emitter::IsSignedIntSimdType(lhsSimd);

                if(isSignedInt)
                    out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleGetIntByIndex(" : "::VectorGetIntByIndex(");
                else
                    out.append(isDouble ? "::VectorDoubleGetByIndex(" : "::VectorGetByIndex(");
                emitSimdRegExpr(out, *expr.lhs, lhsSimd);
                out.append(", static_cast<usize>(");
                emitExpr(out, *expr.rhs);
                out.append("))");
                if(isSignedInt)
                    out.push_back(')');
                break;
            }
        }
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back('[');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(']');
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr.lhs->text;
            if((callee == "dot" || callee == "length" || callee == "lengthSq")
                && !expr.arguments.empty() && expr.arguments[0]
                && __hidden_adhd_source_emitter::IsRegisterPromotableSimdType(expr.arguments[0]->resolvedSimdType))
            {
                const MStringView argSimd = expr.arguments[0]->resolvedSimdType;
                const bool isDouble = __hidden_adhd_source_emitter::IsDoubleSimdType(argSimd);
                const u32 effLanes = __hidden_adhd_source_emitter::InferEffectiveSimdLanes(*expr.arguments[0]);
                out.append(isDouble ? "::VectorDoubleGetX(" : "::VectorGetX(");
                if(callee == "dot"){
                    if(effLanes == 2u)
                        out.append(isDouble ? "::VectorDouble2Dot(" : "::Vector2Dot(");
                    else if(effLanes == 3u)
                        out.append(isDouble ? "::VectorDouble3Dot(" : "::Vector3Dot(");
                    else
                        out.append(isDouble ? "::VectorDouble4Dot(" : "::Vector4Dot(");
                    emitSimdRegExpr(out, *expr.arguments[0], argSimd);
                    out.append(", ");
                    if(expr.arguments.size() > 1u && expr.arguments[1])
                        emitSimdRegExpr(out, *expr.arguments[1], argSimd);
                    out.append("))");
                }
                else if(callee == "length"){
                    if(effLanes == 2u)
                        out.append(isDouble ? "::VectorDouble2Length(" : "::Vector2Length(");
                    else if(effLanes == 3u)
                        out.append(isDouble ? "::VectorDouble3Length(" : "::Vector3Length(");
                    else
                        out.append(isDouble ? "::VectorDouble4Length(" : "::Vector4Length(");
                    emitSimdRegExpr(out, *expr.arguments[0], argSimd);
                    out.append("))");
                }
                else{
                    if(effLanes == 2u)
                        out.append(isDouble ? "::VectorDouble2LengthSq(" : "::Vector2LengthSq(");
                    else if(effLanes == 3u)
                        out.append(isDouble ? "::VectorDouble3LengthSq(" : "::Vector3LengthSq(");
                    else
                        out.append(isDouble ? "::VectorDouble4LengthSq(" : "::Vector4LengthSq(");
                    emitSimdRegExpr(out, *expr.arguments[0], argSimd);
                    out.append("))");
                }
                break;
            }
        }

        if(expr.lhs){
            if(expr.lhs->kind == ExprKind::Identifier){
                const MStringView mappedMath = __hidden_adhd_source_emitter::MapBuiltinMathFunction(expr.lhs->text);
                if(!mappedMath.empty()){
                    out.append(mappedMath.data(), mappedMath.size());
                }
                else{
                    TypeRef maybeType(m_arena);
                    maybeType.name = expr.lhs->text;
                    m_headerEmitter.emitTypeCpp(out, maybeType);
                }
            }
            else{
                emitExpr(out, *expr.lhs);
            }
        }
        if(!expr.callTemplateArgs.empty())
            m_headerEmitter.emitTypeArgListCpp(out, expr.callTemplateArgs);
        out.push_back('(');
        u32 ctorVecLanes = 0u;
        const bool isScalarSplatCtor = expr.lhs && expr.lhs->kind == ExprKind::Identifier
            && m_symbols.isSimdVectorType(expr.lhs->text, ctorVecLanes)
            && expr.arguments.size() == 1u && expr.arguments[0]
            && expr.arguments[0]->resolvedSimdType.empty();
        if(isScalarSplatCtor){
            for(u32 lane = 0u; lane < ctorVecLanes; ++lane){
                if(lane > 0u)
                    out.append(", ");
                emitExpr(out, *expr.arguments[0]);
            }
        }
        else{
            for(usize i = 0u; i < expr.arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(expr.arguments[i])
                    emitExpr(out, *expr.arguments[i]);
            }
        }
        out.push_back(')');
        break;
    }
    case ExprKind::Unary:{
        if(expr.isPostfix){
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(expr.text.data(), expr.text.size());
        }
        else{
            out.append(expr.text.data(), expr.text.size());
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.isNotNullRebind){
            out.push_back('(');
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(" = ::MakeNotNull(&(");
            if(expr.rhs)
                emitExpr(out, *expr.rhs);
            out.append(")))");
            break;
        }
        out.push_back('(');
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(' ');
        out.append(expr.text.data(), expr.text.size());
        out.push_back(' ');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Cast:{
        out.append("static_cast<");
        if(expr.targetType)
            m_headerEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::BitCast:{
        out.append("::BitCast<");
        if(expr.targetType)
            m_headerEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Lambda:{
        out.push_back('[');
        for(usize i = 0u; i < expr.lambdaCaptures.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const LambdaCapture& cap = expr.lambdaCaptures[i];
            if(cap.mode == ParamPassingMode::Mut){
                out.push_back('&');
                out.append(cap.name.data(), cap.name.size());
            }
            else{
                out.append(cap.name.data(), cap.name.size());
            }
        }
        out.append("](");
        for(usize i = 0u; i < expr.lambdaParams.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const ParamDecl& p = expr.lambdaParams[i];
            if(p.type)
                m_headerEmitter.emitParamTypeCpp(out, p.mode, *p.type);
            out.push_back(' ');
            out.append(p.name.data(), p.name.size());
        }
        out.append(")noexcept -> ");
        if(expr.lambdaReturnType)
            m_headerEmitter.emitTypeCpp(out, *expr.lambdaReturnType);
        else
            out.append("void");
        out.append("{\r\n");
        emitScopedSimdBody(out, expr.lambdaBody, 2u);
        out.append("    }");
        break;
    }
    }
}

void SourceEmitter::emitIndent(MString& out, const u32 indent)const{
    for(u32 i = 0u; i < indent; ++i)
        out.append("    ");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

