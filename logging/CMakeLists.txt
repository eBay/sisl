set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-local-typedefs")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

include_directories(BEFORE include)

file(GLOB API_HEADERS include/*.h)
file(GLOB LIB_HEADERS lib/*.h)
file(GLOB LIB_SOURCES lib/*.cpp)

add_library(sds_logging
  ${API_HEADERS}
  ${LIB_HEADERS}
  ${LIB_SOURCES}
)
