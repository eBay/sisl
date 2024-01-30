cmake_minimum_required (VERSION 3.11)

add_subdirectory (logging)
add_subdirectory (options)
add_subdirectory (version)
add_subdirectory (sobject)

# These sub-libraries currently do not support MacOS due to dependencies
# on Folly and pistache. It is unknown if Windows is supported...
list(APPEND POSIX_LIBRARIES )
list(APPEND SISL_DEPS  )

if(${folly_FOUND})
    if(${userspace-rcu_FOUND})
        add_subdirectory (grpc)
        list(APPEND POSIX_LIBRARIES
                $<TARGET_OBJECTS:sisl_grpc>
    )
    endif()
    add_subdirectory (cache)
    add_subdirectory (fds)
    add_subdirectory (file_watcher)
    add_subdirectory (flip)
    add_subdirectory (metrics)
    add_subdirectory (settings)
    add_subdirectory (utility)
    add_subdirectory (wisr)

    list(APPEND POSIX_LIBRARIES
        $<TARGET_OBJECTS:sisl_buffer>
        $<TARGET_OBJECTS:sisl_cache>
        $<TARGET_OBJECTS:sisl_file_watcher>
        $<TARGET_OBJECTS:sisl_metrics>
        $<TARGET_OBJECTS:sisl_settings>
    )
    list(APPEND SISL_DEPS
        folly::folly
        breakpad::breakpad
    )
endif()

add_library(sisl
            ${POSIX_LIBRARIES}
            $<TARGET_OBJECTS:sisl_logging>
            $<TARGET_OBJECTS:sisl_options>
            $<TARGET_OBJECTS:sisl_version>
            $<TARGET_OBJECTS:sisl_sobject_mgr>
           )

if (DEFINED MALLOC_IMPL)
    if (${MALLOC_IMPL} STREQUAL "tcmalloc")
        list(APPEND SISL_DEPS gperftools::gperftools)
    endif()
endif()

target_link_libraries(sisl
    ${SISL_DEPS}
)
