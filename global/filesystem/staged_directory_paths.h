// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "path.h"

#include "../basic_string.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
struct StagedDirectoryPaths{
private:
    static constexpr char s_StagedDirectoryPrefix = '.';
    static constexpr AStringView s_StageDirectorySuffix = "_stage";
    static constexpr AStringView s_BackupDirectorySuffix = "_backup";
    static constexpr usize s_StageDirectorySuffixLength = s_StageDirectorySuffix.size();
    static constexpr usize s_BackupDirectorySuffixLength = s_BackupDirectorySuffix.size();
    static constexpr usize s_StageDirectoryNameExtraCharacters = 1u + s_StageDirectorySuffixLength;
    static constexpr usize s_BackupDirectoryNameExtraCharacters = 1u + s_BackupDirectorySuffixLength;


public:
    Path<ArenaT> stageDirectory;
    Path<ArenaT> backupDirectory;


public:
    explicit StagedDirectoryPaths(ArenaT& arena)noexcept(noexcept(decltype(stageDirectory)(arena)) && noexcept(decltype(backupDirectory)(arena)))
        : stageDirectory(arena)
        , backupDirectory(arena)
    {}

    template<typename TempArenaT>
    [[nodiscard]] static StagedDirectoryPaths Create(TempArenaT& tempArena, const Path<ArenaT>& outputDirectory, const AStringView stageToken){
        ArenaT& pathArena = outputDirectory.arena();
        const Path<ArenaT> outputParentDirectory = outputDirectory.parentPath();
        const Path<ArenaT> stageBaseDirectory = outputParentDirectory.empty() ? outputDirectory : outputParentDirectory;

        StagedDirectoryPaths<ArenaT> output(pathArena);

        AString<TempArenaT> stageDirectoryName{tempArena};
        stageDirectoryName.reserve(stageToken.size() + s_StageDirectoryNameExtraCharacters);
        stageDirectoryName += s_StagedDirectoryPrefix;
        stageDirectoryName += stageToken;
        stageDirectoryName += s_StageDirectorySuffix;
        output.stageDirectory = stageBaseDirectory / stageDirectoryName;

        AString<TempArenaT> backupDirectoryName{tempArena};
        backupDirectoryName.reserve(stageToken.size() + s_BackupDirectoryNameExtraCharacters);
        backupDirectoryName += s_StagedDirectoryPrefix;
        backupDirectoryName += stageToken;
        backupDirectoryName += s_BackupDirectorySuffix;
        output.backupDirectory = stageBaseDirectory / backupDirectoryName;
        return output;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

