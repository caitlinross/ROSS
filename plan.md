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
   - `${ROSS_INCLUDE_DIRS}` variable (used in `src/` and `tests/`) — important caveat: CODES gets this from `pkg_check_modules` (auto-set from pkg-config output), NOT from `rossConfig.cmake`. So as long as CODES keeps using pkg-config, `${ROSS_INCLUDE_DIRS}` keeps working transparently regardless of what the CMake config does. If/when CODES migrates to `find_package(ross)`, it should switch from `${ROSS_INCLUDE_DIRS}` to the imported target `ross::ross` (which carries the include dirs via `INTERFACE_INCLUDE_DIRECTORIES`). The new canonical `rossConfig.cmake` deliberately does NOT set this variable — see Step 5 "Why no `ROSS_LIBRARIES` / `ROSS_INCLUDE_DIRS` / `ROSS_FOUND`."
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

The plan is ordered: pre-flight CI restoration first (its own PR), then export-system changes (steps 1–6), then quality improvements (7–12), then cleanups (13+). Each step is independently mergeable and testable against `codes`.

---

### Pre-flight Step CI — Minimal CI restoration (lands as PR 1, before all CMake work)

**Files**: new `.github/workflows/build.yml`; delete `.travis.yml`

**Context**: ROSS's existing CI is dead. The `.travis.yml` config has been non-functional for years due to a cocktail of ecosystem changes:
- Travis ended free OSS builds in late 2020 (credit-limit changes).
- Ubuntu Trusty (`dist: trusty`) was dropped from Travis in 2022.
- The codecov bash uploader (`bash <(curl -s https://codecov.io/bash)`) was deprecated.
- MPICH renamed from `mpich2` → `mpich` ~2013, so `travis-install-mpi.sh mpich2` was broken even before the rest of the stack rotted.
- The `GH_TOKEN` driving the doc auto-publish to `ross-org.github.io` is almost certainly stale.

Multi-step CMake work benefits from automated regression detection. Restoring CI before the refactor lets every subsequent PR run against a known-good baseline. Scope here is deliberately minimal — comprehensive CI (multi-platform, multiple MPI implementations, coverage, doc publishing) is in [future-refactor-tasks.md](future-refactor-tasks.md).

