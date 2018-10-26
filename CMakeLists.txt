set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(BEFORE include)

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
    # Remove tcmalloc from debug builds so valgrind etc. work well
    list(REMOVE_ITEM CONAN_LIBS tcmalloc tcmalloc_minimal)
endif ()

set(SDS_GRPC_LIBS ${CONAN_LIBS} ${CMAKE_THREAD_LIBS_INIT})


set (SDS_GRPC_SOURCE
    lib/client.cpp
    lib/server.cpp
    lib/utils.cpp
)


add_library(sds_grpc ${SDS_GRPC_SOURCE})

 
add_subdirectory(tests)
