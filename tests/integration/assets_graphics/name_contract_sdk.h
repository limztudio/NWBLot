// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::AssetNameAbi{
using namespace Core::Assets;

struct NameOperand{
    explicit operator Name() & noexcept{ return s_NameNone; }
    operator const Name&()const&{ throw 197u; }
};

struct NameProvider{
    static NameOperand& AssetTypeName()noexcept{
        static NameOperand s_Operand;
        return s_Operand;
    }
};

class NamedAsset final : public NameProvider, public TypedAsset<NamedAsset>{
public:
    NamedAsset() = default;
    explicit NamedAsset(const Name& path) : TypedAsset(path){}
    NamedAsset(AssetArena&, const Name& path) : TypedAsset(path){}
    b8 loadBinary(Span<const u8>)noexcept{ return true; }
};

template<typename AssetT>
class ConcreteCodec final : public AssetCodec<AssetT>{
public:
    ConcreteCodec() = default;
    b8 serialize(const IAsset&, AssetBytes&)const noexcept override{ return false; }
};

template<typename Interface>
class NamedShader final : public NameProvider, public TypedAsset<NamedShader<Interface>, Interface>{
    using Base = TypedAsset<NamedShader<Interface>, Interface>;
public:
    using NameProvider::AssetTypeName;
    explicit NamedShader(AssetArena& arena)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(NameProvider::AssetTypeName())))
        : Base(), m_bytes(arena){}
    NamedShader(AssetArena& arena, const Name& path)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(NameProvider::AssetTypeName())))
        : Base(path), m_bytes(arena){}
    AStringView entryPoint()const noexcept override{ return {}; }
    const Vector<u8, ::ArenaBase>& bytecode()const noexcept override{ return m_bytes; }
    b8 loadBinary(Span<const u8>)noexcept override{ return false; }

private:
    Vector<u8, ::ArenaBase> m_bytes;
};

struct ComparisonOperand{
    explicit operator b8()const{ throw 199u; }
};
struct NameTag{};
inline ComparisonOperand operator!=(const Name&, const NameTag&)noexcept{ return {}; }

class ComparisonAsset final : public IAsset{
public:
    explicit ComparisonAsset(const Name& type)noexcept : IAsset(type){}
    static NameTag AssetTypeName()noexcept{ return {}; }
};

template<typename T, typename Arena>
constexpr b8 StorageNameConstructor()noexcept{ return IsNothrowConstructible_V<T, Arena&>; }
template<typename T, typename Arena>
constexpr b8 PathNameConstructor()noexcept{ return IsNothrowConstructible_V<T, Arena&, const Name&>; }
template<typename Asset>
constexpr b8 ExplicitConversion()noexcept{ return noexcept(Name(Asset::AssetTypeName())); }
template<typename Asset>
constexpr b8 ImplicitConversion()noexcept{ return noexcept(DeclVal<void(*)(const Name&)noexcept>()(Asset::AssetTypeName())); }
template<typename Asset>
constexpr b8 Compare()noexcept{ return noexcept(DeclVal<const IAsset&>().assetType() != Asset::AssetTypeName()); }
template<typename Asset>
constexpr b8 Cast()noexcept{ return noexcept(CastAsset<Asset>(DeclVal<const IAsset*>())); }
template<typename Asset>
usize CastAddress(const Asset& value){ return reinterpret_cast<usize>(CastAsset<Asset>(&value)); }
template<typename Asset>
usize CastNull(){ return reinterpret_cast<usize>(CastAsset<Asset>(nullptr)); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

