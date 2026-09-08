nwb_declare_static_library(nwb_graphics)
target_sources(nwb_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/render_pass.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_contract.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_feature_queries.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_graph_setup.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_setup.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_setup_async.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_texture_upload.cpp"
)

target_link_libraries(nwb_graphics PUBLIC
    nwb_graphics_backend
    nwb_gpu_task
)
