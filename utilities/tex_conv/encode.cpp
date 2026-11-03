// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "encode.h"

#include <core/common/log.h>

#include <global/simdmath.h>
#include <global/simplemath.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<basisu::imagef> LoadImage(const ScratchPath& path, Core::Alloc::ScratchArena& scratchArena){
    basisu::imagef image;
    const auto pathText = PathToGenericString(scratchArena, path);
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
    const SIMDVectorSingle linearPart = VectorScale(rgb, 1.0f / s_SrgbLinearSegmentScale);
    const SIMDVectorSingle nonlinearPart = VectorPow(VectorScale(VectorAdd(rgb, VectorReplicate(s_SrgbOffset)), 1.0f / s_SrgbOffsetScale), VectorReplicate(s_SrgbDecodeExponent));
    return VectorSelect(nonlinearPart, linearPart, VectorLessOrEqual(rgb, VectorReplicate(s_SrgbLinearThreshold)));
}

[[nodiscard]] static b8 PrepareLinearPlane(const basisu::imagef& alphaMask, const AlphaSource& alphaSource, const b8 hdr, const b8 srgb, basisu::imagef& inOutPlane){
    const f32 maxRgb = ResolveMaxTexConvRgb(hdr);
    for(u32 y = 0u; y < inOutPlane.get_height(); ++y){
        for(u32 x = 0u; x < inOutPlane.get_width(); ++x){
            basisu::vec4F& pixel = inOutPlane(x, y);
            const Float4 rgbVec(pixel[0u], pixel[1u], pixel[2u], 0.0f);
            if(!ValidateTexConvRgbPixel(rgbVec, maxRgb))
                return false;
            const SIMDVectorSingle rgb = LoadFloat(rgbVec);

            const f32 maskAlpha = alphaSource.mode == AlphaSourceMode::Image ? alphaMask(x, y)[0u] : 0.0f;
            const auto alpha = TryResolveTexConvPixelAlpha(alphaSource.mode, pixel[3u], alphaSource.constant, maskAlpha);
            if(!alpha)
                return false;

            const SIMDVectorSingle linearRgb = srgb ? SrgbToLinearRgb(rgb) : rgb;
            pixel.set(VectorGetX(linearRgb), VectorGetY(linearRgb), VectorGetZ(linearRgb), *alpha);
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PreparedTexture> PrepareTexture(const Span<const ScratchPath> inputPaths, const TextureDimension::Enum dimension, const b8 srgb, const AlphaSource& alphaSource, Core::Alloc::ScratchArena& scratchArena){
    if(inputPaths.empty() || inputPaths.size() > Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});
    if(!IsValidTextureDimension(dimension) || (dimension == TextureDimension::Texture2D && inputPaths.size() != 1u) || (dimension == TextureDimension::TextureCube && inputPaths.size() != TextureFormat::s_TextureCubeFaceCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: texture dimension and input image count do not match."));
        return MakeUnexpected(Failure{});
    }
    if(!IsValidAlphaSourceMode(alphaSource.mode)){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported alpha source."));
        return MakeUnexpected(Failure{});
    }

    const b8 hdr = IsHdrInputPath(inputPaths.front());
    for(const ScratchPath& path : inputPaths){
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
        auto decoded = __hidden_encode::LoadImage(alphaSource.path, scratchArena);
        if(!decoded)
            return MakeUnexpected(Failure{});
        alphaMask = Move(*decoded);
    }

    for(const ScratchPath& path : inputPaths){
        auto plane = __hidden_encode::LoadImage(path, scratchArena);
        if(!plane)
            return MakeUnexpected(Failure{});
        if(!ValidateTexConvPlaneResolution(
            input.planes.empty(),
            dimension == TextureDimension::TextureCube,
            alphaSource.mode == AlphaSourceMode::Image,
            plane->get_width(),
            plane->get_height(),
            alphaMask.get_width(),
            alphaMask.get_height(),
            input.width,
            input.height
        ))
            return MakeUnexpected(Failure{});
        if(!__hidden_encode::PrepareLinearPlane(alphaMask, alphaSource, hdr, !hdr && srgb, *plane))
            return MakeUnexpected(Failure{});
        input.planes.push_back(Move(*plane));
    }

    return EncodeLinearTexture(Move(input), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

