SET(ross_srcs
  ross-random.c
  rand-${RAND}.c
  network-${NETWORK}.c
  hash-quadratic.c
  buddy.c
  lz4.c
  tw-state.c
  ross-global.c
  tw-event.c
  tw-opts.c
  tw-pe.c
  tw-kp.c
  tw-lp.c
  tw-timing.c
  tw-sched.c
  tw-setup.c
  tw-stats.c
  tw-util.c
)

set(ross_headers
  ross-random.h
  rand-${RAND}.h
  ross-gvt.h
  network-${NETWORK}.h
  hash-quadratic.h
  buddy.h
  lz4.h
  ross-extern.h
  ross-inline.h
  ross-kernel-inline.h
  ross.h
  ross-types.h
  tw-eventq.h
  tw-opts.h
  tw-timing.h
)

# ROSS VERSION INFORMATION
## Print Date and Time at top of ROSS output
INCLUDE (CheckFunctionExists)
CHECK_FUNCTION_EXISTS(ctime HAVE_CTIME)

## Print ROSS Git Hash
# From http://stackoverflow.com/questions/1435953/how-can-i-pass-git-sha1-to-compiler-as-definition-using-cmake
# Now following this approach (which is based on the previous):
# http://ipenguin.ws/2012/11/cmake-automatically-use-git-tags-as.html
# This way lets us use the actual version numbers of ROSS, instead of the git commit
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
INCLUDE(GetGitRevisionDescription)
#GET_GIT_HEAD_REVISION(GIT_REFSPEC GIT_SHA1)

# changed to look at the working tree and not the latest commit hash -- allows for use of --dirty.
git_describe_working_tree(VERSION --tags --dirty)
message(STATUS "ROSS VERSION=${VERSION}")

#parse the version information into pieces.
string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" ROSS_VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" ROSS_VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" ROSS_VERSION_PATCH "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.[0-9]+(.*)" "\\1" ROSS_VERSION_SHA1 "${VERSION}")
# VERSION_SHORT used in the ross.pc file
set(ROSS_VERSION_SHORT "${ROSS_VERSION_MAJOR}.${ROSS_VERSION_MINOR}.${ROSS_VERSION_PATCH}")


# Data Structure for remote Events
# If AVL_TREE is OFF, ROSS reverts to hashing
OPTION(AVL_TREE "Use AVL trees for optimistic mode events? (hash tables otherwise)" ON)
IF(AVL_TREE)
  list(APPEND ross_srcs avl_tree.c)
  list(APPEND ross_headers avl_tree.h)
ENDIF(AVL_TREE)

# RIO: Restart IO
OPTION(USE_RIO "Enable RIO checkpointing library?" OFF)
IF(USE_RIO)
  list(APPEND ross_srcs
    rio/io-mpi.c
    rio/io-serialize.c
  )
  INCLUDE_DIRECTORIES(rio)
ENDIF(USE_RIO)

# Damaris I/O and data management
OPTION(USE_DAMARIS "Build with Damaris library (for in situ vis/analysis)?" OFF)
if (USE_DAMARIS)
    ADD_SUBDIRECTORY(risa)
    INCLUDE_DIRECTORIES(${DAMARIS_INCLUDE})
    list(APPEND ross_headers ${ROSS_Damaris_SOURCE_DIR}/core/damaris.h)
ENDIF(USE_DAMARIS)

# Use deterministic unbiased RNG tiebreaker for event ties
OPTION(USE_RAND_TIEBREAKER "Build with deterministic unbiased tiebreaker for event ties" ON)

# Use debugging-friendly memory allocation
OPTION(ROSS_ALLOC_DEBUG "Use naive allocator to be more friendly to memory debugging tools" OFF)

OPTION(RAND_NORMAL "Turn on state for normal distribution" ON)

# Show timing data at end of run
OPTION(ROSS_timing "Perform ROSS timings" ON)

# Used by ross-kernel-inline.h
OPTION(ROSS_runtime_checks "Perform ID checks" OFF)
IF(CMAKE_BUILD_TYPE MATCHES Debug)
	SET(ROSS_runtime_checks ON CACHE BOOL "Perform ID checks" FORCE)
ENDIF(CMAKE_BUILD_TYPE MATCHES Debug)

