# Minimal header-only Pigweed compatibility subset vendored by this project.
add_library(pigweed INTERFACE)
add_library(Pigweed::pigweed ALIAS pigweed)

target_include_directories(pigweed INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_features(pigweed INTERFACE cxx_std_20)
