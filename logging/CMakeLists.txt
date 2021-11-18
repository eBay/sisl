include_directories(BEFORE include)

file(GLOB API_HEADERS include/*.h)
file(GLOB LIB_HEADERS lib/*.h)
file(GLOB LIB_SOURCES lib/*.cpp)

add_library(sds_logging
    ${API_HEADERS}
    ${LIB_HEADERS}
    ${LIB_SOURCES}
)