# Set all options
#SET(OPTIONS "${OPTIONS} -DROSS_QUEUE_${QUEUE} -DROSS_RAND_${RAND} -DROSS_NETWORK_mpi -DROSS_CLOCK_${CLOCK} -DROSS_GVT_${GVT} -DARCH_${ARCH}")
#SET(OPTIONS "${OPTIONS} -DROSS_QUEUE_${QUEUE} -DROSS_RAND_${RAND} -DROSS_NETWORK_mpi -DROSS_CLOCK_${CLOCK} -DROSS_GVT_${GVT} -DARCH_${ARCH}" PARENT_SCOPE)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DROSS_OPTION_LIST='\"${OPTIONS}\"'")

# Generate Library
OPTION(ROSS_BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
SET(BUILD_SHARED_LIBS ${ROSS_BUILD_SHARED_LIBS})
ADD_LIBRARY(ROSS ${ross_srcs})
add_library(ROSS::ross ALIAS ROSS)
SET_TARGET_PROPERTIES(ROSS PROPERTIES OUTPUT_NAME ROSS)
TARGET_LINK_LIBRARIES(ROSS PUBLIC ${ROSS_EXTERNAL_LIBS})
#TARGET_INCLUDE_DIRECTORIES(ROSS INTERFACE ${MPI_C_INCLUDE_PATH})
TARGET_INCLUDE_DIRECTORIES(ROSS
  PUBLIC $<BUILD_INTERFACE:${ROSS_SOURCE_DIR}/core>
         $<BUILD_INTERFACE:${ROSS_BINARY_DIR}/core>
         $<INSTALL_INTERFACE:include>)

if (COVERALLS)
    set(COVERAGE_SRCS ${ross_srcs})
    # Don't check coverage on lz4
    list(REMOVE_ITEM COVERAGE_SRCS lz4.h lz4.c)

    # Create the coveralls target.
    coveralls_setup(
        "${COVERAGE_SRCS}" # The source files.
        OFF)               # If we should upload.

endif()

add_subdirectory(clock)
add_subdirectory(gvt)
add_subdirectory(instrumentation)
add_subdirectory(queue)
#TODO need to appropriately handle with risa and rio

#-------------------------------------------------------------------------------
# installation

include (GNUInstallDirs)
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/ROSS)

install(TARGETS ROSS
  EXPORT ROSS-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(EXPORT ROSS-targets
    FILE ROSS-targets.cmake
  NAMESPACE ROSS::
  DESTINATION ${INSTALL_CONFIGDIR})

# generate the config file that includes the exports
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${ROSS_BINARY_DIR}/ROSSConfigVersion.cmake
    VERSION ${ROSS_VERSION_SHORT}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(${ROSS_SOURCE_DIR}/cmake/Config.cmake.in
  "${ROSS_BINARY_DIR}/ROSSConfig.cmake"
  INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
  )
install(FILES
  "${ROSS_BINARY_DIR}/ROSSConfig.cmake"
  "${ROSS_BINARY_DIR}/ROSSConfigVersion.cmake"
  DESTINATION ${INSTALL_CONFIGDIR})

export(EXPORT ROSS-targets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/ROSS-targets.cmake
    NAMESPACE ROSS::)

install(FILES ${ross_headers} DESTINATION include)


# Build Specific Config Header
CONFIGURE_FILE(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
SET_SOURCE_FILES_PROPERTIES(${CMAKE_CURRENT_BINARY_DIR}/config.h PROPERTIES GENERATED TRUE)


## CODES config bin
SET(ROSS_CC $ENV{CC})
SET(ROSS_CXX $ENV{CXX})
CONFIGURE_FILE(ross-config.in ${CMAKE_CURRENT_BINARY_DIR}/ross-config @ONLY)
CONFIGURE_FILE(ross.pc.in ${CMAKE_CURRENT_BINARY_DIR}/ross.pc @ONLY)
SET_SOURCE_FILES_PROPERTIES(${CMAKE_CURRENT_BINARY_DIR}/ross.pc PROPERTIES GENERATED TRUE)


## Make Install
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/ross-config DESTINATION bin PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/config.h DESTINATION include)
#INSTALL(DIRECTORY ${ROSS_SOURCE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/ross.pc DESTINATION lib/pkgconfig)
