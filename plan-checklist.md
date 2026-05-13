# ROSS CMake Modernization — Task Checklist

See [plan.md](plan.md) for full context and rationale.

## Pre-flight decisions (resolve before Phase 1 lands)
- [x] **`core/ross-config.in` — resolved: delete (Step 0c).** Grep across CODES, NetMaestro, and ROSS itself returned zero references outside the script's own definition. pkg-config (`ross.pc`) and CMake config (`rossConfig.cmake`) cover the same discovery needs. Deletion is part of Phase 0's PR 2 alongside Damaris and Coveralls removal — same "dead/redundant infrastructure" theme
- [x] **Consumer-facing API surface — resolved: Path B.** External consumers may `#include <ross.h>` or any top-level `ross-*.h` header. Subdirectory and internal headers are unsupported (reachable via BUILD_INTERFACE only). Path B matches what CODES already does — no CODES source changes required. Tighter Path A (single `<ross.h>` entry point) is deferred to [future-refactor-tasks.md](future-refactor-tasks.md). Documentation tasks for this decision live under Step 6 below

## Scope-creep policy
- If new cleanup candidates surface during implementation, add a section to [future-refactor-tasks.md](future-refactor-tasks.md) rather than expanding the active PR

## PR 1 — Pre-flight CI restoration (lands before any CMake work)

**Why first**: ROSS's existing Travis CI has been dead for years (Travis ended free OSS in late 2020; Ubuntu Trusty image dropped 2022; codecov bash uploader deprecated; MPICH renamed from `mpich2` ~2013; doc-publish `GH_TOKEN` stale). The multi-step CMake refactor benefits from automated regression detection. This PR is deliberately minimal — full multi-platform / multi-MPI / coverage setup is tracked in [future-refactor-tasks.md](future-refactor-tasks.md) "CI re-establishment (comprehensive)" and lands after Phase 3.

