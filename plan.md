# ROSS CMake Modernization Plan

## Part 1 — Survey of the current state

### 1a. Top-level `CMakeLists.txt`

- `cmake_minimum_required(VERSION 3.5)` — far too old. Many modern features assume 3.15+ (e.g. `target_link_libraries(... PRIVATE MPI::MPI_C)` cleanly, `BUILD_INTERFACE` / `INSTALL_INTERFACE` semantics for non-trivial cases, `CMAKE_PROJECT_<name>_INCLUDE`, `install(... FILE_SET HEADERS)` if you want it in 3.23+).
- `PROJECT(ROSS_TOP C)` — no `VERSION`, no `LANGUAGES` keyword. This means `ROSS_TOP_VERSION` is empty and you can't drive the package version file from project metadata.
- `LIST(APPEND CMAKE_MODULE_PATH ...)` is fine.
- Sets `CMAKE_POSITION_INDEPENDENT_CODE ON` globally — fine, and important for static-lib consumers that build shared libs.
- RPATH block is roughly correct, but stomps on the consumer's settings (sets `CMAKE_INSTALL_RPATH`, `CMAKE_SKIP_BUILD_RPATH`, etc. unconditionally even when ROSS is added via `add_subdirectory`).
- Per-architecture `CMAKE_C_FLAGS` mutation via direct string append (not using target properties). Some flags like `-O5`, `-qprefetch=aggressive` are IBM XL specific and applied based purely on `CMAKE_SYSTEM_PROCESSOR` rather than compiler ID, which is broken on, e.g., aarch64 with clang.
- `INCLUDE_DIRECTORIES(${MPI_C_INCLUDE_PATH})` and `LIST(APPEND ROSS_EXTERNAL_LIBS ${MPI_C_LIBRARIES})` — the legacy MPI variable form. Should use `MPI::MPI_C` imported target.
- Doxygen: the `OPTION(ROSS_BUILD_DOXYGEN ...)` is only defined inside `if(DOXYGEN_FOUND)` — fine, but means the option doesn't appear when Doxygen is missing.
- No `include(GNUInstallDirs)`.
- Calls `enable_testing()` unconditionally; should be gated on `BUILD_TESTING` and only at the top level (not when added via `add_subdirectory`).

### 1b. `core/CMakeLists.txt`

- `PROJECT(ROSS C)` again — nested project! This re-runs language detection and confuses `${PROJECT_SOURCE_DIR}` / `${PROJECT_BINARY_DIR}` semantics. Should not be a nested `project()` call; the directory is already inside the top-level project.
- `INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ${ROSS_BINARY_DIR})` — directory-scoped, leaks to siblings, and uses the `_SOURCE_DIR` macro derived from the nested project name, which is brittle.
- Sources are listed manually (good — not globbed). Source list mixes headers + .c (fine for IDE).
- `git_describe_working_tree(VERSION ...)` parsed into `VERSION_MAJOR/MINOR/PATCH/SHA1`. These are project-scope vars that are NOT propagated to `project(... VERSION ...)`. They're only used in `ross.pc` and `config.h`.
- `ADD_LIBRARY(ROSS ${ross_srcs})` — bare target name `ROSS`. No alias, no namespace.
- `TARGET_LINK_LIBRARIES(ROSS ${ROSS_EXTERNAL_LIBS})` — no PUBLIC/PRIVATE/INTERFACE keyword — this is the **plain signature**, which is sticky and cannot mix with the keyword signature later. Also leaks MPI symbols as transitive but with no generator-expression wrapping for build-vs-install.
- `TARGET_INCLUDE_DIRECTORIES(ROSS INTERFACE ${MPI_C_INCLUDE_PATH})` — INTERFACE means downstream sees raw absolute MPI paths in the exported target, which we observed in the generated `ROSS-targets.cmake`:
  ```
  INTERFACE_INCLUDE_DIRECTORIES "/opt/homebrew/Cellar/mpich/4.3.0/include"
  INTERFACE_LINK_LIBRARIES "/opt/homebrew/Cellar/mpich/4.3.0/lib/libmpi.dylib;..."
  ```
  This makes installed packages **non-relocatable** and breaks any consumer with a different MPI.
