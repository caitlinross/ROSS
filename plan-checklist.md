# ROSS CMake Modernization — Task Checklist

See [plan.md](plan.md) for full context and rationale.

## Phase 1 — Export story (unblocks `codes` using `find_package(ross CONFIG)`)

### Step 1 — Bump `cmake_minimum_required` and fix top-level `project()`
- [ ] Bump top-level `cmake_minimum_required` to 3.21
- [ ] Move `git_describe_working_tree` call from `core/CMakeLists.txt` up to top-level `CMakeLists.txt`
- [ ] Compute `ROSS_DETECTED_VERSION` as `${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}` before `project()`
- [ ] Replace `PROJECT(ROSS_TOP C)` with `project(ross VERSION ${ROSS_DETECTED_VERSION} DESCRIPTION ... HOMEPAGE_URL ... LANGUAGES C)`
- [ ] Verify `${ross_VERSION}`, `${ross_VERSION_MAJOR}` etc. are populated

### Step 2 — Drop nested `project()` in `core/` and set up target hygiene
- [ ] Delete `PROJECT(ROSS C)` line in `core/CMakeLists.txt`
- [ ] Remove `INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ${ROSS_BINARY_DIR})` directory-scope include (replaced in step 3)
- [ ] Rename target: `add_library(ross ${ross_srcs})`
- [ ] Add `add_library(ross::ross ALIAS ross)`
- [ ] Add back-compat alias `add_library(ROSS ALIAS ross)` for in-tree models during transition
- [ ] Set `OUTPUT_NAME ROSS` and `EXPORT_NAME ross` on the target (preserves `libROSS.a` filename)
- [ ] Replace plain `TARGET_LINK_LIBRARIES(ROSS ...)` with keyword-signature form

### Step 3 — Attach include dirs with generator expressions
- [ ] Add `target_include_directories(ross PUBLIC ...)` with `$<BUILD_INTERFACE:...>` for source + binary dir
- [ ] Add `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ross>` for installed consumers

### Step 4 — Use `MPI::MPI_C` imported target (most important for relocatability)
- [ ] Ensure `find_package(MPI REQUIRED COMPONENTS C)` runs at top level (update `SetupMPI.cmake` or replace)
- [ ] Delete `INCLUDE_DIRECTORIES(${MPI_C_INCLUDE_PATH})` from top-level
- [ ] Delete `LIST(APPEND ROSS_EXTERNAL_LIBS ${MPI_C_LIBRARIES})` from top-level
- [ ] Delete `TARGET_INCLUDE_DIRECTORIES(ROSS INTERFACE ${MPI_C_INCLUDE_PATH})` from `core/`
- [ ] Verify whether public headers include `<mpi.h>`; set `target_link_libraries(ross PUBLIC MPI::MPI_C)` if yes, PRIVATE otherwise
- [ ] Add `target_link_libraries(ross PRIVATE m)` for libm

### Step 5 — Generate proper package config files
- [ ] Create `core/cmake/rossConfig.cmake.in` template with `@PACKAGE_INIT@`, `find_dependency(MPI REQUIRED COMPONENTS C)`, include of `rossTargets.cmake`
- [ ] Template should set `ROSS_INCLUDE_DIRS`, `ROSS_LIBRARIES`, `ROSS_FOUND` back-compat variables
- [ ] Add `include(GNUInstallDirs)` and `include(CMakePackageConfigHelpers)` in `core/CMakeLists.txt`
- [ ] Replace `INSTALL(TARGETS ROSS EXPORT ROSS-targets DESTINATION lib)` with full `install(TARGETS ross EXPORT rossTargets LIBRARY/ARCHIVE/RUNTIME/INCLUDES DESTINATION ...)` form
- [ ] Add `install(EXPORT rossTargets FILE rossTargets.cmake NAMESPACE ross:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ross)`
- [ ] Call `configure_package_config_file(...)` to generate `rossConfig.cmake`
- [ ] Call `write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)`
- [ ] Install generated `rossConfig.cmake` + `rossConfigVersion.cmake` to `${CMAKE_INSTALL_LIBDIR}/cmake/ross`
- [ ] Add `export(EXPORT rossTargets ... NAMESPACE ross::)` for build-tree / FetchContent consumers
- [ ] Delete old `core/ROSSConfig.cmake`
- [ ] (Optional) Add uppercase back-compat shim at `${CMAKE_INSTALL_LIBDIR}/cmake/ROSS/ROSSConfig.cmake`

### Step 6 — Install headers under `include/ross/`
- [ ] Replace catch-all header install with curated `install(DIRECTORY ... DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross ...)` (exclude `cmake`, `risa`)
- [ ] Install generated `config.h` renamed to `ross-config-build.h` (avoid generic name collision)
- [ ] Update internal `#include "config.h"` sites in `core/*.c` to use new name (or wrapper header)
- [ ] Verify `#include <ross.h>` still resolves via exported INCLUDES destination

