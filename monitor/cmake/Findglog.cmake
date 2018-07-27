# Find the glog library.
# Output variables:
#  GLOG_INCLUDE_DIR : e.g., /usr/local/include/.
#  GLOG_LIBRARY     : Library path of glog library
#  GLOG_FOUND       : True if found.

# for GLOG, we force it to find the one that we packaged in monstordb
#deps_prefix is relative to the project source directory.
set(deps_prefix ${PROJECT_SOURCE_DIR}/${CMAKE_DEPENDENT_MODULES_DIR})
MESSAGE ("dep_prefix is set: ${deps_prefix}")

FIND_PATH(GLOG_INCLUDE_DIR NAME glog
  HINTS ${deps_prefix}/include)

# have the preference of static library first.
# let's choose instead the dymamic library for testing purpose
FIND_LIBRARY(GLOG_LIBRARY NAME glog
  HINTS ${deps_prefix}/lib
)

IF (GLOG_INCLUDE_DIR AND GLOG_LIBRARY)
    SET(GLOG_FOUND TRUE)
    MESSAGE(STATUS "Found glog library: inc=${GLOG_INCLUDE_DIR}, lib=${GLOG_LIBRARY}")
ELSE ()
    SET(GLOG_FOUND FALSE)
    MESSAGE(STATUS "WARNING: Glog library not found.")
    MESSAGE(STATUS "Try: 'sudo apt-get install glog/glog-devel' (or sudo yum install glog/glog-devel)")
ENDIF ()
