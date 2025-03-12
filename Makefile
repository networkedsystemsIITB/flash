# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

BUILD_DIR = build

all: build

configure:
	meson setup $(BUILD_DIR)

build: configure
	meson compile -C $(BUILD_DIR)

ci_build:
	./tools/ci_build.sh

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all configure build ci_build clean