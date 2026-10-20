// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_sampler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ReductionAssignmentPrefix = "asset.reduction";
constexpr AStringView s_UnsupportedMetadataCase = "sampler_unsupported_metadata";
constexpr AStringView s_AssetsDirectory = "assets";
constexpr AStringView s_SamplersDirectory = "samplers";
constexpr AStringView s_LinearClampMetadataFilename = "linear_clamp.nwb";

using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_SamplerTestMetadata =
    "sampler asset;\n\n"
    "asset.min_filter = \"nearest\";\n"
    "asset.mag_filter = \"linear\";\n"
    "asset.mip_filter = \"nearest\";\n"
    "asset.address_u = \"wrap\";\n"
    "asset.address_v = \"mirror\";\n"
    "asset.address_w = \"border\";\n"
    "asset.reduction = \"standard\";\n"
    "asset.max_anisotropy = 4.0;\n"
    "asset.mip_bias = -0.25;\n"
;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, SamplerCodecRejectsUnsupportedReductionAndFixedBorderColor){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);

    TestArena testArena;
    NWB::Impl::Sampler sampler(testArena.arena, Name("project/samplers/binary_admission"));
    NWB::Core::SamplerDesc description;
    description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    description.addressW = NWB::Core::SamplerAddressMode::Border;
    sampler.setDescription(description);

    NWB::Impl::SamplerAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(sampler, binary));
    ASSERT_EQ(binary.size(), sizeof(NWB::Impl::SamplerBinaryPayload::HeaderBinary));
    const NWB::Core::Assets::AssetBytes validBinary(binary);

    for(const NWB::Core::SamplerReductionType::Enum reduction : { NWB::Core::SamplerReductionType::Minimum, NWB::Core::SamplerReductionType::Maximum }){
        binary = validBinary;
        const u32 reductionValue = static_cast<u32>(reduction);
        NWB_MEMCPY(binary.data() + offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, reductionType), sizeof(reductionValue), &reductionValue, sizeof(reductionValue));
        EXPECT_FALSE(codec.deserialize(testArena.arena, sampler.virtualPath(), binary));

        description.reductionType = reduction;
        sampler.setDescription(description);
        EXPECT_FALSE(codec.serialize(sampler, binary));
    }

    for(const usize borderOffset : {
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorR),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorG),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorB),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorA)
    }){
        binary = validBinary;
        const f32 nonblack = 1.0f;
        NWB_MEMCPY(binary.data() + borderOffset, sizeof(nonblack), &nonblack, sizeof(nonblack));
        EXPECT_FALSE(codec.deserialize(testArena.arena, sampler.virtualPath(), binary));
    }

    description.reductionType = NWB::Core::SamplerReductionType::Standard;
    description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 1.0f);
    sampler.setDescription(description);
    EXPECT_FALSE(codec.serialize(sampler, binary));

    description.reductionType = NWB::Core::SamplerReductionType::Standard;
    description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    sampler.setDescription(description);
    ASSERT_TRUE(codec.serialize(sampler, binary));
    UniquePtr<NWB::Core::Assets::IAsset> recoveredAsset;
    auto recoveredResult = codec.deserialize(testArena.arena, sampler.virtualPath(), binary);
    ASSERT_TRUE(recoveredResult);
    recoveredAsset = Move(*recoveredResult);
    ASSERT_NE(recoveredAsset.get(), nullptr);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("sampler description is invalid")));
}


TEST(AssetsGraphics, SamplerFailedReloadPreservesDescriptionAcrossEveryPrefixAndAdmissionBoundary){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    NWB::Impl::Sampler sampler(testArena.arena, Name("project/samplers/reload_admission"));
    NWB::Core::SamplerDesc description;
    description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    description.maxAnisotropy = 4.0f;
    description.mipBias = -0.25f;
    description.minFilter = false;
    description.addressW = NWB::Core::SamplerAddressMode::Border;
    sampler.setDescription(description);
    NWB::Impl::SamplerAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(codec.serialize(sampler, binary));
    ASSERT_TRUE(sampler.loadBinary(binary));
    for(usize prefix = 0u; prefix < binary.size(); ++prefix){
        NWB::Core::Assets::AssetBytes malformed(binary);
        malformed.resize(prefix);
        EXPECT_FALSE(sampler.loadBinary(malformed)) << prefix;
        NWB::Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(sampler, retained));
        EXPECT_EQ(retained, binary) << prefix;
    }
    for(u32 invalidCase = 0u; invalidCase < 7u; ++invalidCase){
        NWB::Impl::SamplerBinaryPayload::HeaderBinary header;
        NWB_MEMCPY(&header, sizeof(header), binary.data(), binary.size());
        switch(invalidCase){
        case 0u: header.magic = 0u; break;
        case 1u: ++header.version; break;
        case 2u: header.reserved = 1u; break;
        case 3u: header.minFilter = 2u; break;
        case 4u: header.addressU = Limit<u32>::s_Max; break;
        case 5u: header.mipBias = BitCast<f32>(0x7fc00000u); break;
        }
        NWB::Core::Assets::AssetBytes malformed(testArena.arena);
        AppendPOD(malformed, header);
        if(invalidCase == 6u)
            malformed.push_back(0u);
        EXPECT_FALSE(sampler.loadBinary(malformed)) << invalidCase;
        NWB::Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(sampler, retained));
        EXPECT_EQ(retained, binary) << invalidCase;
    }
    EXPECT_EQ(logger.errorCount(), binary.size() + 7u);
}


