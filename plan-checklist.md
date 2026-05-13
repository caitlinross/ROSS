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
- [ ] Add `.github/workflows/build.yml` — single Ubuntu 24.04 + MPICH job that configures, builds, and runs `ctest`. See plan.md "Pre-flight Step CI" for the exact YAML
- [ ] Triggers: `push` on `master` and `cmake-improvements`; `pull_request` on `master`
- [ ] Verify the workflow runs green on `master` before opening any subsequent PR. If it fails, fix `master` first (probably small Trusty-era assumptions surfacing on Ubuntu 24.04) — do not stack CMake changes on a red baseline
- [ ] After Phase 1 lands, update the `Configure` step to use `cmake --preset ross-debug` instead of manual flags. One-line follow-up

## Phase 0 — Dead-subsystem deletions (PR 2, single combined PR)

Steps 0, 0b, and 0c are all independent deletions of dead or redundant infrastructure that shrink the surface area of downstream phases. Combine into one PR — review each step as its own commit.

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

## Phase 1 — Export story (unblocks `codes` using `find_package(ross)`)

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
- [ ] **Do not set `OUTPUT_NAME`** — CMake's default (target name) gives `libross.{a,so}` automatically. Library filename is intentionally renamed `libROSS.{a,so}` → `libross.{a,so}` for end-to-end lowercase consistency
- [ ] Set `EXPORT_NAME ross` on the target so the namespaced export is `ross::ross`
- [ ] Replace plain `TARGET_LINK_LIBRARIES(ROSS ...)` with keyword-signature form
- [ ] **Verify the rename**: after `cmake --install build/debug`, confirm `<install-prefix>/lib/libross.{a,so,dylib}` exists and `<install-prefix>/lib/libROSS.*` does NOT exist. Grep across CODES / NetMaestro confirmed zero source-tree references to `-lROSS` or `libROSS` — only build artifacts that auto-regenerate on consumer reconfigure

### Step 3 — Attach include dirs with generator expressions
- [ ] Add `target_include_directories(ross PUBLIC ...)` with `$<BUILD_INTERFACE:...>` for source + binary dir
- [ ] Add `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ross>` for installed consumers

### Step 4 — Use `MPI::MPI_C` imported target (most important for relocatability)

**Configure-time UX target**: after this step, `cmake --preset ross-debug` (or `cmake -S . -B build ...`) works with the system C compiler — no `CC=mpicc` or `-DCMAKE_C_COMPILER=mpicc` required. `find_package(MPI)` does the discovery, `MPI::MPI_C` carries everything. Users hint with `-DMPI_HOME=...` / `module load` for non-standard installs.