**Changes**:
1. **Delete `.travis.yml`** — actively misleading (looks like CI exists when it hasn't worked in years).
2. **Add `.github/workflows/build.yml`** with a single Ubuntu + MPICH job that configures, builds, and runs `ctest`:

```yaml
name: build
on:
  push:
    branches: [master, cmake-improvements]
  pull_request:
    branches: [master]
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Install MPICH
        run: |
          sudo apt-get update
          sudo apt-get install -y mpich libmpich-dev
      - name: Configure
        run: cmake -S . -B build -DROSS_BUILD_MODELS=ON -DCMAKE_BUILD_TYPE=Debug
      - name: Build
        run: cmake --build build -j
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

3. **Verify green on `master`** before starting any CMake work. If master doesn't pass, fix master first (probably small Trusty-era assumptions surfaced by Ubuntu 24.04).
4. **After Phase 1 lands**, update the workflow's `Configure` step to use `cmake --preset ross-debug` instead of manual flags. One-line change.

**Why minimal scope here**: this PR is a regression-detection enabler, not the comprehensive CI rebuild. Adding macOS, OpenMPI, compiler matrix, and coverage now would balloon the PR and delay the actual CMake work. Each of those is a small follow-up PR after the CMake refactor lands.

**Compat**: codes / downstream consumers are not affected — this PR only touches CI infrastructure inside the ROSS repo.

---

### Step 0 — Pre-flight: disable Damaris/RISA in CMake

**Files**: `core/CMakeLists.txt`, `models/phold/CMakeLists.txt`, `CLAUDE.md`

**Context**: Damaris does not currently build (verified during phase-I attempt) and has no known consumers. Rather than carry the dead build paths through the modernization, disable it up front so subsequent steps don't have to reason about it. RISA / risa headers / `add_subdirectory(risa)` go away with it. Final removal of the C source paths and the submodule is deferred — only the CMake entry points are removed here.

**Changes**:
1. In `core/CMakeLists.txt`, delete the entire `OPTION(USE_DAMARIS ...)` block and the dependent `IF(USE_DAMARIS) ... ADD_SUBDIRECTORY(risa) ... ENDIF()` (currently [core/CMakeLists.txt:119-125](core/CMakeLists.txt#L119-L125)).
2. In `models/phold/CMakeLists.txt`, delete the `IF(USE_DAMARIS) ... ELSE` branches at [models/phold/CMakeLists.txt:2-4](models/phold/CMakeLists.txt#L2-L4) and [:27-37](models/phold/CMakeLists.txt#L27-L37) — collapse to the plain `target_link_libraries(... ROSS m)` form. (Step 11 then has no Damaris branch to preserve.)
3. Leave `core/risa/` and the `#cmakedefine USE_DAMARIS` in `config.h.in` in place — without the CMake option, both are inert. A future PR can rip them out once nobody objects.
4. Update [CLAUDE.md](CLAUDE.md) "Optional subsystems" — remove the Damaris/RISA bullet, note that Damaris is removed pending future cleanup of the C source paths.

**Why**: the rest of the plan (especially Steps 6 and 11) had carve-outs for Damaris/RISA. Doing this first makes those carve-outs disappear and shrinks the plan's surface area. The `USE_DAMARIS=ON` case in Step 6 (risa header install) is no longer a question.

---

### Step 0b — Pre-flight: remove the Coveralls coverage tracking path

**Files**: top-level `CMakeLists.txt`, `core/CMakeLists.txt`, delete `core/cmake/Coveralls.cmake`, `core/cmake/CoverallsClear.cmake`, `core/cmake/CoverallsGenerateGcov.cmake`

**Context**: same pattern as Step 0 — delete a dead subsystem so subsequent steps don't have to reason about it. Three independent reasons to do this now:

1. **The CI that drove it is gone.** The COVERALLS option exists to produce `coveralls.json` output for upload to coveralls.io. The Travis CI flow that called `make coveralls` is non-functional (see Pre-flight Step CI). The new minimal GHA workflow does not collect coverage. So `COVERALLS=ON` is unreachable from any automation today.
2. **Step 2 silently breaks it.** [core/cmake/Coveralls.cmake:34](core/cmake/Coveralls.cmake#L34) does `set(_CMAKE_SCRIPT_PATH ${PROJECT_SOURCE_DIR}/cmake)`. Today this accidentally works because the nested `PROJECT(ROSS C)` in `core/` makes `${PROJECT_SOURCE_DIR}` = `core/`, so `${PROJECT_SOURCE_DIR}/cmake` = `core/cmake/` where the helper files live. After Step 2 removes the nested `project()`, `${PROJECT_SOURCE_DIR}` reverts to the top-level source dir and the path resolves to `<repo-root>/cmake/`, which doesn't exist. Coveralls then errors with `FATAL_ERROR "Coveralls: Missing ..."`.
3. **Global flag mutation conflicts with Step 13.** The `coveralls_turn_on_coverage()` macro at [core/cmake/Coveralls.cmake:122-123](core/cmake/Coveralls.cmake#L122-L123) appends to `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` globally (the CXX one is dead code — CXX isn't enabled). Step 13 is moving everything to target-scoped options; leaving Coveralls as a global-flag-mutating outlier is inconsistent.

The minimum fix to keep coverage working would be: rebase `_CMAKE_SCRIPT_PATH` on `${CMAKE_CURRENT_LIST_DIR}` (Coveralls.cmake's own location), convert the macro to apply `target_compile_options` + `target_link_options` to `ross`, and re-add coverage to the new GHA workflow with `codecov-action` instead of the deprecated bash uploader. That's real work for a feature nobody is using.

**Changes**:
1. Delete `option(COVERALLS ...)` and the `if(COVERALLS) include(Coveralls); coveralls_turn_on_coverage(); endif()` block at top-level [CMakeLists.txt:43-48](CMakeLists.txt#L43-L48).
2. Delete the `if(COVERALLS) ... coveralls_setup(...) endif()` block at [core/CMakeLists.txt:157-167](core/CMakeLists.txt#L157-L167).
3. Delete `core/cmake/Coveralls.cmake`, `core/cmake/CoverallsClear.cmake`, `core/cmake/CoverallsGenerateGcov.cmake`.
4. No CLAUDE.md update needed — Coveralls isn't mentioned there.

**Why**: dead subsystem, latent break, and inconsistency with Step 13. Re-enabling coverage later is straightforward — see "CI re-establishment (comprehensive)" in [future-refactor-tasks.md](future-refactor-tasks.md). The modern path is `target_compile_options(ross PRIVATE --coverage)` plus `target_link_options(ross PRIVATE --coverage)` plus a `codecov/codecov-action@v4` step in GHA, which is shorter than the existing BLT macro.

**Compat**: `cmake -DCOVERALLS=ON ...` invocations now produce an "unknown option" warning (CMake does not error on unknown cache variables, just ignores them). Acceptable since no working CI ever passes this flag.

**Audit note (docs/CMakeLists.txt)**: also audited as part of review item #6. Uses `${CMAKE_BINARY_DIR}` and `${CMAKE_CURRENT_SOURCE_DIR}` (always-valid globals), not `${ROSS_SOURCE_DIR}` / `${ROSS_BINARY_DIR}`. Unaffected by Step 2. No changes needed.

---

### Step 0c — Pre-flight: delete the `ross-config` shell-script wrapper

**Files**: delete `core/ross-config.in`; modify `core/CMakeLists.txt`

**Context**: `ross-config` is a hand-rolled shell script (`@ROSS_CC@`, `@CMAKE_INSTALL_PREFIX@` substituted at configure time) that prints flags for consumers — `--cflags`, `--ldflags`, `--libs`, `--cc`, `--cxx`, `--ld`. It's the same pattern as historic `mpicc --show`, `gtk-config`, `wx-config`: a custom script that pre-dates standard discovery mechanisms.

It is fully redundant with the two discovery surfaces this plan establishes:
- **pkg-config users** get the same flags via `pkg-config --cflags ross` / `--libs ross` (Step 7 modernizes `ross.pc`).
- **CMake users** get them via `find_package(ross)` + `target_link_libraries(... ross::ross)` (Step 5).

**Decision: delete it.** Grep across the active consumer repos was conclusive: **zero references in CODES, zero in NetMaestro**, zero anywhere in ROSS itself except the script's own definition and CMake plumbing. Nobody uses it.

**Changes**:
1. Delete `core/ross-config.in`.
2. Delete the lines that set up and install it in `core/CMakeLists.txt` (currently around lines 177-187):
   - `SET(ROSS_CC $ENV{CC})` and `SET(ROSS_CXX $ENV{CXX})` (purpose was to feed the script's template substitution)
   - `STRING(REPLACE " " "\\ " CMAKE_INSTALL_PREFIX_ESCAPED "\"${CMAKE_INSTALL_PREFIX}\"")` if it's only used by `ross-config.in` — note `ross.pc.in` also uses `@CMAKE_INSTALL_PREFIX_ESCAPED@` today; Step 7 rewrites `ross.pc.in` to use `@CMAKE_INSTALL_PREFIX@` directly, so this line goes away then anyway. Don't delete it in Step 0c if Step 7 isn't landing in the same PR; deletion order doesn't matter as long as both happen.
   - `CONFIGURE_FILE(ross-config.in ross-config @ONLY)`
   - `SET_SOURCE_FILES_PROPERTIES(...ross-config.in... GENERATED FALSE)` and `...ross-config GENERATED TRUE)`
   - `INSTALL(FILES ${ROSS_BINARY_DIR}/ross-config DESTINATION bin PERMISSIONS ...)` — the bin install rule
3. No CLAUDE.md update needed — `ross-config` isn't mentioned there.

**Why**: dead infrastructure with two modern replacements already in scope. Carrying a third discovery surface (`ross-config`) through the modernization means three things to keep in sync. Deleting it now removes a per-PR maintenance cost from every subsequent step.

**Compat**: any consumer script that previously called `ross-config --cflags` etc. would break — but the grep across CODES / NetMaestro / ROSS itself confirmed zero such callers. If a surprise out-of-tree consumer turns up post-merge, the migration is `pkg-config --cflags ross` (literally one word swap).

---

### Step 1 — Bump `cmake_minimum_required` and fix top-level `project()`

**File**: `CMakeLists.txt` (top level)

**Change**:
```cmake
cmake_minimum_required(VERSION 3.21)   # 3.21+ gives us PROJECT_IS_TOP_LEVEL

# Must run git-describe BEFORE project() so we can pass VERSION in.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/core/cmake/")
include(GetGitRevisionDescription)
git_describe_working_tree(ROSS_GIT_DESCRIBE --tags --dirty)

string(REGEX REPLACE "^v([0-9]+)\\..*"              "\\1" ROSS_VERSION_MAJOR "${ROSS_GIT_DESCRIBE}")
string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*"        "\\1" ROSS_VERSION_MINOR "${ROSS_GIT_DESCRIBE}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" ROSS_VERSION_PATCH "${ROSS_GIT_DESCRIBE}")
set(ROSS_DETECTED_VERSION "${ROSS_VERSION_MAJOR}.${ROSS_VERSION_MINOR}.${ROSS_VERSION_PATCH}")

# Fallback for tarball / shallow-clone / no-tag builds. project(VERSION ...) errors
# on a malformed version string, so we can't pass empty/garbage through.
if(NOT ROSS_DETECTED_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(WARNING "Could not derive ROSS version from git (got '${ROSS_GIT_DESCRIBE}'); "
                    "falling back to 0.0.0. Set -DROSS_DETECTED_VERSION=X.Y.Z to override.")
    set(ROSS_DETECTED_VERSION "0.0.0")
endif()

project(ross
    VERSION ${ROSS_DETECTED_VERSION}
    DESCRIPTION "Rensselaer's Optimistic Simulation System"
    HOMEPAGE_URL "https://github.com/ROSS-org/ROSS"
    LANGUAGES C)
```

Keep the original `VERSION_SHA1` extraction (for embedding into `config.h`) but do it after `project()` — only the numeric parts are fed into `project()`.

Lower-case `ross` as the project name is the modern convention (the package name `find_package(ross)` is then case-insensitive by spec but consistent). It also lets `${ross_VERSION}`, `${ross_VERSION_MAJOR}`, etc. flow naturally from `project()`.

**Why**: enables `PROJECT_IS_TOP_LEVEL`, modern `install(TARGETS ... FILE_SET HEADERS)` (3.23+) if we want it, `target_link_libraries(... PRIVATE MPI::MPI_C)` clean semantics, and lets the package-version file be auto-generated.

**Regression risk to watch**: `git_describe_working_tree` returns empty on tarball builds / shallow clones / tag-less trees. The current code tolerates empty version strings (they just leak into `ross.pc` as blanks); `project(VERSION ...)` does NOT — it errors. The fallback above is load-bearing, not cosmetic.

**Compat**: `find_package(ROSS)` (uppercase) is **not** automatically equivalent to `find_package(ross)` — that earlier claim was wrong. CMake searches for `<Name>Config.cmake` using the exact case the caller provides (it does also try a `<lowercase-name>-config.cmake` fallback, but we ship the camelcase `rossConfig.cmake`, not the dashed variant). So `find_package(ROSS)` would not find the new canonical install at `<prefix>/lib/cmake/ross/rossConfig.cmake`.

External users could plausibly be calling `find_package(ROSS)` against the current master install (which ships `<prefix>/lib/ROSSConfig.cmake` and requires a `-DROSS_DIR=<prefix>/lib` hint). To keep them working through one deprecation cycle, Step 5 ships a back-compat shim at the **old** install location (`<prefix>/lib/ROSSConfig.cmake`) that emits a DEPRECATION message and delegates to the new config. See Step 5 item 4 for the full shim contents.

---

### Step 2 — Drop the nested `project()` in `core/CMakeLists.txt` and convert to target-based hygiene

**File**: `core/CMakeLists.txt`

**Changes**:
1. Delete `PROJECT(ROSS C)` line. The `core/` directory is part of the parent `ross` project.
2. **Sweep `${ROSS_SOURCE_DIR}` / `${ROSS_BINARY_DIR}` references.** These variables were set by the nested `PROJECT(ROSS C)` and become empty as soon as it's removed. Fix every remaining use:
   - `core/CMakeLists.txt`: `${ROSS_SOURCE_DIR}` → `${CMAKE_CURRENT_SOURCE_DIR}`, `${ROSS_BINARY_DIR}` → `${CMAKE_CURRENT_BINARY_DIR}`. Affects [core/CMakeLists.txt:2,187-193](core/CMakeLists.txt#L2-L193) (the directory-scope include — about to be replaced by Step 3 anyway — and four `INSTALL(...)` lines).
   - `models/phold/CMakeLists.txt`: delete `INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ...)` and `INCLUDE_DIRECTORIES(${ROSS_BINARY_DIR})` outright at [models/phold/CMakeLists.txt:1-9](models/phold/CMakeLists.txt#L1-L9) — Step 3's `BUILD_INTERFACE` propagation via `ross::ross` covers them. The raw-path install at [models/phold/CMakeLists.txt:58](models/phold/CMakeLists.txt#L58) is replaced in Step 11.
   - **This forces Step 11 into the same PR as Step 2** (or sooner). Without Step 11's `install(TARGETS ${phold_targets} ...)` rewrite, the broken `${ROSS_BINARY_DIR}/../models/phold/phold` install path remains. See the updated Phase 1 sequencing.
3. Rename the target and commit to lowercase end-to-end. CMake's default `OUTPUT_NAME` equals the target name, so `add_library(ross ...)` produces `libross.{a,so}` automatically. No `OUTPUT_NAME` property needed:

```cmake
add_library(ross ${ross_srcs})
add_library(ross::ross ALIAS ross)              # namespaced consumer target
add_library(ROSS      ALIAS ross)               # back-compat alias for in-tree bare-target callers
set_target_properties(ross PROPERTIES
    EXPORT_NAME ross)                            # exports as ross::ross via namespace
```

   **Library filename rename — `libROSS.{a,so}` → `libross.{a,so}`.** This is a deliberate consistency change: lowercase package name (`find_package(ross)`), lowercase pkg-config (`ross.pc`), lowercase namespaced target (`ross::ross`), lowercase library file. The on-disk filename is the only remaining uppercase artifact today; collapsing it removes the asymmetry. Grep across CODES, NetMaestro, and ROSS itself before deciding showed:
   - Zero source-tree references to `-lROSS` or `libROSS` in CODES (the hits in `test/build.ninja` and `CMakeCache.txt` are auto-regenerated build artifacts that pick up the new name on reconfigure).
   - Zero references in NetMaestro.
   - The two committed ROSS-side references (`core/ross-config.in:45`, `core/ross.pc.in:5`) are already handled — `ross-config.in` is deleted in Step 0c, and `ross.pc.in` is rewritten in Step 7 with the new lowercase flag.

   No consumer-side source changes required. Anyone using pkg-config or `find_package(ross)` picks up `-lross` / `libross.a` transparently on reconfigure.

4. Convert `TARGET_LINK_LIBRARIES(ROSS ${ROSS_EXTERNAL_LIBS})` to keyword form (see steps 3 and 5).

**Why**: gives us a namespaced target AND finishes the lowercase consistency story. End-to-end one mental model: `ross` is the package name, library file, find_package name, pkg-config name, and namespaced target.

**Compat**: the in-tree `models/phold/CMakeLists.txt` still uses bare `ROSS` — `add_library(ROSS ALIAS ross)` keeps it working until Step 11 migrates it to `ross::ross`. The alias is on the *target* name, independent of the library filename — it stays valid regardless of the `libROSS.a` → `libross.a` rename.

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

**Files**: `CMakeLists.txt` (top level), `core/CMakeLists.txt`, delete `core/cmake/SetupMPI.cmake`, [CLAUDE.md](CLAUDE.md)

**Configure-time UX (the point of this step)**: after this step, users configure ROSS with their system C compiler — `cmake --preset ross-debug` or `cmake -S . -B build -DROSS_BUILD_MODELS=ON -DCMAKE_BUILD_TYPE=Debug`. **No `CC=mpicc` and no `-DCMAKE_C_COMPILER=mpicc` required.** CMake's `find_package(MPI REQUIRED COMPONENTS C)` does the discovery (typically by locating `mpicc` on PATH and inspecting it) and exposes everything via `MPI::MPI_C`. If MPI is installed in a non-standard location, users hint with `-DMPI_HOME=...` or `module load <mpi>` — never by overriding the C compiler.

**Changes**:
1. **Delete** [core/cmake/SetupMPI.cmake](core/cmake/SetupMPI.cmake). It's BLT-derived legacy that wraps `find_package(MPI)` and dumps legacy `MPI_C_*` variables — nothing in modern CMake needs it. Replace the `INCLUDE(SetupMPI)` call at [CMakeLists.txt:149](CMakeLists.txt#L149) with `find_package(MPI REQUIRED COMPONENTS C)` directly. The `REQUIRED` keyword turns the current "WARNING: Could not find MPI!" soft-fail (lines 153-157) into a hard configure-time error, which is correct — ROSS does not build without MPI.
2. In top-level `CMakeLists.txt`, delete `INCLUDE_DIRECTORIES(${MPI_C_INCLUDE_PATH})` and `LIST(APPEND ROSS_EXTERNAL_LIBS ${MPI_C_LIBRARIES})` (lines 150-157, the whole `IF(MPI_C_FOUND) ... ELSE ... ENDIF` block).
3. In `core/CMakeLists.txt`:
```cmake
target_link_libraries(ross
    PUBLIC  MPI::MPI_C    # PUBLIC: ross.h includes <mpi.h>, so consumers need it
    PRIVATE m)
```
   MPI must be `PUBLIC`, not `PRIVATE`: [core/ross.h:81](core/ross.h#L81) does `#include <mpi.h>`, and `ross.h` is the primary public header. `ross-gvt.h` and `network-mpi.h` also include it.
4. Delete `TARGET_INCLUDE_DIRECTORIES(ROSS INTERFACE ${MPI_C_INCLUDE_PATH})`.
5. **Update [CLAUDE.md](CLAUDE.md)**: drop `-DCMAKE_C_COMPILER=mpicc` from the manual configure example in the "Build" section. Add a one-liner that says "MPI is auto-discovered; for non-standard install locations use `-DMPI_HOME=...` or `module load`." This documentation change is load-bearing because the existing docs are how users learn the workflow.

**Why**: `MPI::MPI_C` is a properly-named imported target. When we export the package config and add `find_dependency(MPI COMPONENTS C)`, the consumer's CMake re-resolves MPI on its own machine. **This is the single most important change for relocatability**, and the side effect — that users no longer need to know they should set `CC=mpicc` — is also the right end-state. The wrapper-compiler workflow was a workaround for the old MPI-by-legacy-variable style; modern CMake replaces it entirely.

**Compat**: `codes` currently runs its own `find_package(MPI)` — fine, no break. After codes migrates to `find_package(ross)`, codes can drop its own MPI block since it'll get `MPI::MPI_C` transitively.

---

### Step 4b — Honor `BUILD_SHARED_LIBS` directly; retire `ROSS_BUILD_SHARED_LIBS`

**Files**: `core/CMakeLists.txt`, [CLAUDE.md](CLAUDE.md)

**Problem**: [core/CMakeLists.txt:150-151](core/CMakeLists.txt#L150-L151) defines a custom `ROSS_BUILD_SHARED_LIBS` option that maps to the standard `BUILD_SHARED_LIBS`:
```cmake
OPTION(ROSS_BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
SET(BUILD_SHARED_LIBS ${ROSS_BUILD_SHARED_LIBS})
```
This is wrong on three counts:
- Two knobs for one decision — `BUILD_SHARED_LIBS` is the standard, discoverable CMake variable; nobody guesses `ROSS_BUILD_SHARED_LIBS`.
- `set(BUILD_SHARED_LIBS ...)` (without `CACHE`) shadows the parent project when ROSS is `add_subdirectory`'d, silently producing a static `libross.a` inside a shared-by-default superbuild.
- It's not how modern CMake projects handle this.

**Changes**:
1. Delete the `OPTION(ROSS_BUILD_SHARED_LIBS ...)` and the `SET(BUILD_SHARED_LIBS ...)` line. The `add_library(ross ${ross_srcs})` from Step 2 then defers to the standard `BUILD_SHARED_LIBS` (default static when unset).
2. Add a one-release deprecation shim at the top of `core/CMakeLists.txt`, same pattern as Step 15:
```cmake
if(DEFINED ROSS_BUILD_SHARED_LIBS)
    message(DEPRECATION
        "ROSS_BUILD_SHARED_LIBS is deprecated; use the standard "
        "BUILD_SHARED_LIBS variable (-DBUILD_SHARED_LIBS=ON).")
    if(NOT DEFINED BUILD_SHARED_LIBS)
        set(BUILD_SHARED_LIBS ${ROSS_BUILD_SHARED_LIBS} CACHE BOOL "" FORCE)
    endif()
endif()
```
Remove the shim in a follow-up release after a soak period.
3. **Update [CLAUDE.md](CLAUDE.md)** — under "Build", add a one-liner: "Default is a static library. Pass `-DBUILD_SHARED_LIBS=ON` to build shared."

**Why**: standard CMake variable, one knob, superbuild-friendly (`add_subdirectory` consumers inherit the parent's setting), self-documenting. `CMAKE_POSITION_INDEPENDENT_CODE ON` at the top of the top-level `CMakeLists.txt` stays — required so a static ROSS can be linked into downstream shared objects (e.g., Python bindings). The `install(TARGETS ross ... LIBRARY/ARCHIVE/RUNTIME DESTINATION ...)` form from Step 5 already handles both modes via its three destination keywords; no install-side change needed here.

**Compat**: scripts or build systems that pass `-DROSS_BUILD_SHARED_LIBS=ON` get a deprecation warning and continue to work (the shim maps it to `BUILD_SHARED_LIBS`). After one release, the shim goes away and they need to switch to `-DBUILD_SHARED_LIBS=ON`.

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

# rossConfig.cmake — ROSS package configuration for find_package(ross)
#
# Usage:
#     find_package(ross REQUIRED)
#     target_link_libraries(myapp PRIVATE ross::ross)
#
# This is the modern CMake API: a namespaced imported target carries the
# include dirs, link dependencies (MPI), and any compile definitions
# transparently. No legacy <PKG>_LIBRARIES / <PKG>_INCLUDE_DIRS variables
# are set — consumers should use the ross::ross target directly. If you
# need the include path explicitly, query the target:
#     get_target_property(dirs ross::ross INTERFACE_INCLUDE_DIRECTORIES)
#
# Path B consumer contract for headers:
#     #include <ross.h>           — supported
#     #include <ross-extern.h>    — supported (top-level ross-*.h)
#     #include <instrumentation/...> — UNSUPPORTED (internal headers,
#         reachable via BUILD_INTERFACE but may be hidden in a future
#         release). See future-refactor-tasks.md "Header hygiene &
#         public/internal split".

include(CMakeFindDependencyMacro)
find_dependency(MPI REQUIRED COMPONENTS C)

include("${CMAKE_CURRENT_LIST_DIR}/rossTargets.cmake")

check_required_components(ross)
```

**Why no `ROSS_LIBRARIES` / `ROSS_INCLUDE_DIRS` / `ROSS_FOUND` in the new config**: modern CMake convention (since ~3.x became common) is for package config files to expose only imported targets. Legacy `<PKG>_LIBRARIES`-style variables are pre-target-based-CMake conventions kept by `FindXxx.cmake` modules for compatibility, but new packages shouldn't ship them. Adding them speculatively (a) creates an "is `${ROSS_LIBRARIES}` a target name or a file path?" surprise, and (b) doesn't preserve any actual back-compat — master's hand-written `ROSSConfig.cmake` only set `ROSS_INCLUDE_DIRS`, and CODES (the only known consumer) gets that variable from pkg-config, not from rossConfig.cmake. CMake auto-sets `ross_FOUND` when find_package succeeds, so we don't need to set it either.

If a true back-compat surface needs preserving, it lives in the uppercase shim — see item 4 below.

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

4. **Back-compat shim for `find_package(ROSS)`** — ship it. Rationale: master currently installs `<prefix>/lib/ROSSConfig.cmake` (uppercase, non-canonical location), and external researchers may be using `find_package(ROSS)` with a `-DROSS_DIR=<prefix>/lib` hint. We can't audit who they are, the shim is ~10 lines, and the removal is already tracked in [future-refactor-tasks.md](future-refactor-tasks.md).

   Install the shim at the **old** location `<prefix>/${CMAKE_INSTALL_LIBDIR}/ROSSConfig.cmake` (not `lib/cmake/ROSS/`) — that's the path legacy `-DROSS_DIR` hints point at. Contents:

```cmake
# <prefix>/lib/ROSSConfig.cmake — deprecation shim
message(DEPRECATION
    "find_package(ROSS) (uppercase) and the <prefix>/lib install location are "
    "deprecated. Use find_package(ross) and link against ross::ross. "
    "This shim will be removed in a future ROSS release.")

# Delegate discovery to the new canonical config
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ross/rossConfig.cmake)

# Preserve the one variable master's hand-written ROSSConfig.cmake set.
# The new canonical rossConfig.cmake intentionally does NOT set this
# (modern CMake convention is imported-targets-only), but legacy callers
# of find_package(ROSS) may use ${ROSS_INCLUDE_DIRS} directly. Derive it
# from the imported target so it always matches the actual install path.
get_target_property(_ross_inc_dirs ross::ross INTERFACE_INCLUDE_DIRECTORIES)
set(ROSS_INCLUDE_DIRS ${_ross_inc_dirs})
unset(_ross_inc_dirs)

# Legacy bare-target-name compat: pre-modernization users wrote
# target_link_libraries(myapp ROSS). The new install exports the target as
# ross::ross; alias it back so old call sites keep linking.
if(NOT TARGET ROSS)
    add_library(ROSS ALIAS ross::ross)
endif()
```

   This faithfully reproduces master's `<prefix>/lib/ROSSConfig.cmake` behavior (which set `ROSS_INCLUDE_DIRS` and nothing else) while delegating the actual implementation to the new modern config. Anyone using `find_package(ROSS)` + `${ROSS_INCLUDE_DIRS}` + bare `ROSS` target name keeps working through the deprecation cycle.

   Install with a small `install(FILES ...)` rule alongside the main config install. Removal of this shim is tracked under "Deprecation shim removals" in [future-refactor-tasks.md](future-refactor-tasks.md).

**Why**: this is the heart of the migration. After this, downstream code can do:
```cmake
find_package(ross REQUIRED)
target_link_libraries(myapp PRIVATE ross::ross)
```
and get includes, MPI dependency, and all link libs propagated correctly.

**Compat**:
- Codes still works during transition because it uses pkg-config, not `find_package(ROSS)`. We keep `ross.pc` working (step 6).
- CODES's `${ROSS_INCLUDE_DIRS}` references in `src/` and `tests/` come from `pkg_check_modules` (which auto-sets the variable from pkg-config output), not from `rossConfig.cmake`. As long as CODES uses pkg-config, those references keep working transparently. When CODES eventually migrates to `find_package(ross)`, it should switch from `${ROSS_INCLUDE_DIRS}` to the imported target `ross::ross` — the include dirs ride along via `INTERFACE_INCLUDE_DIRECTORIES`. The new canonical `rossConfig.cmake` does not set the legacy variable.
- The shim at the legacy `<prefix>/lib/ROSSConfig.cmake` location handles `find_package(ROSS)` with `-DROSS_DIR=<prefix>/lib`-style hints. New users use `find_package(ross)` against the canonical `<prefix>/lib/cmake/ross/` location.

---

### Step 6 — Move installed headers under `include/ross/`

**File**: `core/CMakeLists.txt`

**Changes**:
1. Replace the catch-all header install:
```cmake
# OLD:
INSTALL(DIRECTORY ${ROSS_SOURCE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")
```
   With a scoped install under `include/ross/`:
```cmake
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "cmake" EXCLUDE
        PATTERN "risa"  EXCLUDE)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross)
```

   The `risa` exclude is defensive — Step 0 already disabled the `add_subdirectory(risa)` path so the headers are unused, but keep the exclude so a future re-enable doesn't accidentally start shipping risa headers without a deliberate decision.

   Keep the filename `config.h`. Once the `/ross/` subdir isolates the include namespace, the generic name is no longer a collision risk — the file now lives at `<prefix>/include/ross/config.h`, not `<prefix>/include/config.h`.

   Consumers do `#include <ross.h>`, which resolves via the exported target's `${CMAKE_INSTALL_INCLUDEDIR}/ross` interface include dir. Internal headers use `#include "config.h"` (quoted) — which resolves via the including header's own directory, so the installed `ross-base.h` at `<prefix>/include/ross/ross-base.h` finds `<prefix>/include/ross/config.h` without any source edits.

2. For the `INTERFACE` target include path (step 3), point at `${CMAKE_INSTALL_INCLUDEDIR}/ross`.

   **Alternative considered**: install to `include/` (top level) and require consumers use `#include <ross/ross.h>`. This is stricter but breaks all existing `#include <ross.h>` usage in codes and elsewhere. **Rejected** — not worth the source churn for consumers.

**Consumer contract — Path B**: external consumers may `#include <ross.h>` or any top-level `ross-*.h` header (e.g. `<ross-extern.h>`, `<ross-types.h>`). Subdirectory headers (`<instrumentation/...>`, `<check-revent/...>`, `<queue/...>`, `<rio/...>`) and internal headers (`<buddy.h>`, `<lz4.h>`, `<hash-quadratic.h>`) are **not** part of the supported surface — build-tree consumers can still reach them via the BUILD_INTERFACE include path, but this is an unsupported artifact and will be hidden when the public/private header split lands (see [future-refactor-tasks.md](future-refactor-tasks.md)).

Path B matches what CODES already does: ~40 files use `#include <ross.h>`, plus 4 redundant `#include <ross-extern.h>` references in [src/surrogate/](../codes/src/surrogate/) that already work transparently because `ross.h` pulls in `ross-extern.h` via [core/ross.h:109](core/ross.h#L109). **No CODES source changes are required for the CMake PR.**

Document the contract in three places as part of this step:
- [CLAUDE.md](CLAUDE.md) — add a "Consumer-facing API surface" subsection under "Architecture" or near the "Consumer compatibility (CODES)" section.
- A comment block at the top of the new `core/cmake/rossConfig.cmake.in` (introduced in Step 5).
- This step's prose above (already covered).

**Why**: stops dumping `lz4.h`, `buddy.h`, `config.h`, `io.h`, `analysis-lp.h`, etc. directly into `<prefix>/include/`. Eliminates the pollution risk without requiring source edits to rename `config.h`. The Path B contract also lets the build-tree/install-tree asymmetry (build-tree exposes more headers than install-tree) be characterized as "build-tree exposes unsupported internals" rather than "build-tree and install-tree disagree about the API," which is much weaker.

**Earlier draft had a `RENAME config.h → ross-config-build.h` step** — removed. That rename was incomplete ([core/ross-base.h:6](core/ross-base.h#L6) and [core/ross-random.h:4](core/ross-random.h#L4) both `#include "config.h"`; only updating `.c` files would have left the installed headers broken) and unnecessary once the `/ross/` subdir namespacing is in place.

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
Libs: -L${libdir} -lross -lm
Cflags: -I${includedir}
```
3. Install to `${CMAKE_INSTALL_LIBDIR}/pkgconfig`.

**Why**: `codes` uses pkg-config today. Updating `includedir` to point to `<prefix>/include/ross` (matching step 6) means codes' existing `${ROSS_INCLUDE_DIRS}` (set by `pkg_check_modules`) automatically points to the right place. Codes continues to work without changes.

**Compat**: existing codes builds keep working — only the include path changes from `<prefix>/include` to `<prefix>/include/ross`, and codes consumes that via `${ROSS_INCLUDE_DIRS}` so it's transparent.

**Intent — pkg-config is back-compat only.** `Cflags`/`Libs` deliberately omit MPI flags. ROSS and CODES both require MPI today and consumers compile with `mpicc` (or equivalent), so MPI gets pulled in transparently — adding `MPI_C_COMPILE_FLAGS`/`MPI_C_LIBRARIES` to `ross.pc` is unnecessary churn. The CMake config (Step 5) handles MPI properly via `find_dependency(MPI)`. The long-term direction is to deprecate `ross.pc` once downstream consumers (CODES first) migrate to `find_package(ross)`; until then, keep `ross.pc` minimal and working. Don't invest in MPI-in-pkg-config or MPI-as-optional support here — both are far-future concerns.

---

### Step 8 — `include(GNUInstallDirs)` and replace hard-coded `lib/`/`bin/`/`include/`

**Files**: top-level `CMakeLists.txt`, `core/CMakeLists.txt`, `models/phold/CMakeLists.txt`

**Change**: add `include(GNUInstallDirs)` near the top of the top-level file (after `project()`). Replace every `DESTINATION lib`, `DESTINATION bin`, `DESTINATION include` with `${CMAKE_INSTALL_LIBDIR}`, `${CMAKE_INSTALL_BINDIR}`, `${CMAKE_INSTALL_INCLUDEDIR}` respectively.

**Also fix the RPATH stanza** at [CMakeLists.txt:35-37](CMakeLists.txt#L35-L37) — it hard-codes `${CMAKE_INSTALL_PREFIX}/lib` in both the `LIST(FIND ... isSystemDir)` check and the `SET(CMAKE_INSTALL_RPATH ...)` line. After `include(GNUInstallDirs)`, change both to `${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}` so multilib (lib64) installs get the correct install RPATH. This is a different construction than `DESTINATION lib` and won't be caught by the sweep above.

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

**Test gating after this step — clarification**: ROSS tests live in `models/phold/CMakeLists.txt` (via the `ROSS_TEST_SCHEDULERS` and `ROSS_TEST_INSTRUMENTATION` helpers defined in `models/CMakeLists.txt`). After Step 9, tests are gated by three independent conditions, all of which must hold for `ctest` to discover ROSS's tests:

1. `PROJECT_IS_TOP_LEVEL` — Step 9's gate. When ROSS is consumed via `add_subdirectory()`, ROSS does NOT call `include(CTest)`; the parent project decides whether to enable testing. Tests don't get registered with ctest in subdirectory mode unless the parent has its own `enable_testing()`.
2. `BUILD_TESTING=ON` — controls whether `include(CTest)` (which runs only when condition 1 holds at top level) calls `enable_testing()`. Defaults ON via the standard CTest module. Both presets explicitly set `BUILD_TESTING=ON`.
3. `ROSS_BUILD_MODELS=ON` — controls whether [models/CMakeLists.txt](models/CMakeLists.txt) is added at all, which is where the test definitions live. Defaults OFF.

Concretely:
- Both ON at top level → tests run.
- `BUILD_TESTING=OFF` at top level → models still build (if `ROSS_BUILD_MODELS=ON`), but `add_test(...)` calls inside `models/phold/CMakeLists.txt` are no-ops because `enable_testing()` was never called.
- `ROSS_BUILD_MODELS=OFF` → no test definitions exist; CTest infrastructure is set up but empty.
- Consumed via `add_subdirectory()` → ROSS skips `include(CTest)` entirely; tests only run if the parent project sets up CTest and `ROSS_BUILD_MODELS=ON`.

This triple-gating is intentional, not a bug — models are opt-in (most consumers just want the library), and testing infrastructure is opt-in (CTest is heavy and shouldn't be forced on subdirectory consumers). The implementer of Step 9 should be aware that `PROJECT_IS_TOP_LEVEL` is layered ON TOP of the existing `BUILD_TESTING` / `ROSS_BUILD_MODELS` gates, not a replacement for them.

---

### Step 10 — Replace `FILE(GLOB_RECURSE)` in `models/CMakeLists.txt`

**File**: `models/CMakeLists.txt`

**Change**: replace with explicit `add_subdirectory(phold)` (and any other models). If you really want auto-discovery, at least restrict to top-level children:
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
Explicit listing is preferred — there are only 1-2 models in tree.

**Why**: globs miss new files until reconfigure, and `GLOB_RECURSE` following symlinks is a footgun if a model ever contains a nested build dir. The build dir hazard is modest in practice (the preset's build dir is at `ross/build/`, not `ross/models/build/`), but explicit `add_subdirectory` is clearer and cheaper to maintain.

---

### Step 11 — Update `models/phold/CMakeLists.txt` to use namespaced target

**File**: `models/phold/CMakeLists.txt`

**Phase**: this step is **Phase 1** (moved from Phase 3). It must land in the same PR as Step 2 because Step 2's removal of `${ROSS_BINARY_DIR}` invalidates the raw-path install at [models/phold/CMakeLists.txt:58](models/phold/CMakeLists.txt#L58).

**Change**: replace the `BGPM` / plain link-library branches at [models/phold/CMakeLists.txt:23-38](models/phold/CMakeLists.txt#L23-L38) with the namespaced loop below. The Damaris branch is gone (Step 0 removed `USE_DAMARIS`):

```cmake
set(phold_targets
    phold phold_comm_test phold_gvt_hook_test
    phold_gvt_hook_model_test phold_gvt_hook_timestamp_test
    phold_gvt_hook_comm_test)

foreach(t ${phold_targets})
    target_link_libraries(${t} PRIVATE ross::ross m)
    if(BGPM)
        target_link_libraries(${t} PRIVATE imp_bgpm)
    endif()
endforeach()
```

(Verify whether the BGPM branch is still live — it keys on `BGPM` but the top-level option is `USE_BGPM`; the existing file may already be broken. If it's dead, delete the branch outright rather than preserving a non-working path.)

Also replace the manual file-path install:
```cmake
install(TARGETS ${phold_targets}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

**Why**: dogfoods the new target. If models build via `add_subdirectory()` they use the alias; if they build standalone against an installed ROSS they `find_package(ross)`. And — load-bearing — without this step, Step 2 leaves an `INSTALL(FILES ...)` line that references an empty variable.

**Compat**: keep the `add_library(ROSS ALIAS ross)` shim from step 2 in case anything else (e.g., out-of-tree models) still references the bare `ROSS` name. The shim is cheap; remove it in a follow-up after a soak period.

---

### Step 12 — Use `MPIEXEC_*` variables in test functions

**File**: `models/CMakeLists.txt` (the `ROSS_TEST_SCHEDULERS` and `ROSS_TEST_INSTRUMENTATION` functions)

**Change**: convert from the positional `ADD_TEST(name cmd args...)` form to the keyword `add_test(NAME ... COMMAND ...)` form. The keyword form is what lets us use generator expressions like `$<TARGET_FILE:...>`:

```cmake
add_test(NAME ${target_name}_SCHED_Optimistic
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2 ${MPIEXEC_PREFLAGS}
                 $<TARGET_FILE:${target_name}> ${MPIEXEC_POSTFLAGS}
                 --synch=3 --extramem=100000)
```

**Why**: respects whatever MPI was found (mpich vs openmpi vs slurm srun); works on systems without `mpirun` in PATH. Generator expressions also make the test location-independent (no more implicit CWD dependency from `./${target_name}`).

**Note**: the positional `ADD_TEST` form does *not* expand generator expressions, so changing the signature is a prerequisite, not an optional cleanup.

---

### Step 13 — Convert per-arch flags to target compile options; gate IBM-XL flags on both arch AND compiler

**File**: top-level `CMakeLists.txt`

**The problem this step fixes**: today, the per-arch branches at [CMakeLists.txt:71-134](CMakeLists.txt#L71-L134) tangle two unrelated concerns (CLOCK selection — which file from `core/clock/` ships — and compiler flag injection) into one `if(CMAKE_SYSTEM_PROCESSOR ...)` chain, and the IBM-XL-specific flags (`-O5 -qprefetch=aggressive -qarch=pwr9`, etc.) are applied based on `CMAKE_SYSTEM_PROCESSOR` alone. That means **a ppc64le machine running gcc — the typical case today — has `-qarch=pwr9` and friends injected into gcc's command line, which gcc doesn't understand and errors on.** Same flaw on BGQ/BGP/BGL, but those are EOL hardware (no surviving builds anyone can validate against).

Conservative approach — we keep all current arch branches with corrected gating rather than ripping out EOL paths we can't test deleting. Tracked as a future-refactor candidate for deletion once dead-path status is confirmed (see [future-refactor-tasks.md](future-refactor-tasks.md)).

**Changes**:

1. **Split the concerns.** CLOCK selection stays as `if(CMAKE_SYSTEM_PROCESSOR STREQUAL ...) set(CLOCK ...) endif()` chains — purely architecture-driven, independent of compiler. No structural change to this part.

2. **Generic compile options** (`-g -Wall`, `-D_GNU_SOURCE`) move from `CMAKE_C_FLAGS`/`ADD_DEFINITIONS` into target-scoped properties applied unconditionally:
```cmake
target_compile_options(ross PRIVATE -g -Wall)
target_compile_definitions(ross PRIVATE _GNU_SOURCE)
```

3. **IBM-XL optimization flags** go in a separate block gated on **both** `CMAKE_C_COMPILER_ID` matching XL **and** the corresponding `CMAKE_SYSTEM_PROCESSOR`:
```cmake
if(CMAKE_C_COMPILER_ID MATCHES "^XL")
    message(STATUS
        "IBM XL compiler detected — applying legacy XL optimization flags. "
        "These paths have not been validated against current hardware; "
        "please report build success/failure.")

    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
        target_compile_options(ross PRIVATE
            -O5 -qprefetch=aggressive -qarch=pwr9 -qtune=auto
            -qmaxmem=-1 -qsimd=noauto -qhot)
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "bgq")
        target_compile_options(ross PRIVATE
            -O5 -qstrict -qprefetch=aggressive -qarch=qp -qtune=qp
            -qmaxmem=-1 -qsimd=noauto -qreport -qhot)
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "bgp")
        target_compile_options(ross PRIVATE -qflag=i:i -qattr=full -O5)
        # Legacy -qtune=450 -qarch=450d preserved in OPTIONS for historical reasons
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "bgl")
        target_compile_options(ross PRIVATE -qflag=i:i -qattr=full -O5)
    endif()
endif()
```

   `MATCHES "^XL"` covers both `"XL"` and `"XLClang"`. The `message(STATUS ...)` is load-bearing: if anyone ever does build on real XL hardware again, the status line tells us they hit a long-untested path so the result can be reported.

4. **Gate the BGQ-specific `USE_BGPM` option + `imp_bgpm` IMPORTED target** at [CMakeLists.txt:94-97](CMakeLists.txt#L94-L97) on the same `(bgq AND XL)` predicate. The IMPORTED target hardcodes `/bgsys/drivers/ppcfloor/bgpm/lib/libbgpm.a` which only exists on BGQ login nodes; applying the block outside BGQ is broken today. Conservative call is to gate it; deletion is tracked in future-refactor-tasks.md.

5. **Delete the dead `CXX_FLAGS` assignment bug** at [CMakeLists.txt:117](CMakeLists.txt#L117) and [:124](CMakeLists.txt#L124):
```cmake
# BOTH OF THESE ARE NONSENSE — CXX isn't an enabled language:
SET(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")   # in x86_64 branch
SET(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")   # in aarch64 branch
```
   Delete outright — copy-paste mangling that has been a silent no-op. The x86_64/aarch64 branches need only the generic flags from step 2 above.

**Why this gating fix is strictly safer than the status quo**:
- On non-PPC systems (x86_64, aarch64, armv7l): no behavior change. Generic flags only.
- On ppc64le with gcc: today this is *broken* (`-qarch=pwr9` injected into gcc). After this step it builds correctly with generic flags only.
- On ppc64le with IBM XL: same flags as today, no regression. Status message appears.
- On BGQ/BGP/BGL with anything: dormant code. No one has the hardware to find out either way.

**Why we don't delete BGQ/BGP/BGL outright**: they're EOL hardware (Mira retired 2019, Sequoia 2020, BGP/BGL even earlier), but the deletion isn't testable. Gated-and-dormant code costs nothing and leaves a path for archeology. If someone confirms zero surviving builds, deletion is a one-PR follow-up.

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

- **PR 1 — Pre-flight CI restoration**: Step CI. Add a minimal GitHub Actions workflow + delete the dead `.travis.yml`. Must be green on `master` before anything else lands. Gives every subsequent PR an automated regression check.
- **Phase 0 (PR 2 — dead-subsystem deletions)**: Step 0 (disable Damaris/RISA) + Step 0b (remove Coveralls path) + Step 0c (delete `ross-config` shell wrapper). Combined in one PR — all three are independent deletions of dead or redundant infrastructure, all shrink the plan's downstream surface area. Land before Phase 1. Review each as its own commit.
- **Phase 1 (export story)**: Steps 1, 2, 3, 4, **4b**, 5, 6, 7, **11**. After phase 1, codes can migrate from pkg-config to `find_package(ross REQUIRED)` cleanly. Keep both pkg-config AND CMake config paths working.
- **Phase 2 (correctness/portability)**: Steps 8, 9, 13, 14.
- **Phase 3 (polish/dogfood)**: Steps 10, 12, 15, 16.
- **Post-Phase 3 — Comprehensive CI rebuild**: multi-platform (macOS, multiple Ubuntu versions), multiple MPI implementations (MPICH, OpenMPI), compiler matrix (gcc, clang), coverage (modernized via target-scoped flags + `codecov-action`), CODES integration test. Tracked in [future-refactor-tasks.md](future-refactor-tasks.md) "CI re-establishment (comprehensive)".

Each phase is independently shippable; Phase 1 unblocks the user's stated priority.

**PR granularity within Phase 1**:
- Steps **2 + 11** must land together — Step 2 deletes `${ROSS_BINARY_DIR}` (via the nested-project removal), which Step 11 replaces with `install(TARGETS ...)`. Splitting them leaves a broken install line referencing an empty variable.
- Steps **3 / 5 / 6 / 7** must also land together (separate commits are fine for review). Step 3's `INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ross` commits to the Step 6 install layout, and Step 7's pkg-config `includedir` must match both. Splitting them across PRs produces intermediate states where the exported target points at an include dir that doesn't exist yet.
- Steps 1, 4, and 4b can each go in their own PR (Step 4b is fully independent — only touches the `add_library` site and CLAUDE.md).
- Pragmatic option: bundle the entire Phase 1 (Steps 1, 2, 3, 4, 4b, 5, 6, 7, 11) into a single PR with one commit per step. The interdependencies are tight enough that staged review across multiple PRs costs more than it gains.

## Validation strategy

After each step, validate by:
1. Build ROSS with current presets: `cmake --preset ross-debug && cmake --build build/debug && cmake --install build/debug`.
2. Inspect `<prefix>/lib/cmake/ross/rossConfig.cmake` and `rossTargets.cmake` (after Step 5) — verify no absolute MPI paths in `INTERFACE_*` properties. Concrete post-condition:
   ```
   grep -rE "/opt/homebrew|/usr/local/Cellar|/usr/lib/x86_64-linux-gnu" <prefix>/lib/cmake/ross/
   ```
   should return zero hits. If anything matches, the MPI absolute-path leak (Step 4's motivating bug) hasn't been fully fixed.
3. Write a tiny test consumer **outside the ROSS source tree** (e.g., in `/tmp/ross-consumer/`, not a sibling directory):
   ```cmake
   cmake_minimum_required(VERSION 3.21)
   project(test_ross_consumer C)
   find_package(ross REQUIRED)
   add_executable(t main.c)
   target_link_libraries(t PRIVATE ross::ross)
   ```
   Configure with `-Dross_DIR=<prefix>/lib/cmake/ross`. Building from a non-sibling dir catches hidden `${CMAKE_CURRENT_SOURCE_DIR}`/`${PROJECT_SOURCE_DIR}` assumptions that pass when the consumer is adjacent to the source.
4. Build codes against the new install and confirm pkg-config flow still works (Phase 1 should be backward-compatible).
5. Spot-check version regression: after Step 1, configure in a copy of the source with `.git/` deleted — build should succeed with `0.0.0` fallback, not error.
6. **MPI auto-discovery regression**: after Step 4, configure ROSS in a shell with `CC` and `CXX` unset (`env -u CC -u CXX cmake --preset ross-debug`). This must succeed without anyone setting `-DCMAKE_C_COMPILER=mpicc`. Re-run on both macOS (Homebrew MPICH) and a Linux box (system OpenMPI or MPICH via `module load`) before merging. If `find_package(MPI)` can't locate MPI without the wrapper-compiler hint on a supported platform, that's a configure-UX regression and Step 4 needs more attention before Phase 1 ships.
7. **PPC validation (Phase 2, when accessible)**: after Step 13 lands, ask someone with PPC system access (e.g., one of Chris's students at RPI) to run `cmake --preset ross-debug && cmake --build --preset ross-debug` with the default system compiler — almost certainly gcc on a Power9/Power10. Build must succeed with generic flags only; no `-qarch=pwr9` errors from gcc. This validates that the XL-gating actually keeps the bad flags away from non-XL compilers. If the system also has IBM XL installed, repeat there to confirm the XL path still applies the optimization flags (untested but unchanged from master). Do **not** gate Phase 2 merge on this — the gating fix is strictly safer than master regardless of whether anyone runs the validation. BGQ/BGP/BGL paths are not testable (no surviving hardware in active use).

---

## Rollback and cross-repo coordination

Phase 1 changes the ROSS install layout (header paths, package config location, library filename). Even with the back-compat preservation built into the plan (`ross.pc` kept working, uppercase shim, `${ROSS_INCLUDE_DIRS}` derived in the shim, Path B header contract matching what CODES already does), the change is cross-repo by nature — ROSS's install tree is read by CODES. This section lays out the order of operations and what to do if something breaks.

### Why the blast radius is small

By design, Phase 1 preserves both supported discovery surfaces:
- **pkg-config users** (CODES today): `ross.pc` updated in Step 7 but still produces the same kind of output (`-L`/`-I`/`-l` flags). CODES's `pkg_check_modules(ROSS ... ross)` keeps working. `${ROSS_INCLUDE_DIRS}` and `PkgConfig::ROSS` keep being populated from pkg-config output.
- **legacy `find_package(ROSS)` users** (hypothetical external researchers): the uppercase shim at `<prefix>/lib/ROSSConfig.cmake` catches the old discovery path, sets `ROSS_INCLUDE_DIRS` via `get_target_property` to match master's behavior, and aliases the bare `ROSS` target to `ross::ross`.

So the realistic breakage modes are narrow: header path collisions (something CODES `#include`'d at `<foo.h>` that's now under `<ross/foo.h>` — but Path B guarantees `<ross.h>` and any `<ross-*.h>` keep resolving), platform-specific MPI detection differences, or a missed `${ROSS_*_DIR}` reference inside a step.

### Order of operations

1. **Land PR 1 (CI restoration) and Phase 0 (PR 2 — Damaris + Coveralls + ross-config deletions) on ROSS master first.** Both are independent of CODES.
2. **Before merging Phase 1, build CODES locally against the Phase 1 install.** The plan's validation strategy already covers this; the timing is "pre-merge," not "post-merge." Concretely:
   - Build ROSS from the `cmake-improvements` branch, install to `install/debug`.
   - In CODES: `rm -rf build/debug` (or `cmake --fresh`) to clear the cached `pkgcfg_lib_ROSS_ROSS:FILEPATH=...libROSS.a` entry that points at the old library filename.
   - Reconfigure and build CODES. Run its test suite. Iterate on whichever side breaks.
3. **Merge Phase 1 only after CODES builds clean.** This is the cheap-coordination version of a contract test (the real one is post-Phase-3 — see [future-refactor-tasks.md](future-refactor-tasks.md) "CODES contract test").
4. **After ROSS Phase 1 lands**, CODES users (just you for now) need to `cmake --fresh` or delete their CODES build directory once to flush the stale pkg-config cache. The first reconfigure picks up the new library name (`libross.{a,so}`) and new include path (`<prefix>/include/ross/`).

### If CODES breaks anyway

Three options, in order of preference:

1. **Fix the consumer side.** Most likely root cause if the break is real — e.g., CODES has a stray `#include <buddy.h>` reaching for an internal header that's no longer installed, or a hardcoded library path in a CODES CMakeLists.txt. Fix CODES, push to its master, done. ROSS doesn't need to revert.
2. **Pin CODES to a pre-Phase-1 ROSS commit temporarily.** If the CODES-side fix is non-trivial and the immediate priority is "unblock other work," CODES can specify an exact ROSS install path (`ROSS_PKG_CONFIG_PATH=<old-install>/lib/pkgconfig`) until the proper fix lands. Use sparingly — it's a workaround, not a solution.
3. **Revert the ROSS PR.** Last resort, only if Phase 1 turns out to have a fundamental design flaw that isn't fixable in a follow-up. Because Phase 1 should land as a single PR with one commit per step (per the sequencing recommendation), revert is one `git revert <merge-commit>` on ROSS master. CODES sees the old install layout again on next reconfigure, no CODES change required.

### Bundle Phase 1 into one PR

This matters specifically for rollback: a single Phase 1 PR with one commit per step is **one** revertible unit. Splitting Phase 1 across multiple PRs would mean reverting in inverse order, which is more annoying but still works (each step is independently revertible).

The Sequencing section above already recommends bundling Phase 1 into one PR with commits per step. The rollback ergonomics are one of the reasons.

### What this does NOT need

This isn't a microservices deploy with rolling cutover and canary stages. It's three personal repos on one workstation. The "rollback story" boils down to: validate manually, merge, observe, revert if needed. No release flags, no migration window, no coordinated cut-over. The contract test (post-Phase-3) is what graduates this from "manual local validation" to "CI catches regressions automatically" — until then, the human-in-the-loop step is acceptable for the volume of work involved.

---

## Out of scope

Cleanup work that's been identified during this refactor but is deliberately not in scope for the `cmake-improvements` branch is tracked in [future-refactor-tasks.md](future-refactor-tasks.md). At time of writing that covers:

- Extern-declaration / definition-file mismatches across `core/*.h`
- Public/private header split (stop installing internal-only headers like `buddy.h`, `lz4.h`, `hash-quadratic.h`, queue/clock/gvt implementation headers, etc.)
- Source-tree restructure to `core/include/ross/` + `core/src/` so build-tree and install-tree layouts become identical
- Tightening the consumer contract from Path B (`<ross.h>` plus top-level `ross-*.h`) to Path A (`<ross.h>` only)

If a scope-creep candidate surfaces while implementing the plan, add a section to `future-refactor-tasks.md` rather than expanding the PR.

---

## Critical files for implementation

- `CMakeLists.txt`
- `core/CMakeLists.txt`
- `core/ROSSConfig.cmake` (delete/replace)
- `core/cmake/rossConfig.cmake.in` (new file)
- `core/ross.pc.in`
- `core/ross-config.in` — deleted in Step 0c. Was a hand-rolled shell-script discovery wrapper redundant with pkg-config and `find_package(ross)`. Grep across CODES / NetMaestro confirmed no callers.
- `models/CMakeLists.txt` and `models/phold/CMakeLists.txt` (Phase 3 dogfooding)
