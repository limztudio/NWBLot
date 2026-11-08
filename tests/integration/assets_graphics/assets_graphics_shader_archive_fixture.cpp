// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_shader_archive_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


Expected<NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>> AssetsGraphicsFixture::ShaderArchiveFixture::LoadCookedShaderArchiveRecords(AssetsGraphicsFixture::TestArena& testArena, const Path& outputDirectory){
    NWB::Core::Filesystem::VolumeMountDesc mountDesc(testArena.arena);
    mountDesc.volumeName = "graphics";
    mountDesc.mountDirectory = outputDirectory;
    auto filesystemResult = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
    const b8 loadedVolume = static_cast<b8>(filesystemResult);
    EXPECT_TRUE(loadedVolume);
    if(!loadedVolume)
        return MakeUnexpected(filesystemResult.error());
    UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = Move(*filesystemResult);

    NWB::Core::GraphicsBytes indexBinary(testArena.arena);
    const b8 loadedIndex = filesystem->readFile(NWB::Core::ShaderArchive::s_IndexVirtualPathName, indexBinary);
    EXPECT_TRUE(loadedIndex);
    EXPECT_FALSE(indexBinary.empty());
    if(!loadedIndex || indexBinary.empty())
        return MakeUnexpected(Failure{});

    auto deserialized = NWB::Core::ShaderArchive::DeserializeIndex(testArena.arena, { indexBinary.data(), indexBinary.size() });
    EXPECT_TRUE(deserialized);
    return deserialized;
}

Expected<u64> AssetsGraphicsFixture::ShaderArchiveFixture::FindShaderArchiveSourceChecksum(const Span<const NWB::Core::ShaderArchive::Record> records, const Name shaderName, const Name stageName)noexcept{
    for(const NWB::Core::ShaderArchive::Record& record : records){
        const AStringView variantName(record.variantName.data(), record.variantName.size());
        if(record.shaderName == shaderName && record.stage == stageName && variantName == NWB::Core::ShaderArchive::s_DefaultVariant){
            if(record.sourceChecksum == 0u)
                return MakeUnexpected(Failure{});
            return record.sourceChecksum;
        }
    }

    return MakeUnexpected(Failure{});
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

