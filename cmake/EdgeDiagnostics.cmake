# Diagnostics are usage requirements of project code, not global SDK flags.
add_library(edge_diagnostics INTERFACE)

if(NOT EDGE_ENABLE_ASAN AND NOT EDGE_ENABLE_UBSAN)
    return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
   OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR
        "Edge diagnostics currently require a Linux/WSL GCC or Clang toolchain."
    )
endif()

# Probe both compilation and runtime-library linkage, without running target code.
function(edge_check_sanitizer sanitizer result)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=${sanitizer}")
    list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=${sanitizer}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    check_cxx_source_compiles("int main() { return 0; }" ${result})
    if(NOT ${result})
        message(FATAL_ERROR
            "The compiler/runtime cannot link -fsanitize=${sanitizer}. "
            "Check the selected toolchain and sanitizer runtime libraries."
        )
    endif()
endfunction()

set(edge_sanitizer_flags)
if(EDGE_ENABLE_ASAN)
    edge_check_sanitizer(address EDGE_ASAN_SUPPORTED)
    list(APPEND edge_sanitizer_flags -fsanitize=address)
endif()
if(EDGE_ENABLE_UBSAN)
    edge_check_sanitizer(undefined EDGE_UBSAN_SUPPORTED)
    list(APPEND edge_sanitizer_flags -fsanitize=undefined)
    # Undefined behavior must fail a test, rather than only print a warning.
    target_compile_options(edge_diagnostics INTERFACE
        -fno-sanitize-recover=undefined
    )
endif()

target_compile_options(edge_diagnostics INTERFACE
    ${edge_sanitizer_flags}
    -fno-omit-frame-pointer
)
target_link_options(edge_diagnostics INTERFACE ${edge_sanitizer_flags})
message(STATUS "Edge diagnostics: ${edge_sanitizer_flags}")
