// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/paint.h>

#include <impl/ecs_ui/toolkit/layout/types.adhd.h>
#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Build and arrange on one owning UI thread. The supplied arena outlives the tree and its published boxes.
class LayoutTree final : NoCopy{
public:
    // Capacity is clamped to s_LayoutMaxNodes; zero capacity admits no nodes.
    explicit LayoutTree(Core::Alloc::GlobalArena& arena, const u32 maxNodes = s_LayoutMaxNodes)
        : m_nodes(arena)
        , m_work(arena)
        , m_boxes(arena)
        , m_stagedBoxes(arena)
        , m_maxNodes(Min(maxNodes, s_LayoutMaxNodes))
    {
        m_nodes.reserve(m_maxNodes);
        m_work.reserve(m_maxNodes);
        m_boxes.reserve(m_maxNodes);
        m_stagedBoxes.reserve(m_maxNodes);
    }


public:
    // Start a new build while retaining the last successful arranged output.
    void reset()noexcept{
        m_nodes.clear();
        m_work.clear();
        m_stagedBoxes.clear();
        m_buildFailed = false;
    }
    // A failed admission poisons this build until reset().
    [[nodiscard]] Expected<u32> addNode(const u32 parent, const LayoutNodeDesc& description){
        if(!CanAddLayoutTreeNode(Span<const LayoutTreeNode>(m_nodes.data(), m_nodes.size()), m_maxNodes, m_buildFailed, parent, description)){
            m_buildFailed = true;
            return MakeUnexpected(Failure{});
        }
        const u32 index = static_cast<u32>(m_nodes.size());
        m_nodes.push_back(LayoutTreeNode(description));
        if(parent != s_LayoutNoParent){
            LayoutTreeNode& parentNode = m_nodes[parent];
            if(parentNode.lastChild == s_LayoutNoParent)
                parentNode.firstChild = index;
            else
                m_nodes[parentNode.lastChild].nextSibling = index;
            parentNode.lastChild = index;
        }
        return index;
    }
    // Root sizing follows its policies against the viewport. All geometry stays in logical units.
    [[nodiscard]] b8 arrange(const Rect& viewport){
        if(m_buildFailed || m_nodes.empty() || !IsValidUiRect(viewport))
            return false;
        m_work.assign(m_nodes.size(), {});
        if(!measure())
            return false;
        const LayoutNodeDesc& root = m_nodes[0u].description;
        const Point measured = m_work[0u].box.measuredSize;
        const f32 width = root.width.policy == LayoutSizePolicy::Stretch ? viewport.width : measured.x;
        const f32 height = root.height.policy == LayoutSizePolicy::Stretch ? viewport.height : measured.y;
        m_work[0u].box.rectangle = { viewport.x, viewport.y, width, height };
        m_work[0u].inheritedClip = viewport;
        for(u32 index = 0u; index < m_nodes.size(); ++index){
            if(!arrangeNode(index))
                return false;
        }
        m_stagedBoxes.clear();
        for(const LayoutTreeWork& work : m_work)
            m_stagedBoxes.push_back(work.box);
        m_boxes.swap(m_stagedBoxes);
        return true;
    }
    [[nodiscard]] u32 nodeCount()const noexcept{ return static_cast<u32>(m_nodes.size()); }

    [[nodiscard]] const LayoutBox* box(u32 index)const noexcept{ return index < m_boxes.size() ? &m_boxes[index] : nullptr; }

    [[nodiscard]] const PaintVector<LayoutBox>& boxes()const noexcept{ return m_boxes; }


private:
    [[nodiscard]] b8 measure()noexcept{
        const Span<const LayoutTreeNode> nodeSpan(m_nodes.data(), m_nodes.size());
        for(usize reverse = m_nodes.size(); reverse > 0u; --reverse){
            const u32 index = static_cast<u32>(reverse - 1u);
            Point measuredSize;
            if(!MeasureLayoutTreeNode(nodeSpan, Span<const LayoutTreeWork>(m_work.data(), m_work.size()), index, measuredSize))
                return false;
            m_work[index].box.measuredSize = measuredSize;
        }
        return true;
    }
    [[nodiscard]] b8 arrangeNode(const u32 index)noexcept{
        const Span<const LayoutTreeNode> nodeSpan(m_nodes.data(), m_nodes.size());
        const Span<const LayoutTreeWork> workSpan(m_work.data(), m_work.size());
        Rect childClip;
        f64 available = 0.0;
        f64 weight = 0.0;
        f64 cursor = 0.0;
        if(!TryBeginArrangeLayoutTreeNode(nodeSpan, workSpan, index, m_work[index].box, childClip, available, weight, cursor))
            return false;
        const LayoutTreeNode& node = m_nodes[index];
        const LayoutNodeDesc& description = node.description;
        const Rect content = m_work[index].box.content;
        for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
            Rect childRect;
            if(!TryArrangeLayoutTreeChild(description.direction, description.gap, content, m_nodes[child].description, m_work[child].box.measuredSize, available, weight, cursor, childRect))
                return false;
            m_work[child].box.rectangle = childRect;
            m_work[child].inheritedClip = childClip;
        }
        return true;
    }


private:
    PaintVector<LayoutTreeNode> m_nodes;
    PaintVector<LayoutTreeWork> m_work;
    PaintVector<LayoutBox> m_boxes;
    PaintVector<LayoutBox> m_stagedBoxes;
    u32 m_maxNodes = 0u;
    b8 m_buildFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

