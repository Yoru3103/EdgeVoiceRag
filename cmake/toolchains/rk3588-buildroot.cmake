# RK3588 Buildroot应用程序交叉编译配置。
#
# 可以通过环境变量覆盖SDK位置：
#
# export RK3588_BUILDROOT_HOST=/path/to/buildroot/output/.../host

if(
    NOT DEFINED RK3588_BUILDROOT_HOST
    OR RK3588_BUILDROOT_HOST STREQUAL ""
)
    if(
        DEFINED ENV{RK3588_BUILDROOT_HOST}
        AND NOT "$ENV{RK3588_BUILDROOT_HOST}" STREQUAL ""
    )
        set(
            RK3588_BUILDROOT_HOST
            "$ENV{RK3588_BUILDROOT_HOST}"
            CACHE PATH
            "RK3588 Buildroot host directory"
        )
    else()
        set(
            RK3588_BUILDROOT_HOST
            "/home/xx/rk3588_linux_sdk/buildroot/output/alientek_rk3588/host"
            CACHE PATH
            "RK3588 Buildroot host directory"
        )
    endif()
endif()

set(
    RK3588_BUILDROOT_TOOLCHAIN
    "${RK3588_BUILDROOT_HOST}/share/buildroot/toolchainfile.cmake"
)

if(NOT EXISTS "${RK3588_BUILDROOT_TOOLCHAIN}")
    message(
        FATAL_ERROR
        "RK3588 Buildroot toolchain file was not found: "
        "${RK3588_BUILDROOT_TOOLCHAIN}"
    )
endif()

include("${RK3588_BUILDROOT_TOOLCHAIN}")
