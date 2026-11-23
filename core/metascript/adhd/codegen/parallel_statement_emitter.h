// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ParallelStatementEmitter final{
public:
    class BlockTaskScopeTracker final{
    public:
        BlockTaskScopeTracker(const SourceEmitter& emitter, Span<const Stmt* const> statements)noexcept;
        ~BlockTaskScopeTracker()noexcept;

        void beforeStatement(MString& out, usize index, u32 indent);
        void afterStatement(MString& out, const Stmt& stmt, Span<const Stmt* const> remaining, u32 indent);


    private:
        const SourceEmitter& m_emitter;
        b8 m_prevHasActiveTaskScope = false;
        b8 m_pendingLocalScope = false;
        usize m_firstTaskStmtIndex = 0u;
    };


public:
    explicit ParallelStatementEmitter(const SourceEmitter& emitter)noexcept : m_emitter(emitter){}
    void emitTaskSchedulerPrologue(MString& out, u32 indent)const;
    void emitTaskScopeDecl(MString& out, u32 indent)const;
    void emitTaskScopeJoinGuard(MString& out, u32 indent, u32 guardId)const;
    void emitLoopBodyTaskGuardIfNeeded(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    [[nodiscard]] static b8 StmtUsesTaskScope(const Stmt& stmt)noexcept;
    [[nodiscard]] static b8 StatementsContainAsyncParallelStmt(Span<const Stmt* const> statements)noexcept;
    [[nodiscard]] static b8 NeedsLocalTaskScopeInList(Span<const Stmt* const> statements, usize& outFirstTaskStmtIndex)noexcept;
    [[nodiscard]] b8 tryEmitParallelWaitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitParallelStmt(MString& out, const Stmt& stmt, u32 indent)const;

private:
    const SourceEmitter& m_emitter;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

