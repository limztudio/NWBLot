// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "basis_encoder.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace TexConv{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BasisFloatImage::load(const ::Path<::ArenaBase>& path){
    Core::Alloc::ScratchArena scratch(Name("basis/image_path"));
    AString<Core::Alloc::ScratchArena> text(scratch);
    return basisu::load_image_hdr(GenericNativeTextView(path.native(), text).data(), m_image, false);
}

b8 BasisFloatImage::resampleFrom(const BasisFloatImage& source, const AStringView filter, const u32 channels){
    const AInteropString name(filter);
    return basisu::image_resample(source.m_image, m_image, name.c_str(), 1.0f, false, 0u, channels);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BasisMipEncoder::BasisMipEncoder(const u32 workers, const b8 hdr, const TextureDimension::Enum dimension)
    : m_jobs(workers)
    , m_format(hdr ? basist::basis_tex_format::cUASTC_HDR_4x4 : basist::basis_tex_format::cUASTC_LDR_4x4)
{
    m_params.set_format_mode(m_format);
    m_params.set_srgb_options(false);
    m_params.m_status_output = false;
    m_params.m_compute_stats = false;
    m_params.m_print_stats = false;
    m_params.m_write_output_basis_or_ktx2_files = false;
    m_params.m_create_ktx2_file = false;
    m_params.m_pJob_pool = &m_jobs;
    m_params.m_read_source_images = false;
    m_params.m_mip_gen = false;
    m_params.m_tex_type = dimension == TextureDimension::TextureCube ? basist::cBASISTexTypeCubemapArray
        : dimension == TextureDimension::Texture3D ? basist::cBASISTexTypeVolume : basist::cBASISTexType2D;
}

void BasisMipEncoder::addPlane(const u32 width, const u32 height){
    if(m_format == basist::basis_tex_format::cUASTC_HDR_4x4){
        m_params.m_source_images_hdr.resize(m_params.m_source_images_hdr.size() + 1u);
        m_params.m_source_images_hdr.back().resize(width, height);
    }
    else{
        m_params.m_source_images.resize(m_params.m_source_images.size() + 1u);
        m_params.m_source_images.back().resize(width, height);
    }
}

BasisSliceDescription BasisMipEncoder::slice(const u32 index)const{
    const auto& desc = output().m_slice_desc[index];
    return { desc.m_source_file_index, desc.m_mip_index, desc.m_orig_width, desc.m_orig_height, desc.m_num_blocks_x, desc.m_num_blocks_y };
}
Span<const u8> BasisMipEncoder::sliceBytes(const u32 index)const{
    const auto& data = output().m_slice_image_data[index];
    return { data.get_ptr(), data.size_in_bytes() };
}

b8 InitializeBasisEncoder(){
    static const b8 initialized = basisu::basisu_encoder_init(false);
    return initialized;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