### Step 7 — Fix `ross.pc` pkg-config file
- [ ] Drop quotes around `prefix = "..."` in `core/ross.pc.in`
- [ ] Use `prefix=@CMAKE_INSTALL_PREFIX@`, `libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@`, `includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@/ross`
- [ ] Use `@PROJECT_VERSION@` for the `Version:` field
- [ ] Install `ross.pc` to `${CMAKE_INSTALL_LIBDIR}/pkgconfig`

### Phase 1 validation
- [ ] Build ROSS with current presets: `cmake --preset ... && cmake --build ... && cmake --install ...`
- [ ] Inspect installed `rossTargets.cmake` — confirm no absolute MPI paths in `INTERFACE_*` properties
- [ ] Build a tiny test consumer with `find_package(ross CONFIG REQUIRED)` + `target_link_libraries(t PRIVATE ross::ross)`
- [ ] Build `codes` against the new install — confirm pkg-config flow still works

---

## Phase 2 — Correctness/portability

### Step 8 — `GNUInstallDirs` everywhere
- [ ] Add `include(GNUInstallDirs)` near top of top-level `CMakeLists.txt` (after `project()`)
- [ ] Replace every `DESTINATION lib` with `DESTINATION ${CMAKE_INSTALL_LIBDIR}`
- [ ] Replace every `DESTINATION bin` with `DESTINATION ${CMAKE_INSTALL_BINDIR}`
- [ ] Replace every `DESTINATION include` with `DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}` (or `.../ross` where needed)
- [ ] Audit `models/phold/CMakeLists.txt` for hard-coded paths

### Step 9 — Gate top-level-only behavior on `PROJECT_IS_TOP_LEVEL`
- [ ] Wrap `include(CTest)` / `enable_testing()` in `if(PROJECT_IS_TOP_LEVEL)`
- [ ] Wrap RPATH variable setup in `if(PROJECT_IS_TOP_LEVEL)`
- [ ] Remove any unconditional `ENABLE_TESTING()`
- [ ] Test that ROSS works as `add_subdirectory()` / `FetchContent_Declare` without hijacking parent config

### Step 13 — Target-scoped compile options, compiler-ID-based arch flags
- [ ] Stop appending to `CMAKE_C_FLAGS` globally for arch-specific flags
- [ ] Convert IBM-XL flags (`-O5 -qprefetch=aggressive -qarch=pwr9`) to `target_compile_options(ross PRIVATE ...)` gated on `CMAKE_C_COMPILER_ID STREQUAL "XL"` (or `"XLClang"`)
- [ ] Gate BGQ / BGP flags on compiler ID, not system processor
- [ ] Convert `-D_GNU_SOURCE` to `target_compile_definitions(ross PRIVATE _GNU_SOURCE)`

### Step 14 — Move `ROSS_OPTION_LIST` to target compile definition
- [ ] Remove `SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DROSS_OPTION_LIST=...")` line
- [ ] Add `target_compile_definitions(ross PRIVATE ROSS_OPTION_LIST="${OPTIONS}")`

---

## Phase 3 — Polish / dogfood

### Step 10 — Replace `FILE(GLOB_RECURSE)` in `models/CMakeLists.txt`
- [ ] Replace recursive glob with explicit `add_subdirectory(phold)` (and any other models)
- [ ] If keeping auto-discovery, at minimum guard against walking into `build/`, `install/`, `test/` dirs

### Step 11 — Dogfood `ross::ross` namespace in phold model
- [ ] Update `models/phold/CMakeLists.txt`: `target_link_libraries(phold PRIVATE ross::ross m)`
- [ ] Same for all 5 other phold variants
- [ ] Replace raw-file-path install with `install(TARGETS ... RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})`
- [ ] Remove the `ROSS` back-compat alias from step 2 once all callsites are migrated

### Step 12 — Use `MPIEXEC_*` variables in test functions
- [ ] Update `ROSS_TEST_SCHEDULERS` function to use `${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${MPIEXEC_PREFLAGS} ... ${MPIEXEC_POSTFLAGS}`
- [ ] Update `ROSS_TEST_INSTRUMENTATION` function similarly
- [ ] Switch target path to `$<TARGET_FILE:${target_name}>` generator expression

### Step 15 — Normalize option names with `ROSS_` prefix (optional)
- [ ] Rename `AVL_TREE` → `ROSS_USE_AVL_TREE`
- [ ] Rename `USE_RIO` → `ROSS_USE_RIO`
- [ ] Rename `USE_DAMARIS` → `ROSS_USE_DAMARIS`
- [ ] Rename `USE_RAND_TIEBREAKER` → `ROSS_USE_RAND_TIEBREAKER`
- [ ] Rename `RAND_NORMAL` → `ROSS_RAND_NORMAL`
- [ ] Rename `COVERALLS` → `ROSS_COVERALLS`
- [ ] Rename `USE_BGPM` → `ROSS_USE_BGPM`
- [ ] Add deprecation shims for each old name (one-release warning then removal)

### Step 16 — Align `CMakePresets.json` with top-level CMake minimum
- [ ] Adjust `cmakeMinimumRequired` so presets match top-level (3.21 or keep 3.28.1 consistently)
- [ ] (Optional) Move `binaryDir` out of `${sourceDir}/build` to avoid in-source-adjacent layout