- ROSS's own include dirs (`${ROSS_SOURCE_DIR}`, `${ROSS_BINARY_DIR}`) are set via the directory-scope `INCLUDE_DIRECTORIES` only — they are NOT attached to the `ROSS` target as `PUBLIC` interface include dirs with `BUILD_INTERFACE` / `INSTALL_INTERFACE`, so `add_subdirectory` consumers and installed consumers both rely on side effects.
- `CONFIGURE_FILE(config.h.in config.h)` produces `${CMAKE_CURRENT_BINARY_DIR}/config.h`. Installed to `include/` — but note this header is unconditionally included by `ross-base.h`/`ross.h` indirectly and pollutes the global include namespace with `config.h` (a very common name — collision risk for consumers).
- `INSTALL(DIRECTORY ${ROSS_SOURCE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")` — installs ALL headers flat into `<prefix>/include`. Hence `/include/avl_tree.h`, `/include/buddy.h`, `/include/lz4.h`, `/include/io.h`, `/include/check-revent/`, `/include/instrumentation/`, etc. all leak into the consumer's include namespace. **Significant pollution risk** (`config.h`, `lz4.h`, `buddy.h` are common names).
- `INSTALL(TARGETS ROSS EXPORT ROSS-targets DESTINATION lib)` + `INSTALL(EXPORT ROSS-targets DESTINATION lib)` — exports are attempted, but…
- `INSTALL(FILES ROSSConfig.cmake DESTINATION lib)` — hand-written, no `Config.cmake.in`, no `configure_package_config_file`, no version file (`ROSSConfigVersion.cmake`), no namespace, no MPI dependency re-resolution, hard-codes `${SELF_DIR}/../include` instead of using `GNUInstallDirs`-derived paths.
- Hand-written `ROSSConfig.cmake` exposes `ROSS_INCLUDE_DIRS` (legacy variable style). Indeed, `codes` consumers rely on `${ROSS_INCLUDE_DIRS}` (see codes `src/CMakeLists.txt` line 166, 205 and `tests/CMakeLists.txt` line 5).
- The exported config is installed to `lib/` instead of the canonical `lib/cmake/ROSS/` (or `lib/cmake/ross/`). `find_package(ROSS)` on most systems will not find it without an explicit `ROSS_DIR` hint.
- The pkg-config file has the wrong syntax on line 1: `prefix = "/Users/.../debug"` — it sets `prefix` literally with quotes, so `-I${prefix}/include` becomes `-I"/Users/.../debug"/include` — that probably works through shell quoting but is wrong by pkg-config grammar. Also `Cflags:` doesn't include MPI flags or any of the `-DROSS_*` definitions from `config.h` macros.
- `SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DROSS_OPTION_LIST='\"${OPTIONS}\"'")` — global flag mutation; should be a `target_compile_definitions` on `ROSS`.

### 1c. `core/ROSSConfig.cmake` (3 lines)

```cmake
GET_FILENAME_COMPONENT(SELF_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
INCLUDE(${SELF_DIR}/ROSS-targets.cmake)
GET_FILENAME_COMPONENT(ROSS_INCLUDE_DIRS "${SELF_DIR}/../include" ABSOLUTE)
```
- No `find_dependency(MPI)` — when consumer does `find_package(ROSS)` and then tries to link against the imported `ROSS` target, the absolute MPI library paths baked into the exported target may not resolve.
- No version file alongside.
- No namespaced alias defined.

### 1d. `models/CMakeLists.txt`

- Uses `FILE(GLOB_RECURSE my_list . FOLLOW_SYMLINKS */CMakeLists.txt)` to enumerate model subdirs. **Glob anti-pattern** — adding/removing models doesn't trigger CMake re-run. Also includes `CMakeLists.txt` files inside `build/`, `install/`, `test/` directories during a configure-in-source-tree (which is what `CMakePresets.json` does because builds go to `${sourceDir}/build/...`).
- `models/phold/CMakeLists.txt` uses `TARGET_LINK_LIBRARIES(phold ROSS m)` — bare target, no namespace. Will break when we add `ross::ross` namespace unless we also keep an `add_library(ROSS ALIAS ross)` (or use the new namespaced name).
- `INSTALL(FILES ${ROSS_BINARY_DIR}/../models/phold/phold ...)` — installs the binary by raw file path, not via `INSTALL(TARGETS phold ...)` — fragile and skips RPATH adjustments.

### 1e. `CMakePresets.json`

