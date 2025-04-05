# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

BUILD_DIR = build
DOCKER_IMAGE = flash

all: build

configure:
	meson setup $(BUILD_DIR)

build: configure
	meson compile -C $(BUILD_DIR)

rust:
	cargo build --target-dir $(BUILD_DIR) -F tracing

rust_release:
	cargo build --target-dir $(BUILD_DIR) --release

ci_build:
	./tools/ci_build.sh

docker:
	docker build -t "$(DOCKER_IMAGE):dev" .

docker_mon:
	docker build --target flash-build -t "$(DOCKER_IMAGE):mon" .

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all configure build rust ci_build docker docker_mon clean