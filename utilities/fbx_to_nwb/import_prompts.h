// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


class ImportPrompts final{
public:
    [[nodiscard]] static Expected<AString> PromptString(AStringView label, AStringView defaultValue, b8& inOutPrompted);
    [[nodiscard]] static b8 PromptBool(AStringView label, b8 defaultValue, Core::Alloc::ScratchArena& scratchArena, b8& inOutPrompted);
    [[nodiscard]] static b8 ValidateOutputOverwrite(const ScratchPath& outputPath, const ImportOptions& options, Core::Alloc::ScratchArena& scratchArena, b8& inOutPrompted);
    [[nodiscard]] static b8 ConfigurePromptsBeforeLoad(const OptionPresence& presence, Core::Alloc::ScratchArena& scratchArena, ImportOptions& inOutOptions, b8& inOutPrompted);
    [[nodiscard]] static b8 ConfigurePromptsAfterLoad(const OptionPresence& presence, Span<const MeshInstance> visibleInstances, Core::Alloc::ScratchArena& scratchArena, ImportOptions& inOutOptions, b8& inOutPrompted);

private:
    [[nodiscard]] static f64 PromptDouble(AStringView label, f64 defaultValue, Core::Alloc::ScratchArena& scratchArena, b8& inOutPrompted);
};


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