TEST(AssetsGraphics, SamplerCodecRejectsWideEnumAliasesBeforeNarrowingAndPreservesReload){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    NWB::Impl::Sampler sampler(testArena.arena, Name("project/samplers/wide_enum_admission"));
    NWB::Core::SamplerDesc description;
    description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    description.addressU = NWB::Core::SamplerAddressMode::MirrorOnce;
    description.addressV = NWB::Core::SamplerAddressMode::MirrorOnce;
    description.addressW = NWB::Core::SamplerAddressMode::MirrorOnce;
    description.reductionType = NWB::Core::SamplerReductionType::Comparison;
    sampler.setDescription(description);
    NWB::Impl::SamplerAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(codec.serialize(sampler, binary));
    ASSERT_EQ(binary.size(), sizeof(NWB::Impl::SamplerBinaryPayload::HeaderBinary));

    for(const usize offset : {
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, addressU),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, addressV),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, addressW),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, reductionType)
    }){
        for(const u32 rawValue : { 0x100u, 0x101u }){
            NWB::Core::Assets::AssetBytes malformed(binary);
            NWB_MEMCPY(malformed.data() + offset, sizeof(rawValue), &rawValue, sizeof(rawValue));
            EXPECT_FALSE(sampler.loadBinary(malformed)) << offset << ":" << rawValue;
            NWB::Core::Assets::AssetBytes retained(testArena.arena);
            ASSERT_TRUE(codec.serialize(sampler, retained));
            EXPECT_EQ(retained, binary) << offset << ":" << rawValue;
            EXPECT_FALSE(codec.deserialize(testArena.arena, sampler.virtualPath(), malformed)) << offset << ":" << rawValue;
        }
    }
    EXPECT_EQ(logger.errorCount(), 16u);
}


TEST(AssetsGraphics, SamplerCookerRejectsFixedBorderColorAndUnsupportedReductionMetadata){
    static constexpr AStringView s_UnsupportedAssignments[] = { "asset.border_color = [0.0, 0.0, 0.0, 0.0];\n", "asset.reduction = \"minimum\";\n", "asset.reduction = \"maximum\";\n", };

    for(const AStringView assignment : s_UnsupportedAssignments){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
        TestArena testArena;
        AString metadata(s_SamplerTestMetadata);
        const AStringView reductionAssignment = "asset.reduction = \"standard\";\n";
        if(assignment.starts_with(s_ReductionAssignmentPrefix)){
            const usize reductionPosition = metadata.find(reductionAssignment);
            ASSERT_NE(reductionPosition, AString::npos);
            metadata.replace(reductionPosition, reductionAssignment.size(), assignment);
        }
        else
            metadata += assignment;

        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(AStringView(metadata)));
        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, s_UnsupportedMetadataCase) / s_AssetsDirectory;
        const Path metadataPath = assetRoot / s_SamplersDirectory / s_LinearClampMetadataFilename;
        NWB::Impl::SamplerCookEntry entry(testArena.arena);
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        EXPECT_FALSE(NWB::Impl::ParseSamplerCookMetadata(assetRoot, "project", metadataPath, document, entry.arena, scratchArena)) << assignment;
        const TStringView expectedDiagnostic = assignment.starts_with(s_ReductionAssignmentPrefix) ? NWB_TEXT("unsupported reduction type") : NWB_TEXT("unsupported asset field 'border_color'");
        EXPECT_TRUE(logger.sawErrorContaining(expectedDiagnostic)) << assignment;
    }
}

TEST(AssetsGraphics, SamplerCookerRejectsDeprecatedVersionMetadata){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    AString metadata(s_SamplerTestMetadata);
    const usize firstFieldPosition = metadata.find("asset.min_filter");
    ASSERT_NE(firstFieldPosition, AString::npos);
    metadata.replace(firstFieldPosition, 0u, "asset.version = 1;\n");

    NWB::Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(AStringView(metadata.data(), metadata.size())));

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, s_UnsupportedMetadataCase) / s_AssetsDirectory;
    const Path metadataPath = assetRoot / s_SamplersDirectory / s_LinearClampMetadataFilename;
    NWB::Impl::SamplerCookEntry entry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    EXPECT_FALSE(NWB::Impl::ParseSamplerCookMetadata(assetRoot, "project", metadataPath, document, entry.arena, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'version'")));
#else
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

