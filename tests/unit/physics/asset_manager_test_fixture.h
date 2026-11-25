// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/auto_registration.h>
#include <core/assets/manager.h>
#include <core/assets/registry.h>
#include <impl/assets_mesh/asset.adhd.h>
#include <impl/assets_physics/physics_assets.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{
namespace PhysicsCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InMemoryAssetBinarySource final : public Core::Assets::IAssetBinarySource{
public:
    explicit InMemoryAssetBinarySource(Core::Assets::AssetArena& arena)noexcept
        : m_arena(arena)
        , m_entries(0u, Hasher<Name>(), EqualTo<Name>(), arena)
    {}

    void setEntry(const Name& path, const Span<const u8> bytes){
        Core::Assets::AssetBytes copy{ m_arena };
        copy.assign(bytes.begin(), bytes.end());
        m_entries.insert_or_assign(path, Move(copy));
    }

    virtual b8 readAssetBinary(const Name& virtualPath, Core::Assets::AssetBytes& outBinary)const override{
        const auto it = m_entries.find(virtualPath);
        if(it == m_entries.end())
            return false;
        outBinary.assign(it->second.begin(), it->second.end());
        return true;
    }

private:
    Core::Assets::AssetArena& m_arena;
    HashMap<Name, Core::Assets::AssetBytes, Core::Assets::AssetArena, Hasher<Name>, EqualTo<Name>> m_entries;
};

class InMemoryAssetManagerFixture final : NoCopy{
public:
    InMemoryAssetManagerFixture()
        : m_arena(Name("tests/physics/in_memory_asset_arena"))
        , m_registry(m_arena)
        , m_source(m_arena)
        , m_manager(m_arena, m_registry, m_source)
    {
        Core::Assets::RegisterAutoCollectedAssetCodecs(m_registry);
        (void)m_registry.registerCodec(MakeUnique<Core::Assets::DeclaredAssetCodec<Impl::Mesh>>(), true);
        (void)m_registry.registerCodec(MakeUnique<Impl::HeightfieldAssetCodec>(), true);
        (void)m_registry.registerCodec(MakeUnique<Impl::CompoundHullAssetCodec>(), true);
    }

    void setBinary(const Name& path, const Span<const u8> bytes){
        m_source.setEntry(path, bytes);
    }

    Core::Assets::AssetManager& manager()noexcept{
        return m_manager;
    }

private:
    Core::Assets::AssetArena m_arena;
    Core::Assets::AssetRegistry m_registry;
    InMemoryAssetBinarySource m_source;
    Core::Assets::AssetManager m_manager;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
