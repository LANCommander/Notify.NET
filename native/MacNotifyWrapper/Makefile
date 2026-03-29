# Makefile for MacNotifyWrapper
#
# Builds a thin Objective-C dylib wrapping UNUserNotificationCenter.
# Produces separate arm64 and x86_64 slices, plus an optional universal binary.
#
# Usage:
#   make             # build all slices
#   make install     # copy dylibs into ../../runtimes/
#   make universal   # also build universal binary
#   make clean

SDK     := $(shell xcrun --show-sdk-path)
CC      := clang
SRC     := MacNotifyWrapper.m
CFLAGS  := -fobjc-arc -fvisibility=hidden -O2 -Wall -Wextra \
           -isysroot $(SDK)
LDFLAGS := -dynamiclib \
           -framework Foundation \
           -framework UserNotifications \
           -install_name @rpath/libMacNotifyWrapper.dylib

ARM64_TARGET  := arm64-apple-macos11.0
X64_TARGET    := x86_64-apple-macos10.14

OUT_ARM64     := osx-arm64/libMacNotifyWrapper.dylib
OUT_X64       := osx-x64/libMacNotifyWrapper.dylib
OUT_UNIVERSAL := osx-universal/libMacNotifyWrapper.dylib

RUNTIME_ARM64 := ../../runtimes/osx-arm64/native
RUNTIME_X64   := ../../runtimes/osx-x64/native

.PHONY: all universal install clean

all: $(OUT_ARM64) $(OUT_X64)

universal: $(OUT_UNIVERSAL)

$(OUT_ARM64): $(SRC) MacNotifyWrapper.h
	mkdir -p osx-arm64
	$(CC) $(CFLAGS) -arch arm64 -target $(ARM64_TARGET) $(LDFLAGS) -o $@ $<

$(OUT_X64): $(SRC) MacNotifyWrapper.h
	mkdir -p osx-x64
	$(CC) $(CFLAGS) -arch x86_64 -target $(X64_TARGET) $(LDFLAGS) -o $@ $<

$(OUT_UNIVERSAL): $(OUT_ARM64) $(OUT_X64)
	mkdir -p osx-universal
	lipo -create -output $@ $(OUT_ARM64) $(OUT_X64)

install: $(OUT_ARM64) $(OUT_X64)
	mkdir -p $(RUNTIME_ARM64) $(RUNTIME_X64)
	cp $(OUT_ARM64) $(RUNTIME_ARM64)/
	cp $(OUT_X64)   $(RUNTIME_X64)/
	@echo "Installed to runtimes/"

clean:
	rm -rf osx-arm64 osx-x64 osx-universal
