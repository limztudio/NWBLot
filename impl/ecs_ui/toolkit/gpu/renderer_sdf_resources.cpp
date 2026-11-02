// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_sdf_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ValidPage(const SharedSdfAtlasPage& page)noexcept{
    return page && ValidSdfAtlasPageBinding(page->binding(), page->pixels().size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRendererState::ValidateSdfPages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCount(snapshot))
        return false;
    const auto& pages = snapshot.sdfPages();
    for(usize index = 0u; index < pages.size(); ++index){
        if(!__hidden_ui_gpu_sdf_resources::ValidPage(pages[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(SameSdfAtlasPageKey(pages[index]->binding(), pages[previous]->binding()))
                return false;
        }
    }
    for(const DrawCommand& command : snapshot.commands()){
        const u32 channelCount = command.sdfPageIndex < pages.size() ? pages[command.sdfPageIndex]->binding().channelCount : 0u;
        if(!ValidDrawCommandSdfBinding(command, pages.size(), channelCount))
            return false;
    }
    return true;
}

Expected<GpuVersion<GpuSdfAtlasVersion>> GpuRendererState::prepareSdfPage(Core::Alloc::ScratchArena& scratchArena, const SharedSdfAtlasPage& page){
    for(usize index = 0u; index < m_sdfCache.size(); ++index){
        const auto& existing = m_sdfCache[index];
        if(!SameSdfAtlasPageKey(existing->m_page->binding(), page->binding()))
            continue;
        if(!(existing->m_page->binding() == page->binding()))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuSdfAtlasVersion> reused = existing;
        m_sdfCache.erase(m_sdfCache.begin() + static_cast<isize>(index));
        m_sdfCache.push_back(reused);
        if(!PrepareGpuSampledImageDescriptor(reused->m_graphics, reused->m_texture, reused->m_texture->getDescription().format, reused->m_sampledImage))
            return MakeUnexpected(Failure{});
        return Move(reused);
    }
    Core::Device& device = m_graphics.getDevice();
    const SdfAtlasPageBinding& binding = page->binding();
    constexpr Array<Core::Format::Enum, 4> s_Formats{ Core::Format::R8_UNORM, Core::Format::RG8_UNORM, Core::Format::RGB8_UNORM, Core::Format::RGBA8_UNORM };
    Core::Format::Enum format = s_Formats[binding.channelCount - 1u];
    u32 uploadChannels = binding.channelCount;
    if(binding.channelCount == s_GpuRgbChannelCount && (device.queryFormatSupport(format) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport){
        format = Core::Format::RGBA8_UNORM;
        uploadChannels = s_GpuRgbaChannelCount;
    }
    if((device.queryFormatSupport(format) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport)
        return MakeUnexpected(Failure{});
    auto versionResult = MakeGpuVersion<GpuSdfAtlasVersion>(m_arena, m_graphics, page);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuSdfAtlasVersion> version = Move(*versionResult);
    Vector<u8, Core::Alloc::ScratchArena> expanded(scratchArena);
    NotNull<const u8*> uploadData(page->pixels().data());
    usize uploadBytes = page->pixels().size();
    if(uploadChannels != binding.channelCount){
        const usize texels = static_cast<usize>(binding.width) * binding.height;
        expanded.resize(texels * uploadChannels, 0u);
        for(usize texel = 0u; texel < texels; ++texel)
            NWB_MEMCPY(expanded.data() + texel * uploadChannels, uploadChannels, uploadData.get() + texel * binding.channelCount, binding.channelCount);
        uploadData = expanded.data();
        uploadBytes = expanded.size();
    }
    if(!CreateAndUploadGpuPageTexture(
        m_graphics,
        GpuPageTextureUploadDesc{
            .name = s_GpuSdfPageTextureName,
            .width = binding.width,
            .height = binding.height,
            .format = format,
            .data = uploadData.get(),
            .dataSize = uploadBytes,
            .rowPitch = static_cast<usize>(binding.width) * uploadChannels,
        },
        version->m_texture,
        version->m_readinessToken
    ))
        return MakeUnexpected(Failure{});
    // Native accepted upload commands retain their destination image until the physical queue completes.
    // Frame owners retain sampled descriptors; cache eviction therefore releases neither a live image nor a live draw view.
    if(m_sdfCache.size() + m_glyphCache.size() + m_textureCache.size() == s_PaintMaxImages)
        evictImageCacheEntry();
    m_sdfCache.push_back(version);
    // Preserve every accepted upload before descriptor publication; retrying a failed descriptor never uploads again.
    if(!PrepareGpuSampledImageDescriptor(version->m_graphics, version->m_texture, version->m_texture->getDescription().format, version->m_sampledImage))
        return MakeUnexpected(Failure{});
    return Move(version);
}

b8 GpuRendererState::prepareSdfPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.sdfPages();
    frame.m_sdfPages.reserve(pages.size());
    Core::Alloc::ScratchArena scratchArena(s_GpuSdfUploadArenaName);
    while(frame.m_sdfPages.size() < pages.size()){
        auto version = prepareSdfPage(scratchArena, pages[frame.m_sdfPages.size()]);
        if(!version)
            return false;
        frame.m_sdfPages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSampledGraphResources> GpuRendererState::declareSdfPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_sdfPages.size() != frame->m_snapshot.sdfPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared SDF page count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_sdfPages.size(); ++index){
        const auto& page = frame->m_sdfPages[index];
        const auto texture = ImportGpuSampledTexture(
            GpuSampledTextureImportDesc{
                .index = index,
                .readyPrefix = s_GpuSdfPageReadyPrefix,
                .readyMarkerLabel = s_GpuSdfPageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("SDF page"),
                .readinessToken = page->m_readinessToken,
                .texturePrefix = s_GpuSdfPagePrefix,
                .textureMarkerLabel = s_GpuSdfPageMarkerLabel,
                .texture = page->m_texture,
            },
            graph,
            uses
        );
        if(!texture){
            if(texture.error() == GpuSampledTextureImportError::Texture)
                NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: SDF page {} texture import failed (atlas={}, group={}, generation={})"), index, page->m_page->binding().atlasIdentity, page->m_page->binding().index, page->m_page->binding().generation);
            return MakeUnexpected(Failure{});
        }
        resources->push_back(*texture);
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

