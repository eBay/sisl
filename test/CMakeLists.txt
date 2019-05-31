set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-local-typedefs")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

include_directories(BEFORE ../src/include)

#file(GLOB API_HEADERS include/*.h)
#file(GLOB LIB_HEADERS lib/*.h)
#file(GLOB LIB_SOURCES lib/*.cpp)

# Simulate how applications will use this
set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-export-dynamic")

add_executable(logging_example example.cpp)
target_link_libraries(logging_example sds_logging dl ${CONAN_LIBS})
