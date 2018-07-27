# Find the Protobuffer library.
# Output variables:
#  Protobuffer_INCLUDE_DIR : e.g., /usr/local/include/.
#  Protobuffer_LIBRARY     : Library path of Protobuffer library
#  Protobuffer_FOUND       : True if found.

# for Prometheus library, we force it to find the one that we packaged in monstordb

#deps_prefix is relative to the project source directory.
set(deps_prefix ${PROJECT_SOURCE_DIR}/${CMAKE_DEPENDENT_MODULES_DIR})
MESSAGE ("dep_prefix is set: ${deps_prefix}")

FIND_PATH(PROTOBUFFER_INCLUDE_DIR NAME google
  HINTS ${deps_prefix}/include)

# have the preference of static library first.
FIND_LIBRARY(PROTOBUFFER_LIBRARY NAME libprotobuf.a protobuf
  HINTS ${deps_prefix}/lib
)

IF (PROTOBUFFER_INCLUDE_DIR AND PROTOBUFFER_LIBRARY)
    SET(PROTOBUFFER_FOUND TRUE)
    MESSAGE(STATUS "Found protobuffer library: inc=${PROTOBUFFER_INCLUDE_DIR}, lib=${PROTOBUFFER_LIBRARY}")
ELSE ()
    SET(PROTOBUFFER_FOUND FALSE)
    MESSAGE(STATUS "WARNING: Protobuffer library not found.")
    MESSAGE(STATUS "Try: 'sudo apt-get install protobuf/protobuf-devel' (or sudo yum install protobuf/protobuf-devel)")
ENDIF ()
