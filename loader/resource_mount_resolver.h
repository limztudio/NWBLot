// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/module.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace LoaderDetail{


class ResourceMountResolver{
public:
    inline static constexpr AStringView s_ResourceDirectoryName = "res";
    inline static constexpr AStringView s_GraphicsVolumeName = "graphics";

    struct Error{
        ErrorCode code;
        AStringView stage;
        ::Path<Core::Alloc::ScratchArena> path;
    };

    static Expected<Path, Error> Resolve(Core::Alloc::GlobalArena& arena, Core::Alloc::ScratchArena& scratchArena);
};


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
