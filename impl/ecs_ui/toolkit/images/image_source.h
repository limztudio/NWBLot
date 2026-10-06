// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_texture/asset.h>

#include <core/assets/ref.h>

#include <global/arena_object.h>
#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ImageSource;

using SharedImageSource = RefCountPtr<
    RefCounter<ImageSource>,
    ArenaRefDeleter<RefCounter<ImageSource>, Core::Alloc::GlobalArena>
>;


// Each immutable version owns its exact texture payload; the caller's arena outlives every retained reference.
class ImageSource : NoCopy{
    friend class ::RefCounter<ImageSource>;
    friend SharedImageSource MakeImageSource(Core::Alloc::GlobalArena& arena, const Texture& texture);


private:
    class ConstructionToken final{
        friend SharedImageSource MakeImageSource(Core::Alloc::GlobalArena& arena, const Texture& texture);


    private:
        explicit ConstructionToken()noexcept = default;
    };


private:
    ImageSource(Core::Alloc::GlobalArena& arena, const Texture& texture, ConstructionToken);


public:
    ImageSource(ImageSource&&) = delete;
    ImageSource& operator=(ImageSource&&) = delete;


public:
    [[nodiscard]] const Core::Assets::AssetRef<Texture>& identity()const noexcept{ return m_identity; }
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    [[nodiscard]] const Texture& texture()const noexcept{ return m_texture; }


private:
    Core::Assets::AssetRef<Texture> m_identity;
    u64 m_generation = 0u;
    Texture m_texture;
};

// Copies a validated static 2D texture and internally assigns a fresh process-wide generation.
[[nodiscard]] SharedImageSource MakeImageSource(Core::Alloc::GlobalArena& arena, const Texture& texture);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