- Declares `cmakeMinimumRequired` 3.28.1, which contradicts the `cmake_minimum_required(VERSION 3.5)` in the source. The presets file is fine but reveals the project actually targets a much newer CMake.
- `binaryDir: ${sourceDir}/build/debug` — in-source-adjacent build (under the source tree). Combined with the model `GLOB_RECURSE`, this is a footgun.

### 1f. CMake module dir (`core/cmake/`)

- `SetupMPI.cmake` is BLT-derived, fine. But it doesn't set up the modern `MPI::MPI_C` target use on the consumer side.
- Coveralls scripts: fine, optional.
- `GetGitRevisionDescription.cmake`: fine.

### 1g. Other observations

- No `BUILD_SHARED_LIBS` discipline: there is `ROSS_BUILD_SHARED_LIBS` which sets the global `BUILD_SHARED_LIBS`. Better to either honor the standard CMake `BUILD_SHARED_LIBS` directly, or pass `STATIC`/`SHARED` explicitly.
- No `cmake_policy` block — policies behave as in 3.5.
- Tests (`ROSS_TEST_SCHEDULERS`, `ROSS_TEST_INSTRUMENTATION`) are model-side functions defined in `models/CMakeLists.txt`. They use raw `mpirun` instead of `${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${MPIEXEC_MAX_NUMPROCS}`.

---

## Part 2 — How `codes` consumes ROSS today

From `/Users/caitlin.ross/projects/digital-twin/software/codes/CMakeLists.txt`:

