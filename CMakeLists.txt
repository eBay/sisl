set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)

include(${CMAKE_HOME_DIRECTORY}/cmake/grpc.cmake)

include_directories(BEFORE include)

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
    # Remove tcmalloc from debug builds so valgrind etc. work well
    list(REMOVE_ITEM CONAN_LIBS tcmalloc tcmalloc_minimal)
endif ()

set(GRPC_HELPER_LIBS ${CONAN_LIBS} ${CMAKE_THREAD_LIBS_INIT})


set (GRPC_HELPER_SOURCE
    lib/rpc_server.cpp
    lib/rpc_client.cpp
)


add_library(grpc_helper ${GRPC_HELPER_SOURCE})
 
add_subdirectory(tests)
