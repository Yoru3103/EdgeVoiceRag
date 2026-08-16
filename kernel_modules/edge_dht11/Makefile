ifneq ($(KERNELRELEASE),)

# obj：要编译的目标； m：以模块形式编译
obj-m += edge_dht11.o

else

# 目标内核的源码和构建目录
KERNEL_DIR ?= /home/xx/rk3588_linux_sdk/kernel-6.1

# 告诉内核Kbuild目标架构
ARCH ?= arm64

# 交叉编译工具链前端
CROSS_COMPILE ?= /home/xx/rk3588_linux_sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

# :=表示立即展开，读到这一行就执行一次并保存结果
MODULE_DIR := $(shell pwd)

.PHONY: all clean

# Makeflile中第一个普通目标通常是默认目标，执行make等价于make all
all:
	$(MAKE) -C $(KERNEL_DIR) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(MODULE_DIR) \
		modules
# $(MAKE)是 Make 提供的递归调用方式。它会把父级make的一些参数传递给子级
# -C表示在执行内核构建前切换目录
# -M 用来构建外部模块，告诉内核内核源码在KERNEL_DIR，但这次需要编译的外部模块位于MODULE_DIR

clean:
	$(MAKE) -C $(KERNEL_DIR) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(MODULE_DIR) \
		clean

endif
