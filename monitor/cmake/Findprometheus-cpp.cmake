# Find the prometheus library.
# Output variables:
#  PROMETHEUS_INCLUDE_DIR : e.g., /usr/local/include/.
#  PROMETHEUS_LIBRARY     : Library path of prometheus library
#  PROMETHEUS_FOUND       : True if found.

# for Prometheus library, we force it to find the one that we packaged in monstordb

#deps_prefix is relative to the project source directory.
set(deps_prefix ${PROJECT_SOURCE_DIR}/${CMAKE_DEPENDENT_MODULES_DIR})
MESSAGE ("dep_prefix is set: ${deps_prefix}")

FIND_PATH(PROMETHEUS_INCLUDE_DIR NAME prometheus
  HINTS ${deps_prefix}/include)

# have the preference of static library first.
FIND_LIBRARY(PROMETHEUS_LIBRARY NAME libprometheus-cpp.a  prometheus-cpp
  HINTS ${deps_prefix}/lib
)

IF (PROMETHEUS_INCLUDE_DIR AND PROMETHEUS_LIBRARY)
    SET(PROMETHEUS_FOUND TRUE)
    MESSAGE(STATUS "Found prometheus library: inc=${PROMETHEUS_INCLUDE_DIR}, lib=${PROMETHEUS_LIBRARY}")
ELSE ()
    SET(PROMETHEUS_FOUND FALSE)
    MESSAGE(STATUS "WARNING: Prometheus library not found.")
    MESSAGE(STATUS "Try: 'sudo apt-get install prometheus/prometheus-devel' (or sudo yum install prometheus/prometheus-devel)")
ENDIF ()