- [ ] **Delete** [core/cmake/SetupMPI.cmake](core/cmake/SetupMPI.cmake) — BLT-derived legacy, nothing modern needs it
- [ ] Replace `INCLUDE(SetupMPI)` at [CMakeLists.txt:149](CMakeLists.txt#L149) with `find_package(MPI REQUIRED COMPONENTS C)` directly. The `REQUIRED` keyword converts the existing soft-warn at lines 153-157 into a hard error (correct — ROSS does not build without MPI)
- [ ] Delete the entire `IF(MPI_C_FOUND) ... ELSE(MPI_C_FOUND) ... ENDIF` block at [CMakeLists.txt:150-157](CMakeLists.txt#L150-L157) (`INCLUDE_DIRECTORIES(${MPI_C_INCLUDE_PATH})` and `LIST(APPEND ROSS_EXTERNAL_LIBS ${MPI_C_LIBRARIES})`)
- [ ] Delete `TARGET_INCLUDE_DIRECTORIES(ROSS INTERFACE ${MPI_C_INCLUDE_PATH})` from `core/`
- [ ] Set `target_link_libraries(ross PUBLIC MPI::MPI_C)` — MPI must be `PUBLIC` because [core/ross.h:81](core/ross.h#L81) does `#include <mpi.h>` (confirmed, not a hedge)
- [ ] Add `target_link_libraries(ross PRIVATE m)` for libm
- [ ] **Update [CLAUDE.md](CLAUDE.md)** — Build section: drop `-DCMAKE_C_COMPILER=mpicc` from the manual configure example. Add a one-liner noting MPI is auto-discovered; users hint with `-DMPI_HOME=...` or `module load <mpi>` for non-standard installs. Do not direct users to set `CC=mpicc` anywhere
- [ ] **Regression-test the new configure UX**: run `env -u CC -u CXX cmake --preset ross-debug` on macOS (Homebrew MPICH) and on a Linux box with a typical MPI install — both must configure clean without anyone overriding the C compiler. If they don't, Step 4 isn't done

### Step 4b — Honor `BUILD_SHARED_LIBS` directly; retire `ROSS_BUILD_SHARED_LIBS`
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

### Step 5 — Generate proper package config files
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

### Step 6 — Install headers under `include/ross/`
- [ ] Replace catch-all header install with scoped `install(DIRECTORY ... DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ross ...)` (exclude `cmake`, `risa` — `risa` exclude is defensive since Step 0 already disabled the build path). **Verify the `PATTERN ... EXCLUDE` syntax actually skips the `risa/` and `cmake/` subdirectories** — `PATTERN` matches basenames; if it doesn't work as expected, fall back to `REGEX "/(cmake|risa)/" EXCLUDE` placed outside the `FILES_MATCHING` block
- [ ] Install generated `config.h` to `${CMAKE_INSTALL_INCLUDEDIR}/ross` **without renaming** — the `/ross/` subdir already isolates it from consumer collisions, and renaming would require edits to [core/ross-base.h](core/ross-base.h) and [core/ross-random.h](core/ross-random.h) which `#include "config.h"`. Earlier draft said to rename to `ross-config-build.h`; that's been dropped
- [ ] Verify `#include <ross.h>` still resolves via exported INCLUDES destination
- [ ] Verify installed `ross-base.h`'s `#include "config.h"` resolves (quoted-include search finds `config.h` in the same installed directory)
- [ ] **Document the Path B consumer contract** (resolved in pre-flight) in two places:
  - [ ] Add a "Consumer-facing API surface" subsection to [CLAUDE.md](CLAUDE.md) near the existing "Consumer compatibility (CODES)" section. Spell out: `<ross.h>` and any top-level `ross-*.h` are supported; subdirectory headers (`<instrumentation/...>`, `<check-revent/...>`, `<queue/...>`, `<rio/...>`) and internal headers (`<buddy.h>`, `<lz4.h>`, `<hash-quadratic.h>`) are not; build-tree exposure of those is an unsupported artifact slated for removal in [future-refactor-tasks.md](future-refactor-tasks.md)
  - [ ] Add the same contract as a comment block at the top of the new [core/cmake/rossConfig.cmake.in](core/cmake/rossConfig.cmake.in) (created in Step 5)

### Step 7 — Fix `ross.pc` pkg-config file
- [ ] Drop quotes around `prefix = "..."` in `core/ross.pc.in`
- [ ] Use `prefix=@CMAKE_INSTALL_PREFIX@`, `libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@`, `includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@/ross`
- [ ] Use `@PROJECT_VERSION@` for the `Version:` field
- [ ] **Update `Libs:` line to `-lross`** (was `-lROSS`) — matches the library filename rename from Step 2. One-character change at the source-template level; consumers using `pkg_check_modules` pick it up transparently on reconfigure
- [ ] Install `ross.pc` to `${CMAKE_INSTALL_LIBDIR}/pkgconfig`
- [ ] **Do NOT add MPI flags** to `Cflags`/`Libs`. Intent: pkg-config is back-compat only. Consumers compile with `mpicc`, so MPI is transparent. The CMake config (Step 5) is the supported path going forward; `ross.pc` stays minimal until it's deprecated
- [ ] **Verify CODES reconfigure picks up the new flag**: after installing the new ROSS, run `cmake --preset codes-debug` (or whatever CODES preset) in CODES and confirm `pkgcfg_lib_ROSS_ROSS` in its CMakeCache.txt now resolves to `libross.{a,so}`, not `libROSS.a`

### Phase 1 validation
- [ ] Build ROSS with current presets: `cmake --preset ... && cmake --build ... && cmake --install ...`
- [ ] Inspect installed `rossTargets.cmake` — confirm no absolute MPI paths in `INTERFACE_*` properties. Concrete check: `grep -rE "/opt/homebrew|/usr/local/Cellar|/usr/lib/x86_64-linux-gnu" <prefix>/lib/cmake/ross/` must return zero hits
- [ ] Build a tiny test consumer **from a non-sibling directory** (e.g., `/tmp/ross-consumer/`) with `find_package(ross REQUIRED)` + `target_link_libraries(t PRIVATE ross::ross)` — catches hidden `${PROJECT_SOURCE_DIR}` assumptions that pass when the consumer is adjacent to the source
- [ ] **Pre-merge CODES validation (gating)**: before merging Phase 1 to ROSS master, build CODES against the Phase 1 install. This is the manual stand-in for the future CODES contract test ([future-refactor-tasks.md](future-refactor-tasks.md) "CODES contract test"). Procedure:
  - [ ] Build ROSS from `cmake-improvements` branch, install to `install/debug`
  - [ ] In CODES: `rm -rf build/debug` (or pass `cmake --fresh`) — required to flush the cached `pkgcfg_lib_ROSS_ROSS:FILEPATH=...libROSS.a` entry; without this, the next configure won't pick up the renamed `libross.{a,so}` until the cache is cleared
  - [ ] Reconfigure CODES against the new ROSS install
  - [ ] Build CODES; run its test suite
  - [ ] Iterate on whichever side breaks (most likely a consumer-side fix in CODES — see plan.md "Rollback and cross-repo coordination" for the rollback ladder if it's not)
  - [ ] Only after CODES builds clean → merge ROSS Phase 1
- [ ] **Post-merge CODES update**: after ROSS Phase 1 lands on master, `cmake --fresh` (or delete the build dir) once in CODES to flush stale pkg-config cache. First reconfigure picks up `libross.{a,so}` + `<prefix>/include/ross/` automatically
- [ ] Tarball regression: delete `.git/` in a copy of the source, reconfigure, confirm Step 1's `0.0.0` fallback engages without error
- [ ] **MPI auto-discovery regression**: `env -u CC -u CXX cmake --preset ross-debug` must succeed on macOS and Linux without `-DCMAKE_C_COMPILER=mpicc`. This is the UX outcome of Step 4 and the most user-visible change of Phase 1

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
- Steps 1, 4, and 4b can each ship independently (Step 4b only touches the `add_library` site and CLAUDE.md).
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