1. **Discovery**: `pkg_check_modules(ROSS REQUIRED IMPORTED_TARGET ross)` — uses the pkg-config `ross.pc` file. It reads `ROSS_PKG_CONFIG_PATH` cache var (set by `CMakePresets.json` to `${workspaceFolder:ross}/install/debug/lib/pkgconfig`) and pushes it into `PKG_CONFIG_PATH`.
2. **What it actually pulls from ROSS**:
   - `PkgConfig::ROSS` imported target → linked to `codes` (`src/CMakeLists.txt` line 92, 174).
   - `${ROSS_INCLUDE_DIRS}` raw include dir variable (`src/CMakeLists.txt` lines 166, 205; `tests/CMakeLists.txt` line 5). This variable comes from pkg-config (`ROSS_INCLUDE_DIRS` is auto-set by `pkg_check_modules`), not from the hand-written `ROSSConfig.cmake`.
   - No compile definitions are pulled from ROSS (the `-DROSS_QUEUE_splay`, `-DROSS_NETWORK_mpi` etc. are written into the installed `config.h`, so they don't need to be re-defined per-consumer).
3. **What does NOT get pulled correctly**:
   - MPI: codes runs its own `find_package(MPI)` via `SetupMPI.cmake`, then `include_directories(${MPI_C_INCLUDE_PATH})` and links `${MPI_C_LIBRARIES}`. Duplicates ROSS's own MPI handling.
   - Compile flags (e.g. `-D_GNU_SOURCE`) ROSS adds.
4. **Compatibility surface to preserve** when migrating:
   - `${ROSS_INCLUDE_DIRS}` variable (used in `src/` and `tests/`) — the new `rossConfig.cmake` should still set this for back-compat.
   - The `ross::ross` (or `ROSS::ROSS`) imported target.
   - Continue producing a working `ross.pc` so that codes can keep using the pkg-config flow until codes is also migrated.

---

## Part 3 — Other CMake pain points found

| # | Issue | Severity |
|---|---|---|
| P1 | No proper package config file — only a hand-written `ROSSConfig.cmake` with no version, no `find_dependency(MPI)`, no namespace, installed to `lib/` not `lib/cmake/ROSS/` | High |
| P2 | Exported target leaks absolute MPI paths into `INTERFACE_INCLUDE_DIRECTORIES`/`INTERFACE_LINK_LIBRARIES` → not relocatable | High |
| P3 | Bare target name `ROSS`, no namespace alias | High |
| P4 | Headers installed flat into `<prefix>/include/` — pollutes consumer include namespace (lz4.h, buddy.h, config.h, io.h, etc.) | High |
| P5 | `cmake_minimum_required(VERSION 3.5)` but presets require 3.28.1 — inconsistent and blocks modern features | Medium |
| P6 | Plain (non-keyword) `target_link_libraries` signature on `ROSS` | Medium |
| P7 | Nested `project()` in `core/` | Medium |
| P8 | No `include(GNUInstallDirs)`; install destinations are hard-coded `lib`, `bin`, `include` (not `${CMAKE_INSTALL_LIBDIR}` etc.) | Medium |
| P9 | `models/CMakeLists.txt` uses `FILE(GLOB_RECURSE)` on subdirs and walks into build/install dirs | Medium |
| P10 | `enable_testing()` runs even when ROSS is added via `add_subdirectory()` | Low |
| P11 | RPATH variables set unconditionally (stomps on parent project) | Low |
| P12 | Architecture flag detection keys on `CMAKE_SYSTEM_PROCESSOR` rather than compiler ID — `-O5 -qarch=...` flags applied to ppc64le with non-XL compilers | Low/Med |
| P13 | `phold` install uses raw file path instead of `INSTALL(TARGETS phold ...)` | Low |
| P14 | Tests hard-code `mpirun` instead of `${MPIEXEC_EXECUTABLE}`/`MPIEXEC_NUMPROC_FLAG` | Low |
| P15 | `ROSS_OPTION_LIST` injected via `CMAKE_C_FLAGS` instead of `target_compile_definitions` | Low |
| P16 | `ross.pc` `prefix = "..."` line uses quotes (non-standard pkg-config syntax) and lacks MPI cflags/libs | Low |
| P17 | Top-level `OPTION` names mix prefixed (`ROSS_*`) and unprefixed (`AVL_TREE`, `USE_RIO`, `USE_DAMARIS`, `RAND_NORMAL`) — all should be prefixed `ROSS_*` to avoid name collisions in superbuilds | Low |
| P18 | `BUILD_TESTING` is set unconditionally (via `enable_testing()` + `include(CTest)`) before checking if we're at top level | Low |

---

## Part 4 — Step-by-step modernization plan

The plan is ordered: do the export-system changes first (steps 1–6), then quality improvements (7–12), then cleanups (13+). Each step is independently mergeable and testable against `codes`.

---

### Step 1 — Bump `cmake_minimum_required` and fix top-level `project()`

**File**: `CMakeLists.txt` (top level)

**Change**:
```cmake
cmake_minimum_required(VERSION 3.21)   # 3.21+ gives us PROJECT_IS_TOP_LEVEL
project(ross
    VERSION ${ROSS_DETECTED_VERSION}   # set after running git-describe
    DESCRIPTION "Rensselaer's Optimistic Simulation System"
    HOMEPAGE_URL "https://github.com/ROSS-org/ROSS"
    LANGUAGES C)
```

Move the `git_describe` block from `core/CMakeLists.txt` up to the top-level so we can pass `VERSION` into `project()`. Set `ROSS_DETECTED_VERSION` to `${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}` derived from the git describe.

Lower-case `ross` as the project name is the modern convention (the package name `find_package(ross)` is then case-insensitive by spec but consistent). It also lets `${ross_VERSION}`, `${ross_VERSION_MAJOR}`, etc. flow naturally from `project()`.

**Why**: enables `PROJECT_IS_TOP_LEVEL`, modern `install(TARGETS ... FILE_SET HEADERS)` (3.23+) if we want it, `target_link_libraries(... PRIVATE MPI::MPI_C)` clean semantics, and lets the package-version file be auto-generated.

**Compat**: any consumer using `find_package(ROSS)` (uppercase) still works — CMake's `find_package` is case-insensitive on the package name; the config file naming changes (see step 4) but we'll ship both `rossConfig.cmake` and a back-compat shim.

---

### Step 2 — Drop the nested `project()` in `core/CMakeLists.txt` and convert to target-based hygiene

**File**: `core/CMakeLists.txt`

**Changes**:
1. Delete `PROJECT(ROSS C)` line. The `core/` directory is part of the parent `ross` project.
2. Replace `INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ${ROSS_BINARY_DIR})` (directory scope) with target-scoped includes (see step 3).
3. Rename the target. Keep the library file output name `ROSS` (so `libROSS.a` stays, preserving pkg-config's `-lROSS`), but use target name `ross` so we can do the modern alias dance:

```cmake
add_library(ross ${ross_srcs})
add_library(ross::ross ALIAS ross)
set_target_properties(ross PROPERTIES
    OUTPUT_NAME ROSS                     # preserves libROSS.a / libROSS.so
    EXPORT_NAME ross)                    # exports as ross::ross via namespace
```

4. Convert `TARGET_LINK_LIBRARIES(ROSS ${ROSS_EXTERNAL_LIBS})` to keyword form (see steps 3 and 5).

**Why**: gives us a namespaced target without breaking the on-disk library filename. Models can later switch from `target_link_libraries(phold ROSS m)` to `target_link_libraries(phold PRIVATE ross::ross m)`. A back-compat alias `add_library(ROSS ALIAS ross)` should also be added so existing in-tree consumers keep working through the transition.

**Compat**: the in-tree `models/phold/CMakeLists.txt` still uses bare `ROSS` — add `add_library(ROSS ALIAS ross)` so it keeps working until step 11.

---

### Step 3 — Attach include directories with `BUILD_INTERFACE`/`INSTALL_INTERFACE`

**File**: `core/CMakeLists.txt`

**Change**:
```cmake
target_include_directories(ross
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>     # for generated config.h
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ross>
)
```

Note the `/ross` subdirectory in the install interface. Headers will move (step 6) to `<prefix>/include/ross/` to fix the namespace pollution.

**Why**: this is the canonical pattern. Build-tree consumers (`add_subdirectory`) use one set of paths, install-tree consumers (`find_package`) use another. Same target description, no manual variable juggling.

---

### Step 4 — Stop leaking absolute MPI paths into the exported target; depend on `MPI::MPI_C`

**Files**: `CMakeLists.txt` (top level), `core/cmake/SetupMPI.cmake`, `core/CMakeLists.txt`

**Changes**:
1. In `SetupMPI.cmake` (or replace with a direct `find_package(MPI REQUIRED COMPONENTS C)` in top-level), drop the `MPI_C_INCLUDE_PATH` / `MPI_C_LIBRARIES` style; rely on `MPI::MPI_C`.
2. In top-level `CMakeLists.txt`, delete `INCLUDE_DIRECTORIES(${MPI_C_INCLUDE_PATH})` and `LIST(APPEND ROSS_EXTERNAL_LIBS ${MPI_C_LIBRARIES})`.
3. In `core/CMakeLists.txt`:
```cmake
target_link_libraries(ross
    PUBLIC  MPI::MPI_C    # PUBLIC because public headers include <mpi.h>
    PRIVATE m)
```
   (Verify whether public headers actually include `<mpi.h>`; if only `network-mpi.c` uses it then `PRIVATE` is correct. From the source layout `ross-network.h` -> `network-mpi.h` -> `<mpi.h>` is plausible — needs a quick check during implementation. Default to `PUBLIC` for safety.)
4. Delete `TARGET_INCLUDE_DIRECTORIES(ROSS INTERFACE ${MPI_C_INCLUDE_PATH})`.

**Why**: `MPI::MPI_C` is a properly-named imported target. When we export the package config and add `find_dependency(MPI COMPONENTS C)`, the consumer's CMake re-resolves MPI on its own machine. **This is the single most important change for relocatability.**

**Compat**: `codes` currently runs its own `find_package(MPI)` — fine, no break. After codes migrates to `find_package(ross CONFIG)`, codes can drop its own MPI block since it'll get `MPI::MPI_C` transitively.

---

### Step 5 — Generate a real package config with `configure_package_config_file` + version file + namespaced export

**Files**:
- New: `core/cmake/rossConfig.cmake.in` (template)
- Modify: `core/CMakeLists.txt`
- Delete (or leave as deprecated shim): `core/ROSSConfig.cmake`

**Changes**:

1. New template `core/cmake/rossConfig.cmake.in`:
```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(MPI REQUIRED COMPONENTS C)

include("${CMAKE_CURRENT_LIST_DIR}/rossTargets.cmake")

# Back-compat variables for consumers still using the old discovery style
set(ROSS_INCLUDE_DIRS "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@/ross")
set(ROSS_LIBRARIES    ross::ross)
set(ROSS_FOUND        TRUE)

check_required_components(ross)
```

2. In `core/CMakeLists.txt` replace the install block:
```cmake
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS ross
    EXPORT rossTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross)

install(EXPORT rossTargets
    FILE rossTargets.cmake
    NAMESPACE ross::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ross)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/rossConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rossConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ross
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/rossConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/rossConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/rossConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ross)

# also export for build-tree (FetchContent / superbuild) consumption
export(EXPORT rossTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/rossTargets.cmake
    NAMESPACE ross::)
```

3. Delete the old `core/ROSSConfig.cmake` file (or, for transition, overwrite it with a deprecation warning that includes the new config). Recommend full deletion.

4. **Optional back-compat shim** for consumers that do `find_package(ROSS)`. Add a tiny installed file `<prefix>/lib/cmake/ROSS/ROSSConfig.cmake` that just `include`s the new one:
```cmake
message(DEPRECATION "find_package(ROSS) is deprecated; use find_package(ross CONFIG) and link ross::ross")
include(${CMAKE_CURRENT_LIST_DIR}/../ross/rossConfig.cmake)
```
   This is optional — only do this if there are external consumers we don't control.

**Why**: this is the heart of the migration. After this, downstream code can do:
```cmake
find_package(ross CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE ross::ross)
```
and get includes, MPI dependency, and all link libs propagated correctly.

**Compat**:
- Codes still works during transition because it uses pkg-config, not `find_package(ROSS)`. We keep `ross.pc` working (step 6).
- The `ROSS_INCLUDE_DIRS` variable is still set by the new `rossConfig.cmake` — codes' `${ROSS_INCLUDE_DIRS}` references keep working when codes does eventually switch to `find_package(ross)`.
- The case-sensitivity shim above handles `find_package(ROSS)`.

---

### Step 6 — Move installed headers under `include/ross/` and clean up `config.h`

**File**: `core/CMakeLists.txt`

**Changes**:
1. Replace the catch-all header install:
```cmake
# OLD:
INSTALL(DIRECTORY ${ROSS_SOURCE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")
```
   With a curated install under `include/ross/`:
```cmake
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "cmake" EXCLUDE
        PATTERN "risa"  EXCLUDE)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross
    RENAME ross-config-build.h)   # avoid generic name collision
```

2. Update internal `#include "config.h"` references in core/*.c to `#include "ross-config-build.h"` (or keep `config.h` as the local name in build tree but install as the renamed one — needs a small wrapper header).

3. For the `INTERFACE` target include path (step 3), point at `${CMAKE_INSTALL_INCLUDEDIR}/ross`. Consumers will use `#include <ross.h>` (which matches existing usage in codes — grep shows `#include <ross.h>` everywhere), and the `ross/` prefix is on the include search path, not in the include statement.

   **Alternative**: install to `include/` (top level) and require consumers use `#include <ross/ross.h>`. This is more strict but breaks all existing `#include <ross.h>` usage in codes. **Recommend the include-path-rooted-at-ross/ approach** to avoid any `.h` source changes.

**Why**: stops dumping `lz4.h`, `buddy.h`, `config.h`, `io.h`, `analysis-lp.h`, etc. into the consumer's include namespace. Eliminates the worst pollution risk.

**Compat**: codes' `#include <ross.h>`, `#include "ross.h"` keeps working because the new install include dir (`<prefix>/include/ross`) becomes the search root via the exported target. No source changes needed in codes.

---

### Step 7 — Fix the `ross.pc` pkg-config file

**File**: `core/ross.pc.in`

**Changes**:
1. Drop the quotes around `prefix = "@CMAKE_INSTALL_PREFIX_ESCAPED@"`.
2. Use proper pkg-config variables:
```
prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@
includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@/ross

Name: ross
Description: Rensselaer's Optimistic Simulation System
URL: https://github.com/ROSS-org/ROSS
Version: @PROJECT_VERSION@
Libs: -L${libdir} -lROSS -lm
Cflags: -I${includedir}
```
3. Install to `${CMAKE_INSTALL_LIBDIR}/pkgconfig`.

**Why**: `codes` uses pkg-config today. Updating `includedir` to point to `<prefix>/include/ross` (matching step 6) means codes' existing `${ROSS_INCLUDE_DIRS}` (set by `pkg_check_modules`) automatically points to the right place. Codes continues to work without changes.

**Compat**: existing codes builds keep working — only the include path changes from `<prefix>/include` to `<prefix>/include/ross`, and codes consumes that via `${ROSS_INCLUDE_DIRS}` so it's transparent.

---

### Step 8 — `include(GNUInstallDirs)` and replace hard-coded `lib/`/`bin/`/`include/`

**Files**: top-level `CMakeLists.txt`, `core/CMakeLists.txt`, `models/phold/CMakeLists.txt`

**Change**: add `include(GNUInstallDirs)` near the top of the top-level file (after `project()`). Replace every `DESTINATION lib`, `DESTINATION bin`, `DESTINATION include` with `${CMAKE_INSTALL_LIBDIR}`, `${CMAKE_INSTALL_BINDIR}`, `${CMAKE_INSTALL_INCLUDEDIR}` respectively.

**Why**: respects multilib systems (`lib64` on RHEL/Fedora) and lets distributions/Spack/superbuilds override per-component install dirs.

---

### Step 9 — Make include-and-test gating respect `add_subdirectory` use

**File**: top-level `CMakeLists.txt`

**Change**:
```cmake
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)   # this calls enable_testing() if BUILD_TESTING=ON
endif()
```
Remove the unconditional `ENABLE_TESTING()` and `INCLUDE(CTest)`.

Similarly gate the RPATH variable manipulation block:
```cmake
if(PROJECT_IS_TOP_LEVEL)
    # rpath stanza
endif()
```

**Why**: makes ROSS embeddable via `add_subdirectory()` or `FetchContent_Declare` without hijacking the parent's testing/RPATH config. Useful for monorepo / superbuild integration.

---

### Step 10 — Replace `FILE(GLOB_RECURSE)` in `models/CMakeLists.txt`

**File**: `models/CMakeLists.txt`

**Change**: replace with explicit `add_subdirectory(phold)` (and any other models). If you really want auto-discovery, at least restrict the glob to skip build/install/test dirs:
```cmake
file(GLOB model_dirs LIST_DIRECTORIES TRUE
     RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*")
foreach(d ${model_dirs})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${d}"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${d}/CMakeLists.txt")
        add_subdirectory(${d})
    endif()
endforeach()
```
But explicit listing is preferred — there are only 1-2 models in tree.

**Why**: globs miss new files until reconfigure; current glob walks into `build/`, `install/`, `test/` (which exist in-source per presets) and will pick up any stale `CMakeLists.txt` under there.

---

### Step 11 — Update `models/phold/CMakeLists.txt` to use namespaced target

**File**: `models/phold/CMakeLists.txt`

**Change**: replace `TARGET_LINK_LIBRARIES(phold ROSS m)` with `target_link_libraries(phold PRIVATE ross::ross m)`. Same for the other 5 phold variants. Also replace the manual file-path install:
```cmake
install(TARGETS phold phold_comm_test phold_gvt_hook_test
                phold_gvt_hook_model_test phold_gvt_hook_timestamp_test
                phold_gvt_hook_comm_test
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

**Why**: dogfoods the new target. If models build via `add_subdirectory()` they use the alias; if they build standalone against an installed ROSS they `find_package(ross CONFIG)`.

**Compat**: keep the `add_library(ROSS ALIAS ross)` shim from step 2 if you want to land step 11 separately or if other tests still reference `ROSS`.

---

### Step 12 — Use `MPIEXEC_*` variables in test functions

**File**: `models/CMakeLists.txt` (the `ROSS_TEST_SCHEDULERS` and `ROSS_TEST_INSTRUMENTATION` functions)

**Change**: replace `mpirun -np 2 ./${target_name} ...` with `${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2 ${MPIEXEC_PREFLAGS} $<TARGET_FILE:${target_name}> ${MPIEXEC_POSTFLAGS} ...`.

**Why**: respects whatever MPI was found (mpich vs openmpi vs slurm srun); works on systems without `mpirun` in PATH.

---

### Step 13 — Convert per-arch flags to target compile options + key on compiler

**File**: top-level `CMakeLists.txt`

**Change**: stop appending to `CMAKE_C_FLAGS` globally. Use `target_compile_options(ross PRIVATE ...)` and key on `CMAKE_C_COMPILER_ID` for IBM-XL-specific flags:
```cmake
if(CMAKE_C_COMPILER_ID STREQUAL "XL" OR CMAKE_C_COMPILER_ID STREQUAL "XLClang")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
        target_compile_options(ross PRIVATE -O5 -qprefetch=aggressive -qarch=pwr9 ...)
    endif()
endif()
```
Same for BGQ, BGP. Keep generic `-D_GNU_SOURCE` as a target compile definition: `target_compile_definitions(ross PRIVATE _GNU_SOURCE)`.

**Why**: applying `-O5 -qarch=pwr9` to ppc64le with gcc/clang is broken. Also avoids polluting parent project flags when ROSS is added via `add_subdirectory`.

---

### Step 14 — Move `ROSS_OPTION_LIST` from `CMAKE_C_FLAGS` to a target definition

**File**: `core/CMakeLists.txt`

**Change**:
```cmake
# remove: SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DROSS_OPTION_LIST='\"${OPTIONS}\"'")
target_compile_definitions(ross PRIVATE ROSS_OPTION_LIST="${OPTIONS}")
```

**Why**: scoped to ROSS, not propagated to siblings or consumers. Quoting is also handled correctly by `target_compile_definitions`.

---

### Step 15 — Normalize option names with `ROSS_` prefix

**File**: top-level + `core/CMakeLists.txt`

**Change**: rename for consistency (keep old as deprecated aliases via `option()` + `if(DEFINED OLDNAME) ...`):
- `AVL_TREE` → `ROSS_USE_AVL_TREE`
- `USE_RIO` → `ROSS_USE_RIO`
- `USE_DAMARIS` → `ROSS_USE_DAMARIS`
- `USE_RAND_TIEBREAKER` → `ROSS_USE_RAND_TIEBREAKER`
- `RAND_NORMAL` → `ROSS_RAND_NORMAL`
- `COVERALLS` → `ROSS_COVERALLS`
- `USE_BGPM` → `ROSS_USE_BGPM`

**Why**: avoids name collisions in superbuilds (`USE_RIO` and `RAND_NORMAL` are very generic). Make this step optional / low priority — only do it if you're willing to do a one-time consumer-facing rename.

**Compat**: provide a one-release deprecation shim:
```cmake
if(DEFINED USE_RIO AND NOT DEFINED ROSS_USE_RIO)
    message(DEPRECATION "USE_RIO is deprecated; set ROSS_USE_RIO instead")
    set(ROSS_USE_RIO ${USE_RIO})
endif()
```

---

### Step 16 — Bump preset minimum to match top-level

**File**: `CMakePresets.json`

**Change**: lower the `cmakeMinimumRequired` to 3.21 (matching the new top-level), or leave at 3.28.1 if you want to require modern presets — but make them consistent. Also consider moving `binaryDir` out of `${sourceDir}/build` to avoid in-source-adjacent layout (e.g., `${sourceDir}/../ross-build/${presetName}`), which makes the model-glob issue moot.

---

## Sequencing recommendation

- **Phase 1 (export story)**: Steps 1, 2, 3, 4, 5, 6, 7. After phase 1, codes can migrate from pkg-config to `find_package(ross CONFIG REQUIRED)` cleanly. Keep both pkg-config AND CMake config paths working.
- **Phase 2 (correctness/portability)**: Steps 8, 9, 13, 14.
- **Phase 3 (polish/dogfood)**: Steps 10, 11, 12, 15, 16.

Each phase is independently shippable; Phase 1 alone unblocks the user's stated priority.

## Validation strategy

After each step, validate by:
1. Build ROSS with current presets: `cmake --preset ross-debug && cmake --build build/debug && cmake --install build/debug`.
2. Inspect `<prefix>/lib/cmake/ross/rossConfig.cmake` and `rossTargets.cmake` (after Step 5) — verify no absolute MPI paths in `INTERFACE_*` properties.
3. Write a tiny test consumer:
   ```cmake
   cmake_minimum_required(VERSION 3.21)
   project(test_ross_consumer C)
   find_package(ross CONFIG REQUIRED)
   add_executable(t main.c)
   target_link_libraries(t PRIVATE ross::ross)
   ```
   pointed at the install prefix via `-Dross_DIR=...`.
4. Build codes against the new install and confirm pkg-config flow still works (Phase 1 should be backward-compatible).

---

## Critical files for implementation

- `CMakeLists.txt`
- `core/CMakeLists.txt`
- `core/ROSSConfig.cmake` (delete/replace)
- `core/cmake/rossConfig.cmake.in` (new file)
- `core/ross.pc.in`
- `models/CMakeLists.txt` and `models/phold/CMakeLists.txt` (Phase 3 dogfooding)
