nwb_declare_static_library(nwb_ecs)
target_sources(nwb_ecs PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/entity.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/entity_id.h"
    "${CMAKE_CURRENT_LIST_DIR}/entity.h"
    "${CMAKE_CURRENT_LIST_DIR}/component.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
    "${CMAKE_CURRENT_LIST_DIR}/query.h"
    "${CMAKE_CURRENT_LIST_DIR}/message_bus.h"
    "${CMAKE_CURRENT_LIST_DIR}/world.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs.h"
)
target_link_libraries(nwb_ecs PUBLIC nwb_alloc nwb_graphics)
