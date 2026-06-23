if(NOT TARGET RmvParser)
    add_subdirectory(source/parser)
endif()

if(NOT TARGET RmvBackend)
    add_subdirectory(source/backend)
endif()
