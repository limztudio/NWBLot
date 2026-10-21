// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One isolated temporary directory per compiler invocation, plus the BOM-stripped source overlay the compiler reads when inputs carry a UTF-8 BOM.
class SlangCompilerWorkspace final : NoCopy{
public:
    using ScratchPath = ::Path<Core::Alloc::ScratchArena>;
    using ScratchString = AString<Core::Alloc::ScratchArena>;
    template<typename T>
    using ScratchVector = Vector<T, Core::Alloc::ScratchArena>;


public:
    // The common path allocates the source path last, so it is declared last to be released first from scratch.
    struct CompilerInputs final{
        ScratchVector<ScratchPath> includeDirectories;
        ScratchPath sourcePath;
    };

    // Removes the work directory when the invocation ends; a cleanup failure is reported, not returned.
    class ScopedDirectoryCleanupGuard final : NoCopy{
    public:
        explicit ScopedDirectoryCleanupGuard(const ScratchPath& path)noexcept
            : m_path(path)
        {}
        ~ScopedDirectoryCleanupGuard()noexcept;


    private:
        const ScratchPath& m_path;
    };


private:
    [[nodiscard]] static bool WriteBomStrippedCompilerInput(ScratchString& source, const Path& sourcePath, const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, const ScratchPath& outputPath, Core::Alloc::ScratchArena& scratchArena);


public:
    [[nodiscard]] static Expected<ScratchPath> CreateCompilerWorkDirectory(const ScratchPath& parentDirectory, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static Expected<usize> CompilerPathTextCapacity(const ScratchPath& path);
    [[nodiscard]] static Expected<ScratchString> BuildCompilerPathText(const ScratchPath& path, Core::Alloc::ScratchArena& scratchArena);
    // Each absolute root gets its own encoded overlay directory so nested relative includes keep resolving within that root.
    [[nodiscard]] static Expected<ScratchPath> BuildCompilerOverlayPath(const ScratchPath& overlayRoot, const ScratchPath& absolutePath, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static bool IsCompilerDependency(const ShaderCook::CookVector<Path>& dependencies, const ScratchPath& absolutePath)noexcept;
    [[nodiscard]] static Expected<CompilerInputs> PrepareBomStrippedCompilerInputs(const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, Core::Alloc::ScratchArena& scratchArena);


public:
    SlangCompilerWorkspace() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

