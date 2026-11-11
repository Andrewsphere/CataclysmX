# Sanitizer follow-ups (2026-11-11)

Full SDL-enabled CTest runs: **ASan/UBSan 604/605 passed; TSan 602/605 passed;
no skips**. Findings remain unfixed and unsuppressed.

## Build and reproduction

- Linux x86_64, Ubuntu 22.04/kernel 6.8; GCC 11.4, C++23, Debug, Ninja,
  CMake 4.4.5. Built all project targets, including SDL game and tests.
- Used the pinned vcpkg manifest: SDL 3.4.20, SDL_image 3.4.8, SDL_ttf 3.2.2,
  shaderc 2026.2. Reused static, **uninstrumented** dependencies via
  `VCPKG_INSTALLED_DIR=<existing-install>` and `VCPKG_MANIFEST_INSTALL=OFF`;
  project code was instrumented and engine exceptions disabled.
- ASan: configure `linux-gcc-sdl` into a separate build directory with
  `CATACLYSMX_ENABLE_ASAN_UBSAN=ON`; run with
  `ASAN_OPTIONS=halt_on_error=1:detect_leaks=1` and
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
- TSan: use `linux-gcc-sdl-tsan`; link against GCC 12's `libtsan.so.2`
  (`CMAKE_EXE_LINKER_FLAGS=-L<runtime-dir>`, containing its `libtsan.so` link).
  GCC 11's runtime lacks `pthread_cond_clockwait` interception and produced
  extra double-lock warnings that disappeared with the newer runtime.
  Run with `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`.
- Hydrate Git LFS assets; provide desktop/Vulkan access. Run
  `setarch x86_64 -R ctest --test-dir <build> --output-on-failure` to avoid
  sanitizer startup mapping failures. ASan ran serially; TSan used `--parallel 4`.

## Failures to fix or classify

- **ASan — `SDL asset loading reports failures and uses bundled default`:**
  malformed-PNG handling leaks 176 bytes in three SDL error/TLS allocations on
  the `SdlAssets` worker. Investigate `SDL_CleanupTLS()` before its external
  `std::jthread` exits.
- **TSan — `SDL bundled lightbulb artwork animates all three content items`;
  `source loader contains paths and reports corrupt or orphaned assets`:**
  system D-Bus lock-order inversion during SDL initialization/teardown.
- **TSan — `sdl_frontend_smoke`:** SDL rwlock race report between the asset
  worker (`SdlAssets::Prepare`) and main-thread allocation through `DrawFrame`.
  Investigate lazy initialization/publication. Both TSan findings need checking
  against instrumented dependencies before treating them as application defects.
