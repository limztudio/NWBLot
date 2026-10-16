// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "encode.h"

#include <core/common/log.h>

#include <global/simdmath.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_UastcHdrMaximum = 65216.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<basisu::imagef> LoadImage(const Path& path){
    basisu::imagef image;
    const AString pathText = PathToGenericString<AString>(path);
    if(!basisu::load_image_hdr(pathText.c_str(), image, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to decode input image '{}'."), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }
    if(image.get_width() == 0u || image.get_height() == 0u || image.get_width() > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION || image.get_height() > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: input image '{}' has an unsupported resolution."), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }
    return image;
}

[[nodiscard]] static SIMDVectorSingle SrgbToLinearRgb(const SIMDVectorSingle rgb)noexcept{
    const SIMDVectorSingle linearPart = VectorScale(rgb, 1.0f / 12.92f);
    const SIMDVectorSingle nonlinearPart = VectorPow(VectorScale(VectorAdd(rgb, VectorReplicate(0.055f)), 1.0f / 1.055f), VectorReplicate(2.4f));
    return VectorSelect(nonlinearPart, linearPart, VectorLessOrEqual(rgb, VectorReplicate(0.04045f)));
}

[[nodiscard]] static bool PrepareLinearPlane(basisu::imagef& plane, const basisu::imagef& alphaMask, const AlphaSource& alphaSource, const bool hdr, const bool srgb){
    const SIMDVectorSingle maximumRgb = VectorReplicate(hdr ? s_UastcHdrMaximum : 1.0f);
    for(u32 y = 0u; y < plane.get_height(); ++y){
        for(u32 x = 0u; x < plane.get_width(); ++x){
            basisu::vec4F& pixel = plane(x, y);
            const SIMDVectorSingle rgb = VectorSet(pixel[0u], pixel[1u], pixel[2u], 0.0f);
            if(!Vector3IsFinite(rgb) || !Vector3GreaterOrEqual(rgb, VectorZero()) || !Vector3LessOrEqual(rgb, maximumRgb)){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: RGB input must contain finite values in [0, {}]."), hdr ? s_UastcHdrMaximum : 1.0f);
                return false;
            }

            f32 alpha = pixel[3u];
            if(alphaSource.mode == AlphaSourceMode::Constant)
                alpha = alphaSource.constant;
            else if(alphaSource.mode == AlphaSourceMode::Image)
                alpha = alphaMask(x, y)[0u];
            if(!IsFinite(alpha)){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha input contains a non-finite value."));
                return false;
            }
            if(alphaSource.mode != AlphaSourceMode::Original)
                alpha = VectorGetX(VectorSaturate(VectorReplicate(alpha)));
            else if(alpha < 0.0f || alpha > 1.0f){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: original alpha input must be in [0, 1]."));
                return false;
            }

            const SIMDVectorSingle linearRgb = srgb ? SrgbToLinearRgb(rgb) : rgb;
            pixel.set(VectorGetX(linearRgb), VectorGetY(linearRgb), VectorGetZ(linearRgb), alpha);
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PreparedTexture> PrepareTexture(Core::Alloc::ScratchArena& scratchArena, const Span<const Path> inputPaths, const TextureDimension::Enum dimension, const bool srgb, const AlphaSource& alphaSource){
    if(inputPaths.empty() || inputPaths.size() > Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});
    if(
        !IsValidTextureDimension(dimension)
        || (dimension == TextureDimension::Texture2D && inputPaths.size() != 1u)
        || (dimension == TextureDimension::TextureCube && inputPaths.size() != TextureFormat::s_TextureCubeFaceCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: texture dimension and input image count do not match."));
        return MakeUnexpected(Failure{});
    }
    if(alphaSource.mode != AlphaSourceMode::Original && alphaSource.mode != AlphaSourceMode::Constant && alphaSource.mode != AlphaSourceMode::Image){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported alpha source."));
        return MakeUnexpected(Failure{});
    }

    const bool hdr = IsHdrInputPath(inputPaths.front());
    for(const Path& path : inputPaths){
        if(IsHdrInputPath(path) != hdr){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: an individual texture conversion cannot mix HDR and LDR source images."));
            return MakeUnexpected(Failure{});
        }
    }

    TextureEncodeInput input;
    input.dimension = dimension;
    input.depth = dimension == TextureDimension::Texture3D ? static_cast<u32>(inputPaths.size()) : 1u;
    input.format = hdr ? TexturePayloadFormat::UastcHdr4x4 : TexturePayloadFormat::UastcLdr4x4;
    input.planes.reserve(inputPaths.size());
    basisu::imagef alphaMask;
    if(alphaSource.mode == AlphaSourceMode::Image){
        auto decoded = __hidden_encode::LoadImage(alphaSource.path);
        if(!decoded)
            return MakeUnexpected(Failure{});
        alphaMask = Move(*decoded);
    }

    for(const Path& path : inputPaths){
        auto plane = __hidden_encode::LoadImage(path);
        if(!plane)
            return MakeUnexpected(Failure{});
        if(input.planes.empty()){
            input.width = plane->get_width();
            input.height = plane->get_height();
            if(dimension == TextureDimension::TextureCube && input.width != input.height){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: cubemap faces must be square."));
                return MakeUnexpected(Failure{});
            }
            if(alphaSource.mode == AlphaSourceMode::Image && (alphaMask.get_width() != input.width || alphaMask.get_height() != input.height)){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image resolution must match the texture input resolution."));
                return MakeUnexpected(Failure{});
            }
        }
        else if(plane->get_width() != input.width || plane->get_height() != input.height){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: texture inputs must have the same resolution."));
            return MakeUnexpected(Failure{});
        }
        if(!__hidden_encode::PrepareLinearPlane(*plane, alphaMask, alphaSource, hdr, !hdr && srgb))
            return MakeUnexpected(Failure{});
        input.planes.push_back(Move(*plane));
    }

    return EncodeLinearTexture(Move(input), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

