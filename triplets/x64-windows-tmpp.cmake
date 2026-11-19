# Custom triplet for TaskManagerPlusPlus.
#
# This machine has Visual Studio 2026 installed but NOT registered with the
# Visual Studio Installer (vswhere reports zero instances), so vcpkg's normal
# Visual Studio discovery fails with:
#   "Unable to find a valid Visual Studio instance"
# Pointing VCPKG_VISUAL_STUDIO_PATH at the installation directory bypasses the
# registry lookup entirely.
#
# The override is conditional on that installation existing, so the same triplet
# still works on a machine (or CI runner) with a registered Visual Studio 2022,
# where vcpkg's normal discovery succeeds and v145 does not exist.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

if(EXISTS "D:/Program Files/Microsoft Visual Studio/18/Community")
    set(VCPKG_VISUAL_STUDIO_PATH "D:/Program Files/Microsoft Visual Studio/18/Community")
    set(VCPKG_PLATFORM_TOOLSET v145)
endif()
