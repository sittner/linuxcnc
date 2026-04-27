cmake_minimum_required(VERSION 3.10)
project(halscope LANGUAGES CXX)

set(BINARY_NAME "halscope")
set(APPLICATION_ID "org.linuxcnc.halscope")

# Install into the build output directory (not /usr/local).
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/bundle" CACHE PATH "" FORCE)
endif()

cmake_policy(SET CMP0063 NEW)

set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib/flutter")

# Set FLUTTER_MANAGED_DIR before add_subdirectory so it points to the right place.
set(FLUTTER_MANAGED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/flutter")

# Find GTK (needed for target_link_libraries below).
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)

# Pull in Flutter engine and helper functions (defines apply_standard_settings).
add_subdirectory("flutter")

# Configure build options from the parent project.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
endif()

# Define the application target. To change its name, update BINARY_NAME above,
# not the value here, or `flutter run` will no longer work.
add_executable(${BINARY_NAME}
  "main.cc"
  "my_application.cc"
  "${FLUTTER_MANAGED_DIR}/generated_plugin_registrant.cc"
)

# Apply the standard set of build settings. These can be removed for apps
# that need different build settings.
apply_standard_settings(${BINARY_NAME})

# Add dependency libraries. Add any application-specific dependencies here.
target_link_libraries(${BINARY_NAME} PRIVATE flutter)
target_link_libraries(${BINARY_NAME} PRIVATE PkgConfig::GTK)

target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")

# Generated plugin build rules, which manage building and bundling native code
# (C/C++) for plugins.
include(flutter/generated_plugins.cmake)

# === Bundle === #
# The Makefile copies the engine + icudtl.dat into the shared lib/flutter/ dir.
# Here we only install per-app files into the bundle directory.

# Install AOT compiled Dart code.
set(AOT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../build/lib/libapp.so")
install(FILES "${AOT_LIB}"
  DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
  COMPONENT Runtime)

# Install flutter_assets.
set(FLUTTER_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/flutter_assets")
install(DIRECTORY "${FLUTTER_ASSETS_DIR}"
  DESTINATION "${CMAKE_INSTALL_PREFIX}/data"
  COMPONENT Runtime)

# Install ICU data (also in data/ for the engine to find it).
install(FILES "${FLUTTER_ICU_DATA_FILE}"
  DESTINATION "${CMAKE_INSTALL_PREFIX}/data"
  COMPONENT Runtime)

# Install bundled plugin libraries.
foreach(bundled_library ${PLUGIN_BUNDLED_LIBRARIES})
  install(FILES "${bundled_library}"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
    COMPONENT Runtime)
endforeach(bundled_library)

# Run the Flutter tool portions of the build. This must not be removed.
add_dependencies(${BINARY_NAME} flutter_assemble)

# Only the install size is googled.
install(TARGETS ${BINARY_NAME} RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}"
  COMPONENT Runtime)
