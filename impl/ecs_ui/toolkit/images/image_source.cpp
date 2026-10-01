// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_source.h"

#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextGeneration{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 NewGeneration(){
    const u64 generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    NWB_FATAL_ASSERT_MSG(generation != 0u, NWB_TEXT("UI image source generation overflow"));
    return generation;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ImageSource::ImageSource(Core::Alloc::GlobalArena& arena, const Texture& texture, ConstructionToken)
    : m_texture(arena, texture.virtualPath())
{
    m_identity.virtualPath = texture.virtualPath();
    Texture::MipLevelVector mipLevels(arena);
    mipLevels.assign(texture.mipLevels().begin(), texture.mipLevels().end());
    Core::Assets::AssetBytes payloadBytes(arena);
    payloadBytes.assign(texture.payloadBytes().begin(), texture.payloadBytes().end());
    m_texture.setPayload(
        texture.colorSpace(),
        texture.hasAlpha(),
        texture.width(),
        texture.height(),
        Move(mipLevels),
        Move(payloadBytes),
        texture.dimension(),
        texture.depth(),
        texture.payloadFormat(),
        texture.alphaMode(),
        texture.alphaConstantUnorm8()
    );
    m_generation = __hidden_ui_image_source::NewGeneration();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedImageSource MakeImageSource(Core::Alloc::GlobalArena& arena, const Texture& texture){
    if(texture.dimension() != TextureDimension::Texture2D || texture.depth() != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("MakeImageSource: texture '{}' must be a static 2D image with depth one")
            , StringConvert(texture.virtualPath().resolvedText())
        );
        return {};
    }
    if(!texture.validatePayload())
        return {};
    return
        SharedImageSource(
            NewArenaObject<RefCounter<ImageSource>>(arena, arena, texture, ImageSource::ConstructionToken{}),
            ArenaRefDeleter<RefCounter<ImageSource>, Core::Alloc::GlobalArena>(&arena),
            AdoptRef
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

