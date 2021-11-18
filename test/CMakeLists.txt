include_directories(BEFORE ../src/include)

#file(GLOB API_HEADERS include/*.h)
#file(GLOB LIB_HEADERS lib/*.h)
#file(GLOB LIB_SOURCES lib/*.cpp)

# Simulate how applications will use this
set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-dynamic")

add_executable(logging_example example.cpp)
target_link_libraries(logging_example sds_logging dl ${CONAN_LIBS})
