# ROSS CMake Modernization — Task Checklist

See [plan.md](plan.md) for full context and rationale.

## Pre-flight decisions (resolve before Phase 1 lands)
- [ ] Decide the fate of [core/ross-config.in](core/ross-config.in) — shell-script consumer wrapper. If pkg-config + CMake config are the supported discovery surfaces going forward, delete it. Otherwise, port its install path to `GNUInstallDirs`-derived vars as part of Step 8

## Phase 0 — Disable Damaris/RISA in CMake (lands first, own PR)

### Step 0 — Remove `USE_DAMARIS` build paths
- [ ] Delete `OPTION(USE_DAMARIS ...)` and the dependent `IF(USE_DAMARIS) ... ADD_SUBDIRECTORY(risa) ... INCLUDE_DIRECTORIES(${DAMARIS_INCLUDE}) ... ENDIF()` block at [core/CMakeLists.txt:119-125](core/CMakeLists.txt#L119-L125)
- [ ] Delete `IF(USE_DAMARIS) INCLUDE_DIRECTORIES(${DAMARIS_INCLUDE}) ENDIF` at [models/phold/CMakeLists.txt:2-4](models/phold/CMakeLists.txt#L2-L4)
- [ ] Delete the `IF(USE_DAMARIS) ... ELSE` Damaris branch at [models/phold/CMakeLists.txt:27-37](models/phold/CMakeLists.txt#L27-L37) — collapse to the plain `target_link_libraries(... ROSS m)` form
- [ ] Leave `core/risa/` submodule and `#cmakedefine USE_DAMARIS` in `config.h.in` in place — both inert without the CMake option, and a future PR can rip them out
- [ ] Update [CLAUDE.md](CLAUDE.md): remove the Damaris/RISA bullet from "Optional subsystems"; note Damaris is disabled pending future removal of C source paths
- [ ] Verify a clean configure + build still succeeds with `USE_DAMARIS` no longer existing as a cache variable (run `cmake --preset ross-debug && cmake --build --preset ross-debug`)

## Phase 1 — Export story (unblocks `codes` using `find_package(ross CONFIG)`)

### Step 1 — Bump `cmake_minimum_required` and fix top-level `project()`
- [ ] Bump top-level `cmake_minimum_required` to 3.21
- [ ] Move `git_describe_working_tree` call from `core/CMakeLists.txt` up to top-level `CMakeLists.txt` (must run before `project()`)
- [ ] Parse `VERSION_MAJOR/MINOR/PATCH` from git describe output
- [ ] Compute `ROSS_DETECTED_VERSION` as `${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}`
- [ ] **Add fallback**: if `ROSS_DETECTED_VERSION` doesn't match `^[0-9]+\.[0-9]+\.[0-9]+$` (tarball / shallow / no-tag build), default to `0.0.0` and emit a warning. `project(VERSION ...)` errors on malformed input — this is load-bearing, not cosmetic
- [ ] Replace `PROJECT(ROSS_TOP C)` with `project(ross VERSION ${ROSS_DETECTED_VERSION} DESCRIPTION ... HOMEPAGE_URL ... LANGUAGES C)`
- [ ] Keep `VERSION_SHA1` extraction (for `config.h`) — move it to after `project()`
- [ ] Verify `${ross_VERSION}`, `${ross_VERSION_MAJOR}` etc. are populated
- [ ] Regression test: delete `.git/` from a copy of the tree, reconfigure, confirm build succeeds with `0.0.0`

### Step 2 — Drop nested `project()` in `core/` and set up target hygiene
- [ ] Delete `PROJECT(ROSS C)` line in `core/CMakeLists.txt`
- [ ] **Sweep `${ROSS_SOURCE_DIR}` / `${ROSS_BINARY_DIR}` references** — both become empty after the nested project goes away. Without this sweep, four `INSTALL(...)` lines and three `INCLUDE_DIRECTORIES(...)` lines silently install/look in `/`:
  - [ ] `core/CMakeLists.txt`: replace `${ROSS_SOURCE_DIR}` → `${CMAKE_CURRENT_SOURCE_DIR}` and `${ROSS_BINARY_DIR}` → `${CMAKE_CURRENT_BINARY_DIR}` (lines 2, 187, 188, 189, 193)
  - [ ] `models/phold/CMakeLists.txt`: delete `INCLUDE_DIRECTORIES(${ROSS_BINARY_DIR})` and `INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ...)` lines outright at [models/phold/CMakeLists.txt:1-9](models/phold/CMakeLists.txt#L1-L9) — Step 3's `BUILD_INTERFACE` propagation via `ross::ross` covers them. The raw-path install at [line 58](models/phold/CMakeLists.txt#L58) is replaced by Step 11
  - [ ] **Step 11 must land in the same PR as Step 2** — without it, the install at line 58 is broken. See updated Phase 1 sequencing
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
- [ ] Set `target_link_libraries(ross PUBLIC MPI::MPI_C)` — MPI must be `PUBLIC` because [core/ross.h:81](core/ross.h#L81) does `#include <mpi.h>` (confirmed, not a hedge)
- [ ] Add `target_link_libraries(ross PRIVATE m)` for libm

### Step 5 — Generate proper package config files
- [ ] Create `core/cmake/rossConfig.cmake.in` template with `@PACKAGE_INIT@`, `find_dependency(MPI REQUIRED COMPONENTS C)`, include of `rossTargets.cmake`
- [ ] Template should set `ROSS_INCLUDE_DIRS`, `ROSS_LIBRARIES`, `ROSS_FOUND` back-compat variables
- [ ] Add `include(GNUInstallDirs)` and `include(CMakePackageConfigHelpers)` in `core/CMakeLists.txt`
- [ ] Replace `INSTALL(TARGETS ROSS EXPORT ROSS-targets DESTINATION lib)` with full `install(TARGETS ross EXPORT rossTargets LIBRARY/ARCHIVE/RUNTIME/INCLUDES DESTINATION ...)` form
- [ ] Add `install(EXPORT rossTargets FILE rossTargets.cmake NAMESPACE ross:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ross)`
- [ ] Call `configure_package_config_file(... PATH_VARS CMAKE_INSTALL_INCLUDEDIR)` — the `PATH_VARS` is required for `@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@` expansion inside the template's `ROSS_INCLUDE_DIRS` line
- [ ] Call `write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)`
- [ ] Install generated `rossConfig.cmake` + `rossConfigVersion.cmake` to `${CMAKE_INSTALL_LIBDIR}/cmake/ross`
- [ ] Add `export(EXPORT rossTargets ... NAMESPACE ross::)` for build-tree / FetchContent consumers
- [ ] Delete old `core/ROSSConfig.cmake`
- [ ] (Optional) Add uppercase back-compat shim at `${CMAKE_INSTALL_LIBDIR}/cmake/ROSS/ROSSConfig.cmake`

### Step 6 — Install headers under `include/ross/`
- [ ] Replace catch-all header install with scoped `install(DIRECTORY ... DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross ...)` (exclude `cmake`, `risa` — `risa` exclude is defensive since Step 0 already disabled the build path)
- [ ] Install generated `config.h` to `${CMAKE_INSTALL_INCLUDEDIR}/ross` **without renaming** — the `/ross/` subdir already isolates it from consumer collisions, and renaming would require edits to [core/ross-base.h](core/ross-base.h) and [core/ross-random.h](core/ross-random.h) which `#include "config.h"`. Earlier draft said to rename to `ross-config-build.h`; that's been dropped
- [ ] Verify `#include <ross.h>` still resolves via exported INCLUDES destination
- [ ] Verify installed `ross-base.h`'s `#include "config.h"` resolves (quoted-include search finds `config.h` in the same installed directory)

### Step 7 — Fix `ross.pc` pkg-config file
- [ ] Drop quotes around `prefix = "..."` in `core/ross.pc.in`
- [ ] Use `prefix=@CMAKE_INSTALL_PREFIX@`, `libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@`, `includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@/ross`
- [ ] Use `@PROJECT_VERSION@` for the `Version:` field
- [ ] Install `ross.pc` to `${CMAKE_INSTALL_LIBDIR}/pkgconfig`
- [ ] **Do NOT add MPI flags** to `Cflags`/`Libs`. Intent: pkg-config is back-compat only. Consumers compile with `mpicc`, so MPI is transparent. The CMake config (Step 5) is the supported path going forward; `ross.pc` stays minimal until it's deprecated

### Phase 1 validation
- [ ] Build ROSS with current presets: `cmake --preset ... && cmake --build ... && cmake --install ...`
- [ ] Inspect installed `rossTargets.cmake` — confirm no absolute MPI paths in `INTERFACE_*` properties. Concrete check: `grep -rE "/opt/homebrew|/usr/local/Cellar|/usr/lib/x86_64-linux-gnu" <prefix>/lib/cmake/ross/` must return zero hits
- [ ] Build a tiny test consumer **from a non-sibling directory** (e.g., `/tmp/ross-consumer/`) with `find_package(ross CONFIG REQUIRED)` + `target_link_libraries(t PRIVATE ross::ross)` — catches hidden `${PROJECT_SOURCE_DIR}` assumptions that pass when the consumer is adjacent to the source
- [ ] Build `codes` against the new install — confirm pkg-config flow still works
- [ ] Tarball regression: delete `.git/` in a copy of the source, reconfigure, confirm Step 1's `0.0.0` fallback engages without error

### Step 11 (now Phase 1) — Update `models/phold/CMakeLists.txt` to use namespaced target
**Moved from Phase 3 because Step 2's removal of `${ROSS_BINARY_DIR}` invalidates the raw-path install — Step 11's `install(TARGETS ...)` rewrite is the fix. Must land in the same PR as Step 2.**

- [ ] Define `set(phold_targets phold phold_comm_test phold_gvt_hook_test phold_gvt_hook_model_test phold_gvt_hook_timestamp_test phold_gvt_hook_comm_test)`
- [ ] Replace per-target `TARGET_LINK_LIBRARIES(... ROSS m)` lines with a `foreach(t ${phold_targets}) target_link_libraries(${t} PRIVATE ross::ross m) endforeach()` (Damaris branch is gone — Step 0 removed `USE_DAMARIS`)
- [ ] Audit the `BGPM` branch — top-level option is `USE_BGPM`, the phold branch keys on `BGPM` (likely already broken). If dead, delete; if live, gate on `USE_BGPM` instead
- [ ] Replace raw-file-path install at [models/phold/CMakeLists.txt:58](models/phold/CMakeLists.txt#L58) with `install(TARGETS ${phold_targets} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})` — install all 6 variants, not just `phold`
- [ ] **Keep** the `add_library(ROSS ALIAS ross)` shim from Step 2 — cheap insurance for any out-of-tree code still referencing bare `ROSS`. Remove in a follow-up after a soak period

### Phase 1 sequencing note
- **Steps 2 + 11 must land together** — Step 2 deletes `${ROSS_BINARY_DIR}`, which Step 11 replaces with `install(TARGETS ...)`. Splitting them leaves a broken install line referencing an empty variable.
- **Steps 3 / 5 / 6 / 7 must land together** (separate commits fine for review). Step 3's `INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ross` commits to the Step 6 install layout, and Step 7's `ross.pc` `includedir` must match both. Splitting across PRs leaves intermediate states with exported targets pointing at paths that don't exist yet.
- Steps 1 and 4 can each ship independently.
- Pragmatic option: bundle all of Phase 1 into a single PR with one commit per step — interdependencies are tight enough that staged review across multiple PRs costs more than it gains.

---

## Phase 2 — Correctness/portability

### Step 8 — `GNUInstallDirs` everywhere
- [ ] Add `include(GNUInstallDirs)` near top of top-level `CMakeLists.txt` (after `project()`)
- [ ] Replace every `DESTINATION lib` with `DESTINATION ${CMAKE_INSTALL_LIBDIR}`
- [ ] Replace every `DESTINATION bin` with `DESTINATION ${CMAKE_INSTALL_BINDIR}`
- [ ] Replace every `DESTINATION include` with `DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}` (or `.../ross` where needed)
- [ ] Audit `models/phold/CMakeLists.txt` for hard-coded paths
- [ ] **Fix the RPATH stanza** at [CMakeLists.txt:35-37](CMakeLists.txt#L35-L37) — both the `LIST(FIND ... isSystemDir)` check and the `SET(CMAKE_INSTALL_RPATH ...)` line hard-code `${CMAKE_INSTALL_PREFIX}/lib`. Change to `${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}`. Different construction than `DESTINATION lib`, so the sweep above won't catch it

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
- [ ] **Delete** the stray `SET(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")` lines at [CMakeLists.txt:117](CMakeLists.txt#L117) (x86_64 branch) and [CMakeLists.txt:124](CMakeLists.txt#L124) (aarch64 branch) — CXX isn't an enabled language, they're dead copy-paste mangles

### Step 14 — Move `ROSS_OPTION_LIST` to target compile definition
- [ ] Remove `SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DROSS_OPTION_LIST=...")` line
- [ ] Add `target_compile_definitions(ross PRIVATE ROSS_OPTION_LIST="${OPTIONS}")`

---

## Phase 3 — Polish / dogfood

### Step 10 — Replace `FILE(GLOB_RECURSE)` in `models/CMakeLists.txt`
- [ ] Replace recursive glob with explicit `add_subdirectory(phold)` (and any other models). Note: the build-dir hazard is modest in practice (build dir is `ross/build/`, not `ross/models/build/`) — the real reason is that globs skip new/removed files until reconfigure and `FOLLOW_SYMLINKS` is a footgun

### Step 11 — moved to Phase 1 (see above)

### Step 12 — Use `MPIEXEC_*` variables in test functions
- [ ] Convert positional `ADD_TEST(name cmd args)` to keyword `add_test(NAME ... COMMAND ...)` form — **prerequisite**, since the positional form does not expand generator expressions
- [ ] Update `ROSS_TEST_SCHEDULERS` function to use `${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${MPIEXEC_PREFLAGS} ... ${MPIEXEC_POSTFLAGS}`
- [ ] Update `ROSS_TEST_INSTRUMENTATION` function similarly
- [ ] Switch target path to `$<TARGET_FILE:${target_name}>` generator expression (drops the implicit CWD dependency from `./${target_name}`)

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