### Step CI — Minimal CI restoration
- [ ] Delete `.travis.yml` — actively misleading (looks like CI exists when it hasn't worked in years)
- [ ] Add `.github/workflows/build.yml` — single Ubuntu 24.04 + MPICH job that configures, builds, runs `ctest`, **and runs `cmake --install`**. See plan.md "Pre-flight Step CI" for the exact YAML
- [ ] Include an `Install` step in the workflow (after `Test`): `cmake --install build` with `CMAKE_INSTALL_PREFIX=$PWD/install` set at configure time. Load-bearing for the CMake modernization — Phase 1 (PR 6) restructures the install tree, and install-time regressions (`install(EXPORT ...)`, header destinations, `ross.pc` generation) go undetected without this step
- [ ] Triggers: `push` on `master` and `cmake-improvements`; `pull_request` on `master`
- [ ] Verify the workflow runs green on `master` before opening any subsequent PR. If it fails, fix `master` first (probably small Trusty-era assumptions surfacing on Ubuntu 24.04) — do not stack CMake changes on a red baseline
- [ ] The manual `cmake -S . -B build -D...` form is the permanent shape of the CI invocation — no preset migration follow-up. The committed `CMakePresets.json` on this branch is being removed in PR 2 (Step 0d); no `CMakePresets.json` will ship upstream

## Phase 0 — Dead-subsystem deletions (PR 2, single combined PR)

Steps 0, 0b, 0c, and 0d are all independent deletions of dead, redundant, or personal-scaffolding infrastructure that shrink the surface area of downstream phases. Combine into one PR — review each step as its own commit.

### Step 0 — Remove `USE_DAMARIS` build paths
- [ ] Delete `OPTION(USE_DAMARIS ...)` and the dependent `IF(USE_DAMARIS) ... ADD_SUBDIRECTORY(risa) ... INCLUDE_DIRECTORIES(${DAMARIS_INCLUDE}) ... ENDIF()` block at [core/CMakeLists.txt:119-125](core/CMakeLists.txt#L119-L125)
- [ ] Delete `IF(USE_DAMARIS) INCLUDE_DIRECTORIES(${DAMARIS_INCLUDE}) ENDIF` at [models/phold/CMakeLists.txt:2-4](models/phold/CMakeLists.txt#L2-L4)
- [ ] Delete the `IF(USE_DAMARIS) ... ELSE` Damaris branch at [models/phold/CMakeLists.txt:27-37](models/phold/CMakeLists.txt#L27-L37) — collapse to the plain `target_link_libraries(... ROSS m)` form
- [ ] Leave `core/risa/` submodule and `#cmakedefine USE_DAMARIS` in `config.h.in` in place — both inert without the CMake option, and a future PR can rip them out
- [ ] Update [CLAUDE.md](CLAUDE.md): remove the Damaris/RISA bullet from "Optional subsystems"; note Damaris is disabled pending future removal of C source paths
- [ ] Verify a clean configure + build still succeeds with `USE_DAMARIS` no longer existing as a cache variable (run `cmake --preset ross-debug && cmake --build --preset ross-debug`)

### Step 0b — Remove the Coveralls coverage tracking path

**Why**: dead subsystem (no working CI drives it), latent break under Step 2 (Coveralls.cmake:34 path-derivation depends on the nested `project()` we're removing), and global-flag mutation inconsistent with Step 13. See plan.md Step 0b for the three independent reasons in full.

- [ ] Delete `option(COVERALLS ...)` and the `if(COVERALLS) include(Coveralls); coveralls_turn_on_coverage(); endif()` block at top-level [CMakeLists.txt:43-48](CMakeLists.txt#L43-L48)
- [ ] Delete the `if(COVERALLS) ... coveralls_setup(...) endif()` block at [core/CMakeLists.txt:157-167](core/CMakeLists.txt#L157-L167)
- [ ] Delete `core/cmake/Coveralls.cmake`
- [ ] Delete `core/cmake/CoverallsClear.cmake`
- [ ] Delete `core/cmake/CoverallsGenerateGcov.cmake`
- [ ] No CLAUDE.md update needed — Coveralls isn't mentioned there
- [ ] Verify clean configure + build still succeeds: `cmake --preset ross-debug && cmake --build --preset ross-debug`. The minimal CI workflow from PR 1 will catch any regression on push

**Audit note from review item #6**: [docs/CMakeLists.txt](docs/CMakeLists.txt) also audited as part of this work. Uses `${CMAKE_BINARY_DIR}` and `${CMAKE_CURRENT_SOURCE_DIR}` (always-valid globals), not `${ROSS_SOURCE_DIR}` / `${ROSS_BINARY_DIR}`. Unaffected by Step 2's nested-`project()` removal. **No changes needed in docs/.**

### Step 0c — Delete the `ross-config` shell-script wrapper

**Why**: hand-rolled shell-script discovery wrapper (`--cflags` / `--ldflags` / `--libs` / `--cc` / `--cxx` / `--ld`) fully redundant with pkg-config (`ross.pc`, Step 7) and CMake `find_package(ross)` (Step 5). Grep across CODES, NetMaestro, and ROSS itself returned zero references outside the script's own definition and CMake plumbing.

- [ ] Delete `core/ross-config.in`
- [ ] Delete `SET(ROSS_CC $ENV{CC})` and `SET(ROSS_CXX $ENV{CXX})` lines at [core/CMakeLists.txt:177-178](core/CMakeLists.txt#L177-L178) (only used to feed the script's template substitution)
- [ ] Delete `CONFIGURE_FILE(ross-config.in ross-config @ONLY)` at [core/CMakeLists.txt:180](core/CMakeLists.txt#L180)
- [ ] Delete the two `SET_SOURCE_FILES_PROPERTIES(...)` lines for `ross-config.in` / `ross-config` (currently lines ~182-183)
- [ ] Delete the `INSTALL(FILES ${ROSS_BINARY_DIR}/ross-config DESTINATION bin PERMISSIONS ...)` line at [core/CMakeLists.txt:187](core/CMakeLists.txt#L187)
- [ ] **Don't** delete `STRING(REPLACE ... CMAKE_INSTALL_PREFIX_ESCAPED ...)` at [core/CMakeLists.txt:179](core/CMakeLists.txt#L179) in this step — `ross.pc.in` also uses `@CMAKE_INSTALL_PREFIX_ESCAPED@` today. Step 7 rewrites `ross.pc.in` to drop the escaped variant; the `STRING(REPLACE ...)` line goes away then. (If Steps 0c and 7 land in the same PR, the line can be deleted in either; order doesn't matter as long as both happen.)
- [ ] No CLAUDE.md update needed — `ross-config` isn't mentioned there
- [ ] Verify build + install still succeed: `cmake --preset ross-debug && cmake --build --preset ross-debug && cmake --install build/debug`. No `bin/ross-config` should appear in `install/debug/bin/`

### Step 0d — Remove the committed `CMakePresets.json`

**Context**: `CMakePresets.json` was added in commit `6581ba2d` on the `cmake-improvements` branch (NOT on master) — it was personal scaffolding that should have been `CMakeUserPresets.json` (which `.gitignore` already excludes). Removing it before any PR opens means no `CMakePresets.json` ships upstream; the developer keeps a local `CMakeUserPresets.json` with the same `ross-debug` / `ross-release` content for their own workflow.

- [ ] `git rm CMakePresets.json` on the `cmake-improvements` branch. The file lives only on this branch (`git branch --contains 6581ba2d` returns `cmake-improvements` only)
- [ ] Confirm `.gitignore` already excludes `CMakeUserPresets.json` so the developer's local copy stays untracked (the original commit `6581ba2d` titled "build: add presets file and ignore user presets" suggests this is already in place — verify)
- [ ] Re-copy the file's content to a local `CMakeUserPresets.json` (gitignored) before deleting if you want `cmake --preset ross-debug` to keep working in your personal workflow
- [ ] **Resolves P5**: the "cmake_minimum_required 3.5 vs. preset 3.28.1 inconsistency" goes away — only the source-side `3.5` half remains, addressed by Step 1
- [ ] **Makes Step 16 moot** — there's no preset minimum to align anymore
- [ ] No CLAUDE.md update needed in this step — `CMakePresets.json` isn't mentioned in CLAUDE.md by name, though the "Build" section documents `cmake --preset ross-debug` invocations. Those continue to work for the developer against their local `CMakeUserPresets.json`; if the project ever onboards another developer, that's when CLAUDE.md should be revisited to either document a manual-flag fallback or ship a project-wide preset deliberately
- [ ] Verify the branch still builds with manual flags after the preset removal: `cmake -S . -B build -DROSS_BUILD_MODELS=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && cmake --install build`

## Phase 1 — Export story (unblocks `codes` using `find_package(ross)`)

Phase 1 ships as **four PRs**, not one. The three small-and-independent steps (1, 4, 4b) go as PRs 3, 4, 5; the tightly-coupled export work (Steps 2, 3, 5, 6, 7, 11) goes as PR 6 — the actual "export bundle." See plan.md "Sequencing recommendation" for rationale; see "Phase 1 sequencing note" at the end of this Phase 1 section for the per-PR dependency map.

### Step 1 (PR 3) — Bump `cmake_minimum_required` and fix top-level `project()`
- [ ] Bump top-level `cmake_minimum_required` to 3.21
- [ ] Move `git_describe_working_tree` call from `core/CMakeLists.txt` up to top-level `CMakeLists.txt` (must run before `project()`)
- [ ] Parse `VERSION_MAJOR/MINOR/PATCH` from git describe output
- [ ] Compute `ROSS_DETECTED_VERSION` as `${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}`
- [ ] **Add fallback**: if `ROSS_DETECTED_VERSION` doesn't match `^[0-9]+\.[0-9]+\.[0-9]+$` (tarball / shallow / no-tag build), default to `0.0.0` and emit a warning. `project(VERSION ...)` errors on malformed input — this is load-bearing, not cosmetic
- [ ] Replace `PROJECT(ROSS_TOP C)` with `project(ross VERSION ${ROSS_DETECTED_VERSION} DESCRIPTION ... HOMEPAGE_URL ... LANGUAGES C)`
- [ ] Keep `VERSION_SHA1` extraction (for `config.h`) — move it to after `project()`
- [ ] Verify `${ross_VERSION}`, `${ross_VERSION_MAJOR}` etc. are populated
- [ ] Regression test: delete `.git/` from a copy of the tree, reconfigure, confirm build succeeds with `0.0.0`

### Step 2 (PR 6) — Drop nested `project()` in `core/` and set up target hygiene
- [ ] Delete `PROJECT(ROSS C)` line in `core/CMakeLists.txt`
- [ ] **Sweep `${ROSS_SOURCE_DIR}` / `${ROSS_BINARY_DIR}` references** — both become empty after the nested project goes away. Without this sweep, four `INSTALL(...)` lines and three `INCLUDE_DIRECTORIES(...)` lines silently install/look in `/`:
  - [ ] `core/CMakeLists.txt`: replace `${ROSS_SOURCE_DIR}` → `${CMAKE_CURRENT_SOURCE_DIR}` and `${ROSS_BINARY_DIR}` → `${CMAKE_CURRENT_BINARY_DIR}` (lines 2, 187, 188, 189, 193)
  - [ ] `models/phold/CMakeLists.txt`: delete `INCLUDE_DIRECTORIES(${ROSS_BINARY_DIR})` and `INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ...)` lines outright at [models/phold/CMakeLists.txt:1-9](models/phold/CMakeLists.txt#L1-L9) — Step 3's `BUILD_INTERFACE` propagation via `ross::ross` covers them. The raw-path install at [line 58](models/phold/CMakeLists.txt#L58) is replaced by Step 11
  - [ ] **Step 11 must land in the same PR as Step 2** — without it, the install at line 58 is broken. See updated Phase 1 sequencing
- [ ] Remove `INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ${ROSS_BINARY_DIR})` directory-scope include (replaced in step 3)
- [ ] Rename target: `add_library(ross ${ross_srcs})`
- [ ] Add `add_library(ross::ross ALIAS ross)`
- [ ] Add back-compat alias `add_library(ROSS ALIAS ross)` for in-tree models during transition
- [ ] **Do not set `OUTPUT_NAME`** — CMake's default (target name) gives `libross.{a,so}` automatically. Library filename is intentionally renamed `libROSS.{a,so}` → `libross.{a,so}` for end-to-end lowercase consistency
- [ ] Set `EXPORT_NAME ross` on the target so the namespaced export is `ross::ross`
- [ ] Replace plain `TARGET_LINK_LIBRARIES(ROSS ...)` with keyword-signature form
- [ ] **Verify the rename**: after `cmake --install build/debug`, confirm `<install-prefix>/lib/libross.{a,so,dylib}` exists and `<install-prefix>/lib/libROSS.*` does NOT exist. Grep across CODES / NetMaestro confirmed zero source-tree references to `-lROSS` or `libROSS` — only build artifacts that auto-regenerate on consumer reconfigure

### Step 3 (PR 6) — Attach include dirs with generator expressions
- [ ] Add `target_include_directories(ross PUBLIC ...)` with `$<BUILD_INTERFACE:...>` for source + binary dir
- [ ] Add `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ross>` for installed consumers

### Step 4 (PR 4) — Use `MPI::MPI_C` imported target (most important for relocatability)

**Configure-time UX target**: after this step, `cmake --preset ross-debug` (or `cmake -S . -B build ...`) works with the system C compiler — no `CC=mpicc` or `-DCMAKE_C_COMPILER=mpicc` required. `find_package(MPI)` does the discovery, `MPI::MPI_C` carries everything. Users hint with `-DMPI_HOME=...` / `module load` for non-standard installs.

- [ ] **Delete** [core/cmake/SetupMPI.cmake](core/cmake/SetupMPI.cmake) — BLT-derived legacy, nothing modern needs it
- [ ] Replace `INCLUDE(SetupMPI)` at [CMakeLists.txt:149](CMakeLists.txt#L149) with `find_package(MPI REQUIRED COMPONENTS C)` directly. The `REQUIRED` keyword converts the existing soft-warn at lines 153-157 into a hard error (correct — ROSS does not build without MPI)
- [ ] Delete the entire `IF(MPI_C_FOUND) ... ELSE(MPI_C_FOUND) ... ENDIF` block at [CMakeLists.txt:150-157](CMakeLists.txt#L150-L157) (`INCLUDE_DIRECTORIES(${MPI_C_INCLUDE_PATH})` and `LIST(APPEND ROSS_EXTERNAL_LIBS ${MPI_C_LIBRARIES})`)
- [ ] Delete `TARGET_INCLUDE_DIRECTORIES(ROSS INTERFACE ${MPI_C_INCLUDE_PATH})` from `core/`
- [ ] Set `target_link_libraries(ross PUBLIC MPI::MPI_C)` — MPI must be `PUBLIC` because [core/ross.h:81](core/ross.h#L81) does `#include <mpi.h>` (confirmed, not a hedge)
- [ ] Add `target_link_libraries(ross PRIVATE m)` for libm
- [ ] **Update [CLAUDE.md](CLAUDE.md)** — Build section: drop `-DCMAKE_C_COMPILER=mpicc` from the manual configure example. Add a one-liner noting MPI is auto-discovered; users hint with `-DMPI_HOME=...` or `module load <mpi>` for non-standard installs. Do not direct users to set `CC=mpicc` anywhere
- [ ] **Regression-test the new configure UX**: run `env -u CC -u CXX cmake --preset ross-debug` on macOS (Homebrew MPICH) and on a Linux box with a typical MPI install — both must configure clean without anyone overriding the C compiler. If they don't, Step 4 isn't done

### Step 4b (PR 5) — Honor `BUILD_SHARED_LIBS` directly; retire `ROSS_BUILD_SHARED_LIBS`
**Problem**: [core/CMakeLists.txt:150-151](core/CMakeLists.txt#L150-L151) defines a custom `ROSS_BUILD_SHARED_LIBS` option that maps to the standard `BUILD_SHARED_LIBS`. Two knobs for one decision, and `set(BUILD_SHARED_LIBS ...)` without `CACHE` shadows the parent project when ROSS is `add_subdirectory`'d.

- [ ] Delete the `OPTION(ROSS_BUILD_SHARED_LIBS ...)` and the `SET(BUILD_SHARED_LIBS ${ROSS_BUILD_SHARED_LIBS})` lines at [core/CMakeLists.txt:150-151](core/CMakeLists.txt#L150-L151)
- [ ] Confirm the Step 2 `add_library(ross ${ross_srcs})` has no explicit `STATIC`/`SHARED` keyword — CMake then defers to `BUILD_SHARED_LIBS` (default static when unset)
- [ ] Add a deprecation shim at the top of `core/CMakeLists.txt` so existing `-DROSS_BUILD_SHARED_LIBS=ON` invocations still work for one release:
  ```cmake
  if(DEFINED ROSS_BUILD_SHARED_LIBS)
      message(DEPRECATION "ROSS_BUILD_SHARED_LIBS is deprecated; use BUILD_SHARED_LIBS")
      if(NOT DEFINED BUILD_SHARED_LIBS)
          set(BUILD_SHARED_LIBS ${ROSS_BUILD_SHARED_LIBS} CACHE BOOL "" FORCE)
      endif()
  endif()
  ```
- [ ] **Update [CLAUDE.md](CLAUDE.md)** — under "Build", add: "Default is a static library. Pass `-DBUILD_SHARED_LIBS=ON` to build shared."
- [ ] Leave `CMAKE_POSITION_INDEPENDENT_CODE ON` (top-level [CMakeLists.txt:4](CMakeLists.txt#L4)) in place — needed so a static ROSS can be linked into downstream shared objects
- [ ] Verify build still works in both modes: `cmake --preset ross-debug` (static, default) and `cmake --preset ross-debug -DBUILD_SHARED_LIBS=ON` (shared)
- [ ] Verify the deprecation shim emits the warning and maps correctly: `cmake --preset ross-debug -DROSS_BUILD_SHARED_LIBS=ON` should warn and still produce `libross.{so,dylib}`
- [ ] Schedule shim removal as a separate item in [future-refactor-tasks.md](future-refactor-tasks.md) (one release after Phase 1 ships)

### Step 5 (PR 6) — Generate proper package config files
- [ ] Create `core/cmake/rossConfig.cmake.in` template with `@PACKAGE_INIT@`, `find_dependency(MPI REQUIRED COMPONENTS C)`, include of `rossTargets.cmake`, `check_required_components(ross)`
- [ ] **Do NOT set `ROSS_LIBRARIES`, `ROSS_INCLUDE_DIRS`, or `ROSS_FOUND` in the canonical template.** Modern CMake convention is imported-targets-only; legacy `<PKG>_LIBRARIES`-style variables are not part of new package configs. Master only ever set `ROSS_INCLUDE_DIRS`, and CODES doesn't read that from rossConfig.cmake anyway (it gets it from pkg-config). If genuine back-compat is needed, it lives in the uppercase shim below
- [ ] Add a Path B consumer-contract comment block at the top of the template — see Step 6 docs task
- [ ] Add `include(GNUInstallDirs)` and `include(CMakePackageConfigHelpers)` in `core/CMakeLists.txt`
- [ ] Replace `INSTALL(TARGETS ROSS EXPORT ROSS-targets DESTINATION lib)` with full `install(TARGETS ross EXPORT rossTargets LIBRARY/ARCHIVE/RUNTIME/INCLUDES DESTINATION ...)` form
- [ ] Add `install(EXPORT rossTargets FILE rossTargets.cmake NAMESPACE ross:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ross)`
- [ ] Call `configure_package_config_file(...)`. The `PATH_VARS CMAKE_INSTALL_INCLUDEDIR` argument is no longer needed (it was for `@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@` expansion inside the now-removed `ROSS_INCLUDE_DIRS` line in the template)
- [ ] Call `write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)`
- [ ] Install generated `rossConfig.cmake` + `rossConfigVersion.cmake` to `${CMAKE_INSTALL_LIBDIR}/cmake/ross`
- [ ] Add `export(EXPORT rossTargets ... NAMESPACE ross::)` for build-tree / FetchContent consumers
- [ ] Delete old `core/ROSSConfig.cmake`
- [ ] **Install uppercase back-compat shim** at `<prefix>/${CMAKE_INSTALL_LIBDIR}/ROSSConfig.cmake` (legacy location — what `-DROSS_DIR=<prefix>/lib` hints point at). Shim must:
  - Emit a `message(DEPRECATION ...)` directing users to `find_package(ross)` + `ross::ross`
  - `include(${CMAKE_CURRENT_LIST_DIR}/cmake/ross/rossConfig.cmake)` to delegate discovery
  - **Preserve `ROSS_INCLUDE_DIRS`** by deriving from the imported target: `get_target_property(_dirs ross::ross INTERFACE_INCLUDE_DIRECTORIES); set(ROSS_INCLUDE_DIRS ${_dirs}); unset(_dirs)`. This is the one variable master's hand-written ROSSConfig.cmake set; the shim preserves it for legacy `find_package(ROSS)` + `${ROSS_INCLUDE_DIRS}` callers
  - Define `add_library(ROSS ALIAS ross::ross)` (guarded by `if(NOT TARGET ROSS)`) so legacy `target_link_libraries(... ROSS)` call sites keep linking
  - Removal of this shim is tracked under "Deprecation shim removals" in [future-refactor-tasks.md](future-refactor-tasks.md)

### Step 6 (PR 6) — Install headers under `include/ross/`
- [ ] Replace catch-all header install with scoped `install(DIRECTORY ... DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross ...)` (exclude `cmake`, `risa` — `risa` exclude is defensive since Step 0 already disabled the build path). **Verify the `PATTERN ... EXCLUDE` syntax actually skips the `risa/` and `cmake/` subdirectories** — `PATTERN` matches basenames; if it doesn't work as expected, fall back to `REGEX "/(cmake|risa)/" EXCLUDE` placed outside the `FILES_MATCHING` block
- [ ] Install generated `config.h` to `${CMAKE_INSTALL_INCLUDEDIR}/ross` **without renaming** — the `/ross/` subdir already isolates it from consumer collisions, and renaming would require edits to [core/ross-base.h](core/ross-base.h) and [core/ross-random.h](core/ross-random.h) which `#include "config.h"`. Earlier draft said to rename to `ross-config-build.h`; that's been dropped
- [ ] Verify `#include <ross.h>` still resolves via exported INCLUDES destination
- [ ] Verify installed `ross-base.h`'s `#include "config.h"` resolves (quoted-include search finds `config.h` in the same installed directory)
- [ ] **Document the Path B consumer contract** (resolved in pre-flight) in two places:
  - [ ] Add a "Consumer-facing API surface" subsection to [CLAUDE.md](CLAUDE.md) near the existing "Consumer compatibility (CODES)" section. Spell out: `<ross.h>` and any top-level `ross-*.h` are supported; subdirectory headers (`<instrumentation/...>`, `<check-revent/...>`, `<queue/...>`, `<rio/...>`) and internal headers (`<buddy.h>`, `<lz4.h>`, `<hash-quadratic.h>`) are not; build-tree exposure of those is an unsupported artifact slated for removal in [future-refactor-tasks.md](future-refactor-tasks.md)
  - [ ] Add the same contract as a comment block at the top of the new [core/cmake/rossConfig.cmake.in](core/cmake/rossConfig.cmake.in) (created in Step 5)

### Step 7 (PR 6) — Fix `ross.pc` pkg-config file
- [ ] Drop quotes around `prefix = "..."` in `core/ross.pc.in`
- [ ] Use `prefix=@CMAKE_INSTALL_PREFIX@`, `libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@`, `includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@/ross`
- [ ] Use `@PROJECT_VERSION@` for the `Version:` field
- [ ] **Update `Libs:` line to `-lross`** (was `-lROSS`) — matches the library filename rename from Step 2. One-character change at the source-template level; consumers using `pkg_check_modules` pick it up transparently on reconfigure
- [ ] Install `ross.pc` to `${CMAKE_INSTALL_LIBDIR}/pkgconfig`
- [ ] **Do NOT add MPI flags** to `Cflags`/`Libs`. Intent: pkg-config is back-compat only. Consumers compile with `mpicc`, so MPI is transparent. The CMake config (Step 5) is the supported path going forward; `ross.pc` stays minimal until it's deprecated
- [ ] **Verify CODES reconfigure picks up the new flag**: after installing the new ROSS, run `cmake --preset codes-debug` (or whatever CODES preset) in CODES and confirm `pkgcfg_lib_ROSS_ROSS` in its CMakeCache.txt now resolves to `libross.{a,so}`, not `libROSS.a`

### Per-PR validation

Validation breaks down by which PR is landing. Per-step verifications already live inside each step above; this section captures the additional cross-cutting checks for each PR.

**PR 3 validation (Step 1 — CMake-minimum + project VERSION)**:
- [ ] Build ROSS with current presets: `cmake --preset ross-debug && cmake --build --preset ross-debug && cmake --install build/debug`
- [ ] Verify `${ross_VERSION}` is populated from `project(...)` after the rewrite
- [ ] Tarball regression: delete `.git/` in a copy of the source, reconfigure, confirm the `0.0.0` fallback engages without error (no `project(VERSION ...)` parse failure)
- [ ] CODES not affected — install layout unchanged

**PR 4 validation (Step 4 — MPI auto-discovery)**:
- [ ] Build ROSS with `cmake --preset ross-debug` (no `CC=mpicc`, no `-DCMAKE_C_COMPILER=mpicc`)
- [ ] **MPI auto-discovery regression**: `env -u CC -u CXX cmake --preset ross-debug` must succeed on macOS (Homebrew MPICH) AND on Linux (system MPICH/OpenMPI) without anyone overriding the C compiler. This is the UX outcome of the step
- [ ] CODES not affected — install layout unchanged

**PR 5 validation (Step 4b — `BUILD_SHARED_LIBS`)**:
- [ ] Default static build still works: `cmake --preset ross-debug` → produces `libross.{a}`
- [ ] Shared build works: `cmake --preset ross-debug -DBUILD_SHARED_LIBS=ON` → produces `libross.{so,dylib}`
- [ ] Deprecation shim emits warning: `cmake --preset ross-debug -DROSS_BUILD_SHARED_LIBS=ON` should print the DEPRECATION message and still produce a shared lib
- [ ] CODES not affected — install layout unchanged

**PR 6 validation (Steps 2/3/5/6/7/11 — the export bundle)** — this is the CODES-gating PR:
- [ ] Build + install ROSS: `cmake --preset ross-debug && cmake --build --preset ross-debug && cmake --install build/debug`
- [ ] Inspect installed `rossTargets.cmake` — confirm no absolute MPI paths in `INTERFACE_*` properties. Concrete check: `grep -rE "/opt/homebrew|/usr/local/Cellar|/usr/lib/x86_64-linux-gnu" <prefix>/lib/cmake/ross/` must return zero hits
- [ ] Verify file rename: `<prefix>/lib/libross.{a,so,dylib}` exists; `<prefix>/lib/libROSS.*` does NOT
- [ ] Verify header reorg: `<prefix>/include/ross/ross.h` exists; `<prefix>/include/ross.h` does NOT
- [ ] Verify pkg-config: `<prefix>/lib/pkgconfig/ross.pc` has `Libs: -L${libdir} -lross -lm` and `includedir=${prefix}/include/ross`
- [ ] Build a tiny test consumer **from a non-sibling directory** (e.g., `/tmp/ross-consumer/`) with `find_package(ross REQUIRED)` + `target_link_libraries(t PRIVATE ross::ross)` — catches hidden `${PROJECT_SOURCE_DIR}` assumptions that pass when the consumer is adjacent to the source
- [ ] **Pre-merge CODES validation (gating)**: before merging PR 6 to ROSS master, build CODES against the new install. This is the manual stand-in for the future CODES contract test ([future-refactor-tasks.md](future-refactor-tasks.md) "CODES contract test"). Procedure:
  - [ ] Build ROSS from `cmake-improvements` branch (with PR 6 applied), install to `install/debug`
  - [ ] In CODES: `rm -rf build/debug` (or pass `cmake --fresh`) — required to flush the cached `pkgcfg_lib_ROSS_ROSS:FILEPATH=...libROSS.a` entry; without this, the next configure won't pick up the renamed `libross.{a,so}` until the cache is cleared
  - [ ] Reconfigure CODES against the new ROSS install
  - [ ] Build CODES; run its test suite
  - [ ] Iterate on whichever side breaks (most likely a consumer-side fix in CODES — see plan.md "Rollback and cross-repo coordination" for the rollback ladder if it's not)
  - [ ] Only after CODES builds clean → merge ROSS PR 6
- [ ] **Post-merge CODES update**: after PR 6 lands on master, `cmake --fresh` (or delete the build dir) once in CODES to flush stale pkg-config cache. First reconfigure picks up `libross.{a,so}` + `<prefix>/include/ross/` automatically

### Step 11 (PR 6) — Update `models/phold/CMakeLists.txt` to use namespaced target
**Moved from Phase 3 because Step 2's removal of `${ROSS_BINARY_DIR}` invalidates the raw-path install — Step 11's `install(TARGETS ...)` rewrite is the fix. Must land in the same PR as Step 2 (PR 6, the export bundle).**

- [ ] Define `set(phold_targets phold phold_comm_test phold_gvt_hook_test phold_gvt_hook_model_test phold_gvt_hook_timestamp_test phold_gvt_hook_comm_test)`
- [ ] Replace per-target `TARGET_LINK_LIBRARIES(... ROSS m)` lines with a `foreach(t ${phold_targets}) target_link_libraries(${t} PRIVATE ross::ross m) endforeach()` (Damaris branch is gone — Step 0 removed `USE_DAMARIS`)
- [ ] Audit the `BGPM` branch — top-level option is `USE_BGPM`, the phold branch keys on `BGPM` (likely already broken). If dead, delete; if live, gate on `USE_BGPM` instead
- [ ] Replace raw-file-path install at [models/phold/CMakeLists.txt:58](models/phold/CMakeLists.txt#L58) with `install(TARGETS ${phold_targets} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})` — install all 6 variants, not just `phold`
- [ ] **Keep** the `add_library(ROSS ALIAS ross)` shim from Step 2 — cheap insurance for any out-of-tree code still referencing bare `ROSS`. Remove in a follow-up after a soak period

### Phase 1 sequencing note
- **Steps 1, 4, 4b each ship as their own PRs (PRs 3, 4, 5)** — independent of each other and of the export bundle. Order between them doesn't matter; ship whichever is ready first.
  - Step 1 (PR 3): touches top-level `CMakeLists.txt` only. No install-layout impact.
  - Step 4 (PR 4): touches top-level + `core/CMakeLists.txt` + deletes `SetupMPI.cmake` + CLAUDE.md. Wants its own macOS/Linux MPI-auto-discovery validation.
  - Step 4b (PR 5): touches `core/CMakeLists.txt` (`add_library` site) + CLAUDE.md only.
- **Steps 2, 3, 5, 6, 7, 11 bundle into PR 6 (the export bundle)** — one PR, one commit per step, in this order: 2 → 11 → 3 → 5 → 6 → 7. This is the heart of the migration and the only PR that affects CODES.
  - Inside PR 6: **Steps 2 + 11 are inseparable** — Step 2 deletes `${ROSS_BINARY_DIR}`, which Step 11 replaces with `install(TARGETS ...)`. Splitting them leaves a broken install line referencing an empty variable.
  - Inside PR 6: **Steps 3 / 5 / 6 / 7 are inseparable** — Step 3's `INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ross` commits to the Step 6 install layout, and Step 7's `ross.pc` `includedir` must match both. Splitting them produces intermediate states where exported targets point at paths that don't exist yet.
- **Why not bundle everything into one big Phase 1 PR**: PR 6's review surface is focused on the consumer-facing export contract — the only thing that affects CODES. PRs 3/4/5 each carry independent infrastructure concerns (CMake version bump, MPI discovery, shared-libs convention) that benefit from being reviewed and validated on their own merits. Gating the export work behind those is unnecessary coupling.
- **Rollback ergonomics**: PR 6 is one revertible unit (one merge commit). PRs 3/4/5 are independently revertible. If CODES breaks after PR 6 lands, revert PR 6 only — PRs 3/4/5 don't touch the install layout.

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

**Test-gating awareness** — `PROJECT_IS_TOP_LEVEL` is a NEW gate layered on top of the existing `BUILD_TESTING` and `ROSS_BUILD_MODELS` gates. All three must hold for `ctest` to discover ROSS's tests (which live in `models/phold/`). The implementer should not assume Step 9 unifies test gating — it adds a layer. See plan.md Step 9 "Test gating after this step" for the full truth table.

- [ ] Wrap `include(CTest)` / `enable_testing()` in `if(PROJECT_IS_TOP_LEVEL)`
- [ ] Wrap RPATH variable setup in `if(PROJECT_IS_TOP_LEVEL)`
- [ ] Remove any unconditional `ENABLE_TESTING()`
- [ ] Test that ROSS works as `add_subdirectory()` / `FetchContent_Declare` without hijacking parent config
- [ ] Verify the three-gate test discovery model still works as documented:
  - [ ] Top-level + `BUILD_TESTING=ON` + `ROSS_BUILD_MODELS=ON` (default presets) → `ctest` lists phold tests
  - [ ] Top-level + `BUILD_TESTING=ON` + `ROSS_BUILD_MODELS=OFF` → `ctest` lists zero tests (no `models/` subdirectory added)
  - [ ] Top-level + `BUILD_TESTING=OFF` + `ROSS_BUILD_MODELS=ON` → models build but `ctest` lists zero tests (`enable_testing()` not called, `add_test` calls are no-ops)
  - [ ] `add_subdirectory(ross)` from a wrapper project → ROSS does NOT call `include(CTest)`; parent's testing config is undisturbed

### Step 13 — Target-scoped compile options; gate IBM-XL flags on both arch AND compiler

**Problem**: today's per-arch branches inject IBM-XL-only flags (`-O5 -qprefetch=aggressive -qarch=pwr9`, etc.) based on `CMAKE_SYSTEM_PROCESSOR` alone. Result: ppc64le-with-gcc (the typical case today) gets `-qarch=pwr9` passed to gcc, which doesn't understand it and errors out. The gating fix is strictly safer than master regardless of whether anyone validates the IBM-XL path.

- [ ] **Split CLOCK selection from compile-flag injection.** Keep `if(CMAKE_SYSTEM_PROCESSOR STREQUAL ...) set(CLOCK ...) endif()` chains for CLOCK — purely architecture-driven, no change.
- [ ] Move generic flags to target-scoped, applied unconditionally:
  - `target_compile_options(ross PRIVATE -g -Wall)`
  - `target_compile_definitions(ross PRIVATE _GNU_SOURCE)`
- [ ] **Gate IBM-XL flags on BOTH `CMAKE_C_COMPILER_ID MATCHES "^XL"` AND `CMAKE_SYSTEM_PROCESSOR`**, not on system processor alone. Covers `XL` and `XLClang` in one match. Inside the XL branch, dispatch on processor for `ppc64le` (`-qarch=pwr9 -qtune=auto ...`), `bgq` (`-qarch=qp -qtune=qp ...`), `bgp` (`-qflag=i:i -qattr=full -O5`), `bgl` (same as bgp)
- [ ] Inside the XL-detected branch, add a `message(STATUS "IBM XL compiler detected — applying legacy XL optimization flags ...")` so anyone hitting the untested path gets a signal
- [ ] **Gate the `USE_BGPM` option + `imp_bgpm` IMPORTED target** at [CMakeLists.txt:94-97](CMakeLists.txt#L94-L97) on `(bgq AND XL)`. Its hardcoded `/bgsys/drivers/ppcfloor/bgpm/lib/libbgpm.a` path only exists on BGQ login nodes; without the gate, it's broken today on every non-BGQ build
- [ ] **Delete** the stray `SET(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")` lines at [CMakeLists.txt:117](CMakeLists.txt#L117) (x86_64 branch) and [CMakeLists.txt:124](CMakeLists.txt#L124) (aarch64 branch) — CXX isn't an enabled language, they're dead copy-paste mangles
- [ ] **Verify on x86_64 and aarch64**: build succeeds with generic flags only, no XL flags injected. Cheap, run as part of normal Phase 2 dev
- [ ] **PPC validation (optional, when accessible)**: ask someone at RPI with PPC access to run `cmake --preset ross-debug && cmake --build --preset ross-debug`. Must succeed with gcc — no `-qarch=pwr9` errors. **Do not gate Phase 2 merge on this** — the gating fix is strictly safer than master regardless. See plan.md Validation strategy item 7
- [ ] BGQ/BGP/BGL are not testable (no surviving hardware in active use). Gated-and-dormant. Deletion tracked in [future-refactor-tasks.md](future-refactor-tasks.md) "Untested legacy code paths"

### Step 14 — Move `ROSS_OPTION_LIST` to target compile definition
- [ ] Remove `SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DROSS_OPTION_LIST=...")` line
- [ ] Add `target_compile_definitions(ross PRIVATE ROSS_OPTION_LIST="${OPTIONS}")`

---

## Phase 3 — Polish / dogfood

### Step 10 — Replace `FILE(GLOB_RECURSE)` in `models/CMakeLists.txt`
- [ ] Replace recursive glob with explicit `add_subdirectory(phold)` (and any other models). Note: the build-dir hazard is modest in practice (build dir is `ross/build/`, not `ross/models/build/`) — the real reason is that globs skip new/removed files until reconfigure and `FOLLOW_SYMLINKS` is a footgun
- [ ] Delete the now-orphaned `cmake_policy(SET CMP0009 NEW)` line at [models/CMakeLists.txt:56](models/CMakeLists.txt#L56). Its only purpose was to make the deleted `FILE(GLOB_RECURSE ... FOLLOW_SYMLINKS)` follow symlinks; with the glob gone, it has no remaining caller

### Step 11 — moved to Phase 1 (see above)

### Step 12 — Use `MPIEXEC_*` variables in test functions
- [ ] Convert positional `ADD_TEST(name cmd args)` to keyword `add_test(NAME ... COMMAND ...)` form — **prerequisite**, since the positional form does not expand generator expressions
- [ ] Update `ROSS_TEST_SCHEDULERS` function to use `${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${MPIEXEC_PREFLAGS} ... ${MPIEXEC_POSTFLAGS}` **for MPI-launched tests only** (Conservative, Optimistic, Realtime, and all `_INST_*` tests that use `mpirun -np 2` today)
- [ ] Use the bare `$<TARGET_FILE:${target_name}>` form (no `${MPIEXEC_*}` prefix) for tests that today run the binary directly: `_SCHED_Sequential`, `_SCHED_OptDebug`, `_SCHED_RollbackCheck`, and `_INST_Seq`. Wrapping these in `${MPIEXEC_EXECUTABLE}` would break the single-rank contract that `--synch=1`, `--synch=4`, and `--synch=6` depend on
- [ ] Update `ROSS_TEST_INSTRUMENTATION` function similarly (most are MPI; `_INST_Seq` is the sole sequential case)
- [ ] Switch target path to `$<TARGET_FILE:${target_name}>` generator expression in both MPI and sequential forms (drops the implicit CWD dependency from `./${target_name}`)

### Step 15 — Normalize option names with `ROSS_` prefix (optional)
**Scope clarification**: this is a CMake-cache rename only, not a C-source `#ifdef` rename. `core/config.h.in` and the 40+ `#ifdef USE_RIO` / `#ifdef AVL_TREE` / etc. sites in `core/*.c` and `core/*.h` stay on the historical macro names. A bridge block copies the new CMake variables back into the old names before `configure_file(config.h.in config.h)` runs, so the generated `config.h` is byte-identical to today. C-side macro rename is deferred to a follow-up.
- [ ] Rename the `option(...)` declaration: `AVL_TREE` → `ROSS_USE_AVL_TREE`
- [ ] Rename the `option(...)` declaration: `USE_RIO` → `ROSS_USE_RIO`
- [ ] Rename the `option(...)` declaration: `USE_DAMARIS` → `ROSS_USE_DAMARIS`
- [ ] Rename the `option(...)` declaration: `USE_RAND_TIEBREAKER` → `ROSS_USE_RAND_TIEBREAKER`
- [ ] Rename the `option(...)` declaration: `RAND_NORMAL` → `ROSS_RAND_NORMAL`
- [ ] Rename the `option(...)` declaration: `COVERALLS` → `ROSS_COVERALLS` (moot if Step 0b lands first — listed for completeness)
- [ ] Rename the `option(...)` declaration: `USE_BGPM` → `ROSS_USE_BGPM`
- [ ] Update every internal CMake reference to use the new name (e.g., the `if(AVL_TREE) set(ross_srcs ${ross_srcs} avl_tree.h avl_tree.c) endif()` block in `core/CMakeLists.txt` becomes `if(ROSS_USE_AVL_TREE) ...`)
- [ ] **Do NOT modify `core/config.h.in`** — the `#cmakedefine USE_RIO` / `#cmakedefine AVL_TREE` / etc. directives stay on the historical names so they remain aligned with the `#ifdef` sites in `core/*.c` / `core/*.h`
- [ ] **Add the CMake-to-CMake bridge block** immediately before `configure_file(config.h.in config.h)` in `core/CMakeLists.txt`: `set(USE_RIO ${ROSS_USE_RIO})`, `set(AVL_TREE ${ROSS_USE_AVL_TREE})`, `set(USE_DAMARIS ${ROSS_USE_DAMARIS})`, `set(USE_RAND_TIEBREAKER ${ROSS_USE_RAND_TIEBREAKER})`, `set(RAND_NORMAL ${ROSS_RAND_NORMAL})`, `set(USE_BGPM ${ROSS_USE_BGPM})`. This keeps `#cmakedefine` substitution working without touching the C sources
- [ ] Verify the generated `<build>/core/config.h` is byte-identical before and after the rename (run `cmake --preset ross-debug` on master, save `build/debug/core/config.h`, then on the rename branch, diff — must be empty)
- [ ] Add deprecation shims for each old name (one-release warning then removal). Each shim guards on `if(DEFINED OLDNAME AND NOT DEFINED ROSS_NEWNAME)` and uses `CACHE BOOL "" FORCE` so the cache picks up the value cleanly
- [ ] Track each shim's removal in [future-refactor-tasks.md](future-refactor-tasks.md) "Deprecation shim removals" (entries already exist)
- [ ] Schedule the C-side `#ifdef` rename as a separate item in [future-refactor-tasks.md](future-refactor-tasks.md) — when it ships, the bridge block above goes away

### Step 16 — REMOVED (moot)
- [x] **Removed**: Step 0d deletes the committed `CMakePresets.json` (it was personal scaffolding, not project infrastructure). With no preset shipping upstream, there's no `cmakeMinimumRequired` to align. Top-level minimum is set per Step 1 (3.21) without preset-driven pressure. Phase 3 is now just Steps 10, 12, 15.
