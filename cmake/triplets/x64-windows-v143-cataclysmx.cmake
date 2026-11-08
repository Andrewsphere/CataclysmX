# Match win-vs2022's project toolset, including IDE-triggered regeneration that
# does not inherit the configure preset's environment.
include("${CMAKE_CURRENT_LIST_DIR}/x64-windows-cataclysmx.cmake")
list(APPEND VCPKG_HASH_ADDITIONAL_FILES
    "${CMAKE_CURRENT_LIST_DIR}/x64-windows-cataclysmx.cmake")
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)
