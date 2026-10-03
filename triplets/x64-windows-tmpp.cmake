# Custom triplet for TaskManagerPlusPlus.
#
# This machine has Visual Studio 2026 installed but NOT registered with the
# Visual Studio Installer (vswhere reports zero instances), so vcpkg's normal
# Visual Studio discovery fails with:
#   "Unable to find a valid Visual Studio instance"
# Pointing VCPKG_VISUAL_STUDIO_PATH at the installation directory bypasses the
# registry lookup entirely.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_VISUAL_STUDIO_PATH "D:/Program Files/Microsoft Visual Studio/18/Community")
set(VCPKG_PLATFORM_TOOLSET v145)
