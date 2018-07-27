# Output variables:
#  EVENT_LIBRARY     : Library path of libevent library
#  EVENT_FOUND       : True if found.
#  the include path is assumed to be side-by-side at the repository level.

#deps_prefix is relative to the project source directory.
set(deps_prefix ${PROJECT_SOURCE_DIR}/${CMAKE_DEPENDENT_MODULES_DIR})
MESSAGE ("dep_prefix is set: ${deps_prefix}")

# have the preference of static library first.
FIND_LIBRARY(EVENT_LIBRARY NAME libevent.a event
  HINTS ${deps_prefix}/lib
)

IF (EVENT_LIBRARY)
    SET(EVENT_FOUND TRUE)
    MESSAGE(STATUS "Found event library: lib=${EVENT_LIBRARY}")
ELSE ()
    SET(EVENT_FOUND FALSE)
    MESSAGE(STATUS "WARNING: event library not found.")
ENDIF ()
