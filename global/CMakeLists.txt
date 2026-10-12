if(NWB_OBFUSCATE_STRING_LITERALS)
    add_library(nwb_string_literal_runtime OBJECT "${CMAKE_CURRENT_LIST_DIR}/string_literal_runtime.cpp")
    set_target_properties(nwb_string_literal_runtime PROPERTIES CXX_COMPILER_LAUNCHER "" POSITION_INDEPENDENT_CODE ON)
    target_include_directories(nwb_string_literal_runtime PRIVATE "${PROJECT_SOURCE_DIR}")
    nwb_apply_codegen(nwb_string_literal_runtime)
    # Only scalar aliases are used; header-injected CRT/iterator directives must not constrain vendor images.
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_string_literal_runtime PRIVATE /clang:-fno-autolink)
    else()
        target_compile_options(nwb_string_literal_runtime PRIVATE -fno-autolink)
    endif()
endif()

nwb_declare_static_library(global_foundation)
target_sources(global_foundation PRIVATE "${CMAKE_CURRENT_LIST_DIR}/base64.h" "${CMAKE_CURRENT_LIST_DIR}/allocation_size.h" "${CMAKE_CURRENT_LIST_DIR}/atomic_identity.h" "${CMAKE_CURRENT_LIST_DIR}/arena_base.h" "${CMAKE_CURRENT_LIST_DIR}/arena_memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_memory.h" "${CMAKE_CURRENT_LIST_DIR}/auto_registration.h" "${CMAKE_CURRENT_LIST_DIR}/container/adaptor.cpp" "${CMAKE_CURRENT_LIST_DIR}/container/adaptor.h" "${CMAKE_CURRENT_LIST_DIR}/cpu_topology.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_topology.h" "${CMAKE_CURRENT_LIST_DIR}/inplace_function.h" "${CMAKE_CURRENT_LIST_DIR}/mesh/tangent_frame_rebuild.h" "${CMAKE_CURRENT_LIST_DIR}/math/bounding_box.h" "${CMAKE_CURRENT_LIST_DIR}/math/bounding_corners.h"
    "${CMAKE_CURRENT_LIST_DIR}/math/bounding_frustum.h" "${CMAKE_CURRENT_LIST_DIR}/math/bounding_oriented_box.h" "${CMAKE_CURRENT_LIST_DIR}/math/bounding_shape_decls.h" "${CMAKE_CURRENT_LIST_DIR}/math/bounding_sphere.h"
    "${CMAKE_CURRENT_LIST_DIR}/math/bounding_volumes.h" "${CMAKE_CURRENT_LIST_DIR}/math/collision_aabb.h" "${CMAKE_CURRENT_LIST_DIR}/math/collision_detail.h" "${CMAKE_CURRENT_LIST_DIR}/math/collision.h" "${CMAKE_CURRENT_LIST_DIR}/math/collision_plane.h"
    "${CMAKE_CURRENT_LIST_DIR}/math/collision_sdf.h" "${CMAKE_CURRENT_LIST_DIR}/math/collision_triangle.h" "${CMAKE_CURRENT_LIST_DIR}/math/constant.h" "${CMAKE_CURRENT_LIST_DIR}/math/convert.h" "${CMAKE_CURRENT_LIST_DIR}/math/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/math/matrix.h" "${CMAKE_CURRENT_LIST_DIR}/math/quaternion.h" "${CMAKE_CURRENT_LIST_DIR}/math/type.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector2.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector3.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector4.h"
    "${CMAKE_CURRENT_LIST_DIR}/math/vector_arithmetic.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_compare.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_construct.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_exp_log.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_interp.h"
    "${CMAKE_CURRENT_LIST_DIR}/math/vector_inverse_trig.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_lane.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_matrix.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_transform.h" "${CMAKE_CURRENT_LIST_DIR}/math/vector_trig.h"
    "${CMAKE_CURRENT_LIST_DIR}/math/vector.h" "${CMAKE_CURRENT_LIST_DIR}/mesh/triangle_area.h" "${CMAKE_CURRENT_LIST_DIR}/scope_exit.h" "${CMAKE_CURRENT_LIST_DIR}/span.h" "${CMAKE_CURRENT_LIST_DIR}/string_view.h" "${CMAKE_CURRENT_LIST_DIR}/sha256.h"
    "${CMAKE_CURRENT_LIST_DIR}/termination.h" "${CMAKE_CURRENT_LIST_DIR}/terminal_entry.h" "${CMAKE_CURRENT_LIST_DIR}/texture_payload.h" "${CMAKE_CURRENT_LIST_DIR}/text_numeric_format.h")

