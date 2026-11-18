// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <engine_namespace.h>
#include <global/filesystem.h>
#include <global/simdmath.h>
#include <global/texture_payload.h>

#include <basisu_comp.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace TexConv{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BasisFloatImage{
public:
    u32 width()const{ return m_image.get_width(); }
    u32 height()const{ return m_image.get_height(); }
    void resize(u32 width, u32 height){ m_image.resize(width, height); }
    b8 load(const ::Path<::ArenaBase>& path);
    b8 resampleFrom(const BasisFloatImage& source, AStringView filter, u32 channels);
    SIMDVectorSingle pixel(const u32 x, const u32 y)const{
        const auto& rgba = m_image(x, y);
        return VectorSet(rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    void setPixel(const u32 x, const u32 y, const SIMDVectorSingle color){
        m_image(x, y).set(VectorGetX(color), VectorGetY(color), VectorGetZ(color), VectorGetW(color));
    }
private:
    basisu::imagef m_image;
};

struct BasisSliceDescription{
    u32 sourceIndex;
    u32 mipIndex;
    u32 width;
    u32 height;
    u32 blocksX;
    u32 blocksY;
};

class BasisMipEncoder{
public:
    BasisMipEncoder(u32 workers, b8 hdr, TextureDimension::Enum dimension);
    void addPlane(u32 width, u32 height);
    void setHdrPixel(const u32 plane, const u32 x, const u32 y, const SIMDVectorSingle color){
        m_params.m_source_images_hdr[plane](x, y).set(VectorGetX(color), VectorGetY(color), VectorGetZ(color), VectorGetW(color));
    }
    void setBytePixel(const u32 plane, const u32 x, const u32 y, const Array<u8, 4u>& color){
        m_params.m_source_images[plane](x, y) = basisu::color_rgba(color[0], color[1], color[2], color[3]);
    }
    b8 initialize(){ return m_compressor.init(m_params); }
    i32 process(){ return static_cast<i32>(m_compressor.process()); }
    b8 matchesFormat()const{ return output().m_tex_format == m_format; }
    usize sliceCount()const{ return output().m_slice_desc.size(); }
    usize imageCount()const{ return output().m_slice_image_data.size(); }
    BasisSliceDescription slice(u32 index)const;
    Span<const u8> sliceBytes(u32 index)const;
private:
    const basisu::basisu_backend_output& output()const{ return m_compressor.get_uastc_backend_output(); }

    basisu::job_pool m_jobs;
    basist::basis_tex_format m_format;
    basisu::basis_compressor_params m_params;
    basisu::basis_compressor m_compressor;
};

inline constexpr u32 s_BasisMaxTextureDimension = basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION;
b8 InitializeBasisEncoder();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

