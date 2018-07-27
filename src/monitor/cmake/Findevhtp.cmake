# Find the evhtp library.
# Output variables:
#  EVHTP_INCLUDE_DIR : e.g., /usr/local/include/.
#  EVHTP_LIBRARY     : Library path of evhtp library
#  EVHTP_FOUND       : True if found.

# for evhtp library, we force it to find the one that we packaged in monstordb
#deps_prefix is relative to the project source directory.
set(deps_prefix ${PROJECT_SOURCE_DIR}/${CMAKE_DEPENDENT_MODULES_DIR})
MESSAGE ("dep_prefix is set: ${deps_prefix}")

FIND_PATH(EVHTP_INCLUDE_DIR NAME evhtp
  HINTS ${deps_prefix}/include)

# have the preference of static library first.
FIND_LIBRARY(EVHTP_LIBRARY NAME libevhtp.a evhtp
  HINTS ${deps_prefix}/lib
)

IF (EVHTP_INCLUDE_DIR AND EVHTP_LIBRARY)
    SET(EVHTP_FOUND TRUE)
    MESSAGE(STATUS "Found evhtp library: inc=${EVHTP_INCLUDE_DIR}, lib=${EVHTP_LIBRARY}")
ELSE ()
    SET(EVHTP_FOUND FALSE)
    MESSAGE(STATUS "WARNING: Evhtp library not found.")
    MESSAGE(STATUS "Try: 'sudo apt-get install evhtp/evhtp-devel' (or sudo yum install evhtp/evhtp-devel)")
ENDIF ()
