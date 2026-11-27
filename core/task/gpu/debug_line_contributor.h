// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <global/containers.h>
#include <global/simdmath.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(16) GpuDebugLineVertex{
    Float4 positionPad = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 color = Float4(1.0f, 1.0f, 1.0f, 1.0f);
};
static_assert(sizeof(GpuDebugLineVertex) == 32u, "GpuDebugLineVertex must match 32-byte shader stride");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuDebugLineCollector final : NoCopy{
public:
    void clear()noexcept{
        m_depthTestedVertices.clear();
        m_overlayVertices.clear();
    }

    void reserve(const usize depthTestedVertexCapacity, const usize overlayVertexCapacity){
        if(depthTestedVertexCapacity > m_depthTestedVertices.capacity())
            m_depthTestedVertices.reserve(depthTestedVertexCapacity);
        if(overlayVertexCapacity > m_overlayVertices.capacity())
            m_overlayVertices.reserve(overlayVertexCapacity);
    }

    void pushDepthTestedLine(const Float4& start, const Float4& end, const Float4& color){
        m_depthTestedVertices.push_back(GpuDebugLineVertex{ start, color });
        m_depthTestedVertices.push_back(GpuDebugLineVertex{ end, color });
    }

    void pushOverlayLine(const Float4& start, const Float4& end, const Float4& color){
        m_overlayVertices.push_back(GpuDebugLineVertex{ start, color });
        m_overlayVertices.push_back(GpuDebugLineVertex{ end, color });
    }

    [[nodiscard]] Span<const GpuDebugLineVertex> depthTestedVertices()const noexcept{
        return Span<const GpuDebugLineVertex>(m_depthTestedVertices.data(), m_depthTestedVertices.size());
    }

    [[nodiscard]] Span<const GpuDebugLineVertex> overlayVertices()const noexcept{
        return Span<const GpuDebugLineVertex>(m_overlayVertices.data(), m_overlayVertices.size());
    }

    [[nodiscard]] b8 empty()const noexcept{
        return m_depthTestedVertices.empty() && m_overlayVertices.empty();
    }


private:
    InteropVector<GpuDebugLineVertex> m_depthTestedVertices;
    InteropVector<GpuDebugLineVertex> m_overlayVertices;
};


interface IDebugLineProvider{
public:
    virtual ~IDebugLineProvider()noexcept = default;


public:
    [[nodiscard]] virtual b8 hasDebugLines()const = 0;
    virtual void collectDebugLines(GpuDebugLineCollector& collector, const SIMDMatrix& viewProj) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

