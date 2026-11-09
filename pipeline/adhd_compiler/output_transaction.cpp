// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "output_transaction.h"

#include <core/common/log.h>

#include <global/filesystem.h>
#include <global/filesystem/staged_file_writer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AdhdOutputTransaction::AdhdOutputTransaction(Core::Metascript::MetaArena& arena, const Span<const Path<Core::Metascript::MetaArena>> outputPaths, const Span<const Core::Metascript::MStringView> outputContents, const Span<const Core::Metascript::MString> inputs, const Core::Metascript::MStringView depfilePath)
    : m_outputPaths(outputPaths)
    , m_outputContents(outputContents)
    , m_inputs(inputs)
    , m_depfilePath(depfilePath)
    , m_hadExisting(arena)
    , m_hasStaged(arena)
    , m_hasBackup(arena)
    , m_promoted(arena)
    , m_stagedPaths(arena)
    , m_backupPaths(arena)
{
    const usize totalOutputs = m_outputPaths.size();
    m_hadExisting.resize(totalOutputs, false);
    m_hasStaged.resize(totalOutputs, false);
    m_hasBackup.resize(totalOutputs, false);
    m_promoted.resize(totalOutputs, false);
    m_stagedPaths.reserve(totalOutputs);
    m_backupPaths.reserve(totalOutputs);
    for(usize k = 0u; k < totalOutputs; ++k){
        m_stagedPaths.emplace_back(arena);
        m_backupPaths.emplace_back(arena);
    }
}


b8 AdhdOutputTransaction::publish(){
    const usize totalOutputs = m_outputPaths.size();
    for(usize k = 0u; k < totalOutputs; ++k){
        if(!CanRepresentStreamSize(static_cast<u64>(m_outputContents[k].size()))){
            logOutputWriteFailure(k);
            return false;
        }
        const auto exists = FileExistsNoFollow(m_outputPaths[k]);
        if(!exists){
            logOutputWriteFailure(k);
            return false;
        }
        if(*exists){
            const auto regular = IsRegularFile(m_outputPaths[k]);
            if(!regular || !*regular){
                logOutputWriteFailure(k);
                return false;
            }
            m_hadExisting[k] = true;
        }
    }

    for(usize k = 0u; k < totalOutputs; ++k){
        const auto staged = StagedFileWriter(m_outputPaths[k]).stageUnique(m_outputContents[k].data(), m_outputContents[k].size());
        if(!staged){
            logOutputWriteFailure(k);
            rollbackOutputs();
            return false;
        }
        m_stagedPaths[k] = *staged;
        m_hasStaged[k] = true;
    }

    for(usize k = 0u; k < totalOutputs; ++k){
        if(!m_hadExisting[k])
            continue;
        const auto stillExists = FileExistsNoFollow(m_outputPaths[k]);
        if(!stillExists){
            logOutputWriteFailure(k);
            rollbackOutputs();
            return false;
        }
        if(!*stillExists){
            m_hadExisting[k] = false;
            continue;
        }
        const auto reservedBackup = StagedFileWriter(m_outputPaths[k]).stageUnique(nullptr, 0u, FOUNDATION_TEXT("backup"));
        if(!reservedBackup){
            logOutputWriteFailure(k);
            rollbackOutputs();
            return false;
        }
        const auto backedUp = RenamePath(m_outputPaths[k], *reservedBackup);
        if(!backedUp){
            const auto removedBackup = RemoveFile(*reservedBackup);
            if(!removedBackup)
                NWB_LOGGER_WARNING(FOUNDATION_TEXT("AdhdCompiler: failed removing backup reservation '{}'"), PathToString<tchar>(*reservedBackup));
            logOutputWriteFailure(k);
            rollbackOutputs();
            return false;
        }
        m_backupPaths[k] = *reservedBackup;
        m_hasBackup[k] = true;
    }

    for(usize k = 0u; k < totalOutputs; ++k){
        const auto finalized = RenamePath(m_stagedPaths[k], m_outputPaths[k]);
        if(!finalized){
            logOutputWriteFailure(k);
            rollbackOutputs();
            return false;
        }
        m_hasStaged[k] = false;
        m_promoted[k] = true;
    }

    for(usize k = 0u; k < totalOutputs; ++k){
        if(!m_hasBackup[k])
            continue;
        const auto removedBackup = RemoveFile(m_backupPaths[k]);
        if(!removedBackup)
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("AdhdCompiler: failed removing backup '{}'"), PathToString<tchar>(m_backupPaths[k]));
    }
    return true;
}


void AdhdOutputTransaction::logOutputWriteFailure(const usize outputIndex)const{
    if(!m_depfilePath.empty() && outputIndex + 1u == m_outputPaths.size())
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to write depfile '{}'"), ConvertString(m_depfilePath));
    else
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to write generated output for '{}'"), ConvertString(m_inputs[outputIndex / 2u]));
}

void AdhdOutputTransaction::rollbackOutputs()noexcept{
    const usize totalOutputs = m_outputPaths.size();
    for(usize rev = totalOutputs; rev > 0u; --rev){
        const usize k = rev - 1u;
        if(m_promoted[k]){
            const auto removed = RemoveFile(m_outputPaths[k]);
            if(!removed)
                NWB_LOGGER_WARNING(FOUNDATION_TEXT("AdhdCompiler: rollback failed removing '{}'"), PathToString<tchar>(m_outputPaths[k]));
        }
        if(m_hasBackup[k]){
            const auto restored = RenamePath(m_backupPaths[k], m_outputPaths[k]);
            if(!restored)
                NWB_LOGGER_WARNING(FOUNDATION_TEXT("AdhdCompiler: rollback failed restoring '{}'"), PathToString<tchar>(m_outputPaths[k]));
        }
        if(m_hasStaged[k]){
            const auto removed = RemoveFile(m_stagedPaths[k]);
            if(!removed)
                NWB_LOGGER_WARNING(FOUNDATION_TEXT("AdhdCompiler: rollback failed removing staged '{}'"), PathToString<tchar>(m_stagedPaths[k]));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

