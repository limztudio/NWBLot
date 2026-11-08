// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_shader/asset.h>

#include <core/assets/cook_metadata.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_construction_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::Assets;


class NullAllocatedAsset final : public TypedAsset<NullAllocatedAsset>{
public:
    inline static u32 s_ConstructorCalls = 0u;
    inline static b8 s_LoadCalled = false;
    [[nodiscard]] static const Name& AssetTypeName()noexcept{
        static constexpr Name s_Type("tests/metadata_registry/null_allocation");
        return s_Type;
    }

    static void* operator new(usize)noexcept{ return nullptr; }

    static void operator delete(void* pointer)noexcept{ ::operator delete(pointer); }


public:
    NullAllocatedAsset(AssetArena&, const Name& virtualPath)noexcept
        : TypedAsset(virtualPath)
    {
        ++s_ConstructorCalls;
    }


public:
    b8 loadBinary(const Span<const u8>)noexcept{
        s_LoadCalled = true;
        return true;
    }
};

class NullAllocatedCodec final : public AssetCodec<NullAllocatedAsset>{
public:
    NullAllocatedCodec()noexcept = default;


public:
    virtual b8 serialize(const IAsset&, AssetBytes&)const noexcept override{ return false; }
};

struct ThrowingImplicitAssetName{
    explicit operator Name() & noexcept{ return s_NameNone; }

    operator const Name&()const&{ throw 197u; }
};

class ImplicitNameAsset final : public TypedAsset<ImplicitNameAsset>{
public:
    [[nodiscard]] static ThrowingImplicitAssetName& AssetTypeName()noexcept{
        static ThrowingImplicitAssetName s_Value;
        return s_Value;
    }


public:
    ImplicitNameAsset() = default;
    explicit ImplicitNameAsset(const Name& virtualPath)
        : TypedAsset(virtualPath)
    {}
    ImplicitNameAsset(AssetArena&, const Name& virtualPath)
        : TypedAsset(virtualPath)
    {}


public:
    b8 loadBinary(const Span<const u8>)noexcept{ return true; }
};

class ImplicitNameCodec final : public AssetCodec<ImplicitNameAsset>{
public:
    ImplicitNameCodec() = default;


public:
    virtual b8 serialize(const IAsset&, AssetBytes&)const noexcept override{ return false; }
};

class ImplicitNameShader final : public Impl::TypedShader<ImplicitNameShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::ComputeStage;


public:
    [[nodiscard]] static ThrowingImplicitAssetName& AssetTypeName()noexcept{
        static ThrowingImplicitAssetName s_Value;
        return s_Value;
    }


public:
    using Impl::TypedShader<ImplicitNameShader>::TypedShader;
};


struct ThrowingComparisonAnswer{
    explicit operator b8()const{ throw 199u; }
};

struct AssetComparisonTag{};

[[nodiscard]] inline ThrowingComparisonAnswer operator!=(const Name&, const AssetComparisonTag&)noexcept{ return {}; }

class ContextualComparisonAsset final : public IAsset{
public:
    [[nodiscard]] static AssetComparisonTag AssetTypeName()noexcept{ return {}; }


public:
    ContextualComparisonAsset()noexcept
        : IAsset(Name("tests/asset_constructor/contextual_comparison"))
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetCodecAdmission, NullClassAllocationDoesNotCallAssetLoad){
    AssetArena arena(Name("tests/asset_codec/null_allocation"));
    NullAllocatedAsset::s_ConstructorCalls = 0u;
    NullAllocatedAsset::s_LoadCalled = false;
    NullAllocatedCodec codec;
    AssetBytes bytes(arena);
    const auto result = codec.deserialize(Name("project/tests/null_asset"), bytes, arena);
    EXPECT_FALSE(result);
    EXPECT_EQ(NullAllocatedAsset::s_ConstructorCalls, 0u);
    EXPECT_FALSE(NullAllocatedAsset::s_LoadCalled);
}

TEST(AssetConstructorContracts, ImplicitNameBindingCanThrowDespiteNothrowExplicitConversion){
    static_assert(noexcept(Name(ImplicitNameAsset::AssetTypeName())));
    static_assert(!noexcept(DeclVal<void(*)(const Name&)noexcept>()(ImplicitNameAsset::AssetTypeName())));
    static_assert(!IsNothrowConstructible_V<ImplicitNameAsset>);
    static_assert(!IsNothrowConstructible_V<ImplicitNameCodec>);
    static_assert(!IsNothrowConstructible_V<ImplicitNameShader, AssetArena&>);
    static_assert(!IsNothrowConstructible_V<ImplicitNameShader, AssetArena&, const Name&>);
    static_assert(!IsNothrowConstructible_V<Impl::ShaderAssetCodec<ImplicitNameShader>>);
    EXPECT_THROW(ImplicitNameAsset{}, u32);
    EXPECT_THROW(ImplicitNameAsset{Name("project/tests/name_conversion")}, u32);
    EXPECT_THROW(ImplicitNameCodec{}, u32);
    AssetArena arena(Name("tests/asset_constructor/implicit_name"));
    EXPECT_THROW(ImplicitNameShader{arena}, u32);
    EXPECT_THROW((ImplicitNameShader{arena, Name("project/tests/shader_name_conversion")}), u32);
    EXPECT_THROW((Impl::ShaderAssetCodec<ImplicitNameShader>{}), u32);
}


TEST(AssetConstructorContracts, CastIncludesTheCompleteContextualComparisonConversion){
    static_assert(noexcept(DeclVal<const IAsset&>().assetType() != ContextualComparisonAsset::AssetTypeName()));
    static_assert(!noexcept(CastAsset<ContextualComparisonAsset>(DeclVal<const IAsset*>())));
    ContextualComparisonAsset asset;
    EXPECT_THROW({ EXPECT_EQ(CastAsset<ContextualComparisonAsset>(&asset), &asset); }, u32);
    EXPECT_EQ(CastAsset<ContextualComparisonAsset>(nullptr), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

