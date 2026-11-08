// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/metascript/global.h>

#include <global/filesystem/path.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AdhdOutputTransaction final : NoCopy{
public:
    AdhdOutputTransaction(Core::Metascript::MetaArena& arena, Span<const Path<Core::Metascript::MetaArena>> outputPaths, Span<const Core::Metascript::MStringView> outputContents, Span<const Core::Metascript::MString> inputs, Core::Metascript::MStringView depfilePath);


public:
    [[nodiscard]] b8 publish();


private:
    void logOutputWriteFailure(usize outputIndex)const;
    void rollbackOutputs()noexcept;


private:
    Span<const Path<Core::Metascript::MetaArena>> m_outputPaths;
    Span<const Core::Metascript::MStringView> m_outputContents;
    Span<const Core::Metascript::MString> m_inputs;
    Core::Metascript::MStringView m_depfilePath;
    Core::Metascript::MVector<b8> m_hadExisting;
    Core::Metascript::MVector<b8> m_hasStaged;
    Core::Metascript::MVector<b8> m_hasBackup;
    Core::Metascript::MVector<b8> m_promoted;
    Core::Metascript::MVector<Path<Core::Metascript::MetaArena>> m_stagedPaths;
    Core::Metascript::MVector<Path<Core::Metascript::MetaArena>> m_backupPaths;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

