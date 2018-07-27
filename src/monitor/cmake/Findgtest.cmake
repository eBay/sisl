# Find the gtest library.
# Output variables:
#  GTEST_INCLUDE_DIR : e.g., /usr/local/include/.
#  GTEST_LIBRARY     : Library path of gtest library
#  GTEST_FOUND       : True if found.

#deps_prefix is relative to the project source directory.
set(deps_prefix ${PROJECT_SOURCE_DIR}/${CMAKE_DEPENDENT_MODULES_DIR})
MESSAGE ("dep_prefix is set: ${deps_prefix}")

# for GTEST, it is up to the directory
FIND_PATH(GTEST_INCLUDE_DIR NAME gtest
  HINTS ${deps_prefix}/include $ENV{HOME}/local/include /opt/local/include /usr/local/include /usr/include)

# have the preference of static library first.
FIND_LIBRARY(GTEST_LIBRARY NAME libgtest.a gtest
  HINTS ${deps_prefix}/lib $ENV{HOME}/local/lib64 $ENV{HOME}/local/lib /usr/local/lib64 /usr/local/lib /opt/local/lib64 /opt/local/lib /usr/lib64 /usr/lib
)

IF (GTEST_INCLUDE_DIR AND GTEST_LIBRARY)
    SET(GTEST_FOUND TRUE)
    MESSAGE(STATUS "Found gtest library: inc=${GTEST_INCLUDE_DIR}, lib=${GTEST_LIBRARY}")
ELSE ()
    SET(GTEST_FOUND FALSE)
    MESSAGE(STATUS "WARNING: Gtest library not found.")
    MESSAGE(STATUS "Try: 'sudo apt-get install gtest/gtest-devel' (or sudo yum install gtest/gtest-devel)")
ENDIF ()
