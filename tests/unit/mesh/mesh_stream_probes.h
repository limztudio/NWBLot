// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/global.h>
#include <impl/assets_mesh/asset.adhd.h>
#include <impl/assets_mesh/cook.adhd.h>
#include <impl/assets_mesh/cook_solid_topology.adhd.h>
#include <impl/assets_mesh/meshlet_triangle_indices.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingPositionStreamGeometry{
    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const;
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normalStream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& tangentStream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Float2U>& uv0Stream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colorStream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Impl::MeshletDesc>& meshlets()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Impl::MeshletBounds>& meshletBounds()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPositionRefDeltas()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletAttributeRefDeltas()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Impl::MeshletLocalVertexRef>& meshletLocalVertexRefs()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u32>& solidTriangleWords()const noexcept;
};

struct NoThrowCornerVisitor{
    [[nodiscard]] b8 operator()(const Impl::MeshletDesc&, usize, const Impl::MeshletLocalVertexRef&)const noexcept;
};

struct ThrowingCornerVisitor{
    [[nodiscard]] b8 operator()(const Impl::MeshletDesc&, usize, const Impl::MeshletLocalVertexRef&)const;
};

class NthFailMeshArena final : public ArenaBase{
public:
    NthFailMeshArena()
        : ArenaBase(Name("tests/mesh/nth_fail"))
        , m_backing(Name("tests/mesh/nth_fail_backing"))
    {}

    void arm(const usize failOnCall)noexcept{
        m_armed = true;
        m_remaining = failOnCall;
    }

    void* allocate(const usize alignment, const usize size)override{
        if(m_armed){
            if(m_remaining == 0u)
                throw AllocationException{};
            --m_remaining;
        }
        return m_backing.allocate(alignment, size);
    }

    void* reallocate(void* p, const usize alignment, const usize size)override{
        if(m_armed){
            if(m_remaining == 0u)
                throw AllocationException{};
            --m_remaining;
        }
        return m_backing.reallocate(p, alignment, size);
    }

    void deallocate(void* p, const usize alignment, const usize size)noexcept override{
        m_backing.deallocate(p, alignment, size);
    }


private:
    Core::Alloc::GlobalArena m_backing;
    usize m_remaining = 0u;
    b8 m_armed = false;
};

[[nodiscard]] inline b8 VerifyMeshCookAllocationUnwindPropagates()noexcept{
    const Array<Float3U, 4u> positions{
        Float3U{ 0.0f, 0.0f, 0.0f },
        Float3U{ 1.0f, 0.0f, 0.0f },
        Float3U{ 0.0f, 1.0f, 0.0f },
        Float3U{ 0.0f, 0.0f, 1.0f },
    };
    const Array<u32, 12u> indices{ 0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u };
    const Span<const Float3U> posSpan(positions.data(), positions.size());
    const Span<const u32> idxSpan(indices.data(), indices.size());

    b8 solidThrew = false;
    {
        NthFailMeshArena failArena;
        failArena.arm(0u);
        try{
            Vector<u32, ArenaBase> words(failArena);
            if(Impl::BuildSolidTriangleWordsInto(posSpan, idxSpan, words))
                return false;
        }
        catch(const AllocationException&){
            solidThrew = true;
        }
        catch(...){
            return false;
        }
    }

    b8 indicesThrew = false;
    {
        NthFailMeshArena failArena;
        failArena.arm(0u);
        try{
            const auto decoded = Impl::BuildMeshletTriangleIndices(
                failArena,
                Span<const Impl::MeshletDesc>{},
                Span<const Impl::MeshletLocalVertexRef>{},
                Span<const u8>{},
                Span<const u8>{ reinterpret_cast<const u8*>(indices.data()), indices.size() },
                positions.size()
            );
            if(decoded.has_value())
                return false;
        }
        catch(const AllocationException&){
            indicesThrew = true;
        }
        catch(...){
            return false;
        }
    }

    b8 meshletsThrew = false;
    {
        NthFailMeshArena failArena;
        ::Path<::ArenaBase> meshPath(failArena);
        Vector<Impl::MeshletDesc, ArenaBase> outMeshlets(failArena);
        Vector<Impl::MeshletBounds, ArenaBase> outBounds(failArena);
        Vector<Impl::MeshletPositionStreamRef, ArenaBase> outPosRefs(failArena);
        Vector<Impl::MeshletAttributeStreamRef, ArenaBase> outAttrRefs(failArena);
        Vector<Impl::MeshletLocalVertexRef, ArenaBase> outLocalRefs(failArena);
        Vector<u8, ArenaBase> outPrimIndices(failArena);
        const Array<Impl::MeshVertexRef, 3u> refs{
            Impl::MeshVertexRef{ 0u, 0u, 0u, 0u, 0u },
            Impl::MeshVertexRef{ 1u, 1u, 1u, 1u, 1u },
            Impl::MeshVertexRef{ 2u, 2u, 2u, 2u, 2u },
        };
        failArena.arm(0u);
        try{
            if(Impl::BuildCookMeshlets(
                meshPath,
                Span<const u32>(indices.data(), 3u),
                false,
                1u,
                Span<const Float3U>(positions.data(), 3u),
                Span<const Half4U>{},
                Span<const Half4U>{},
                Span<const Float2U>{},
                Span<const Half4U>{},
                Span<const Impl::MeshVertexRef>(refs.data(), refs.size()),
                outMeshlets,
                outBounds,
                outPosRefs,
                outAttrRefs,
                outLocalRefs,
                outPrimIndices
            ))
                return false;
        }
        catch(const AllocationException&){
            meshletsThrew = true;
        }
        catch(...){
            return false;
        }
    }

    static_assert(!noexcept(Impl::BuildCookMeshlets(
        DeclVal<const ::Path<::ArenaBase>&>(),
        DeclVal<Span<const u32>>(),
        DeclVal<b8>(),
        DeclVal<u32>(),
        DeclVal<Span<const Float3U>>(),
        DeclVal<Span<const Half4U>>(),
        DeclVal<Span<const Half4U>>(),
        DeclVal<Span<const Float2U>>(),
        DeclVal<Span<const Half4U>>(),
        DeclVal<Span<const Impl::MeshVertexRef>>(),
        DeclVal<Vector<Impl::MeshletDesc, ::ArenaBase>&>(),
        DeclVal<Vector<Impl::MeshletBounds, ::ArenaBase>&>(),
        DeclVal<Vector<Impl::MeshletPositionStreamRef, ::ArenaBase>&>(),
        DeclVal<Vector<Impl::MeshletAttributeStreamRef, ::ArenaBase>&>(),
        DeclVal<Vector<Impl::MeshletLocalVertexRef, ::ArenaBase>&>(),
        DeclVal<Vector<u8, ::ArenaBase>&>()
    )));

    return solidThrew && indicesThrew && meshletsThrew;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

