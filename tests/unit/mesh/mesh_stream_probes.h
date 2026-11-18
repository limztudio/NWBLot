// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/asset.adhd.h>


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

