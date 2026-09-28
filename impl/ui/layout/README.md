# Logical layout tree

`LayoutTree` owns bounded construction, measurement, and arranged geometry in a caller-owned `GlobalArena`. It depends only on the UI geometry/arena types. It does not shape text, access the OS, or create GPU resources.

Build one root with `s_LayoutNoParent`, then add each child after its parent. Nodes receive `u32` indexes. The tree admits up to 4,096 nodes (or a smaller constructor capacity), rejects a second root or children on `Leaf`, and cannot express a cycle. Containers preserve child insertion order. A failed `addNode` leaves its output index unchanged and prevents publication until `reset()` starts another build.

`reset()` clears construction and working state while retaining the last successful `boxes()`. `arrange(viewport)` measures bottom-up and arranges top-down without recursion, then swaps the complete result into the published boxes. Invalid viewport geometry, unrepresentable measured sizes, or arranged-coordinate overflow leaves the prior boxes and their addresses intact. Readers must use the current build's node indexes only after successful arrangement; a successful arrange invalidates previous box pointers.

All dimensions and positions are logical units. DPI changes are applied later by painting/rendering, so layout does not round to physical pixels. `intrinsicSize` supplies leaf content dimensions, or a minimum natural content size for a container. Padding surrounds content; `gap` separates adjacent row/column children. The measured border size contains padding and natural child sizes.

Each axis has a `LayoutSize` policy:

- `Fixed` uses its finite, nonnegative `value` as the border size.
- `Content` uses the measured border size. Its finite, nonnegative `value` is ignored.
- `Stretch` requires a positive finite weight. In a row's horizontal axis or a column's vertical axis, it divides the space remaining after fixed/content children and gaps by weight. The remaining space clamps to zero; intrinsic size does not prevent shrinking. In the cross axis and in overlays, it fills the available content extent.

The root follows its size policies against the viewport; a root with two stretch axes fills it. Row children share the top edge, column children share the left edge, and overlay children share the content origin. There is no alignment, wrapping, scrolling, or flex minimum/maximum policy in this increment.

Each `LayoutBox` publishes its border `rectangle`, padded `content`, paint `clip`, visible `hit` rectangle, and natural/resolved `measuredSize`. Ancestor clipping always includes the viewport. By default descendants are clipped to a container's padded content. Setting `clipChildren=false` lets descendants overflow that container while retaining the outer ancestor clip. Fixed/content children keep their requested sizes and overflow is clipped; padding that consumes an entire axis produces zero content on the border edge.

The tests cover nested content dimensions, weighted row sizing, exhausted column space, overlay placement, overflow clipping, root sizing, odd physical display sizes with stable logical coordinates, empty content, invalid construction/geometry, publication failure atomicity, capacity, and a 4,096-node depth without recursion.
