# Linux builds use BlueZ; Windows builds receive battery advertisements.

CXX ?= g++
BUILD_DATE := $(shell date +"%Y-%m-%d %H:%M:%S")
GIT_HASH ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")

DEBUG ?= 0
PREFIX ?= /usr/local
DESTDIR ?=

TARGET_NAME = airpods
SRC_DIR = src
BUILD_BASE = build

ifeq ($(DEBUG), 1)
    BUILD_DIR = $(BUILD_BASE)/debug
else
    BUILD_DIR = $(BUILD_BASE)/release
endif

OBJ_DIR = $(BUILD_DIR)/obj
TARGET = $(BUILD_DIR)/$(TARGET_NAME)

SOURCES = $(SRC_DIR)/main.cpp
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEPS = $(OBJECTS:.o=.d)

WARNINGS = -Wall -Wextra -Wsuggest-override -Wnon-virtual-dtor -Wshadow

# BlueZ contributes headers only: the bdaddr parsing is hand-rolled so nothing
# links against libbluetooth. sd-bus is what reaches BlueZ's device list.
PKGS = libsystemd Qt6Widgets Qt6Svg libcrypto
# -isystem, not -I: keeps Qt's own header warnings out of our build output.
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS) 2>/dev/null | sed 's/-I/-isystem /g')
PKG_LIBS := $(shell pkg-config --libs $(PKGS) 2>/dev/null)

CXXFLAGS = -std=c++20 -O2 -fPIC $(WARNINGS) -MMD -MP -I$(SRC_DIR) $(PKG_CFLAGS) \
	-DBUILD_DATE="\"$(BUILD_DATE)\"" -DGIT_HASH="\"$(GIT_HASH)\""
LIBS = $(PKG_LIBS)

ifeq ($(DEBUG), 1)
    CXXFLAGS += -g -DDEBUG
endif

# Container build: the toolchain and headers live in the image, never on the host.
IMAGE ?= airpods-build
DOCKER ?= docker
# GIT_HASH is resolved on the host and passed in: the hash describes the
# checkout, not the image, and git is deliberately absent from the container.
# The recipe's GIT_HASH ?= keeps an environment value, so this wins inside.
# dbus-daemon refuses to serve a uid it cannot name, and the image has no entry
# for the host user these run as, which the discovery test's private bus needs.
# One generated line answers it and keeps the host's own passwd out of here.
CONTAINER_PASSWD = $(BUILD_BASE)/container-passwd

DOCKER_RUN = $(DOCKER) run --rm -u $(shell id -u):$(shell id -g) \
	-e GIT_HASH="$(GIT_HASH)" -e TEST_DIR="$(CONTAINER_TEST_DIR)" \
	-e COVERAGE_DIR="$(CONTAINER_COVERAGE_DIR)" \
	-v "$(CURDIR)/$(CONTAINER_PASSWD)":/etc/passwd:ro \
	-v "$(CURDIR)":/work -w /work $(IMAGE)

.PHONY: all build clean run help deps format format-check lint install uninstall \
	image container-build container-lint container-format-check package

FORMAT_SOURCES := $(shell find $(SRC_DIR) tests -name '*.cpp' -o -name '*.hpp')

# Recursive submake so the clean finishes before any directory is recreated.
all:
	$(MAKE) clean
	$(MAKE) build

build: $(TARGET)

$(BUILD_DIR) $(OBJ_DIR):
	mkdir -p $@

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	echo Linking $@...
	$(CXX) $(OBJECTS) -o $@ $(LIBS)
	echo Build complete: $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	echo Compiling $<...
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	echo Cleaning build files...
	rm -rf "$(BUILD_BASE)" *.log

run: build
	echo Running $(TARGET)...
	$(TARGET)

# --------------------------------------------------------------- container ---
image:
	$(DOCKER) build -t $(IMAGE) .

$(CONTAINER_PASSWD):
	mkdir -p "$(BUILD_BASE)"
	printf 'root:x:0:0::/root:/bin/sh\nbuilder:x:%s:%s::/tmp:/bin/sh\n' \
		"$(shell id -u)" "$(shell id -g)" > "$@"

container-build: image $(CONTAINER_PASSWD)
	$(DOCKER_RUN) make build

container-lint: image $(CONTAINER_PASSWD)
	$(DOCKER_RUN) make lint

container-format-check: image $(CONTAINER_PASSWD)
	$(DOCKER_RUN) make format-check

# ------------------------------------------------------------------ checks ---
deps:
	@pkg-config --exists libsystemd || \
		echo "MISSING libsystemd  (Arch: systemd-libs, Debian: libsystemd-dev, Fedora: systemd-devel)"
	@pkg-config --exists Qt6Widgets || \
		echo "MISSING Qt6Widgets  (Arch: qt6-base, Debian: qt6-base-dev, Fedora: qt6-qtbase-devel)"
	@pkg-config --exists Qt6Svg || \
		echo "MISSING Qt6Svg      (Arch: qt6-svg, Debian: libqt6svg6-dev, Fedora: qt6-qtsvg-devel)"
	@pkg-config --exists libcrypto || \
		echo "MISSING libcrypto   (Arch: openssl, Debian: libssl-dev, Fedora: openssl-devel)"
	@test -f /usr/include/bluetooth/l2cap.h || \
		echo "MISSING bluez headers  (Arch: bluez-libs, Debian: libbluetooth-dev, Fedora: bluez-libs-devel)"
	@pkg-config --exists libsystemd && pkg-config --exists Qt6Widgets && \
		pkg-config --exists Qt6Svg && pkg-config --exists libcrypto && \
		test -f /usr/include/bluetooth/l2cap.h && \
		echo "All build dependencies present." || true

format:
	@command -v clang-format >/dev/null || { echo "clang-format not found"; exit 1; }
	clang-format -i $(FORMAT_SOURCES)

format-check:
	@command -v clang-format >/dev/null || { echo "clang-format not found"; exit 1; }
	clang-format --dry-run --Werror $(FORMAT_SOURCES)

# Static analysis. Checks and exclusions are configured in .clang-tidy.
#
# Headers are linted as standalone TUs: misc-include-cleaner only diagnoses the
# main file, so a header-only project gets no include checking otherwise.
LINT_HEADERS := $(shell find $(SRC_DIR) -name '*.hpp' ! -name '*_windows.hpp' | sort)
# The tests go through the compile database instead of this flag list: each one
# includes moc output that exists only under $(TEST_DIR), which is why linting
# them means building them first.
LINT_TESTS := $(shell find tests -name '*.cpp' -o -name '*.hpp' | sort)

lint: test-build
	@command -v clang-tidy >/dev/null || { echo "clang-tidy not found (Arch: clang)"; exit 1; }
	@printf '%s\n' $(SOURCES) $(LINT_HEADERS) | xargs -P $$(nproc) -I{} \
		clang-tidy --quiet --warnings-as-errors='*' {} -- \
		-xc++ -Wno-pragma-once-outside-header $(CXXFLAGS)
	@printf '%s\n' $(LINT_TESTS) | xargs -P $$(nproc) -I{} \
		clang-tidy --quiet --warnings-as-errors='*' -p "$(TEST_DIR)" {}
	@echo "clang-tidy clean ($(words $(SOURCES) $(LINT_HEADERS) $(LINT_TESTS)) files)"

# ---------------------------------------------------------------- install ---
install: build
	install -Dm755 "$(TARGET)" "$(DESTDIR)$(PREFIX)/bin/$(TARGET_NAME)"
	echo "Installed to $(DESTDIR)$(PREFIX)/bin/$(TARGET_NAME)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(TARGET_NAME)"
	echo "Uninstalled from $(DESTDIR)$(PREFIX)"

# makepkg rewrites pkgver= in whatever PKGBUILD it runs, so it runs on a copy
# and the tracked file keeps its placeholder. The PKGBUILD clones this
# repository by absolute path, so it packages committed HEAD from anywhere.
PACKAGE_DIR = $(BUILD_BASE)/package

package:
	mkdir -p "$(PACKAGE_DIR)"
	cp packaging/arch/PKGBUILD "$(PACKAGE_DIR)/"
	cd "$(PACKAGE_DIR)" && makepkg -fsi $(MAKEPKG_FLAGS)

help:
	@echo airpods - Build System
	@echo
	@echo Targets:
	@echo "  all                   - Clean and build (default)"
	@echo "  build                 - Build without cleaning"
	@echo "  clean                 - Remove build artifacts"
	@echo "  run                   - Build and run"
	@echo "  deps                  - Check build dependencies"
	@echo "  format                - Rewrite sources with clang-format"
	@echo "  format-check          - Fail if any source is not clang-format clean"
	@echo "  lint                  - Run clang-tidy over src and tests"
	@echo "  image                 - Build the build container image"
	@echo "  container-build       - Build inside the container"
	@echo "  container-lint        - Run clang-tidy inside the container"
	@echo "  container-format-check- Run clang-format check inside the container"
	@echo "  test                  - Configure with CMake and run the CTest suite"
	@echo "  container-test        - Run the CTest suite inside the container"
	@echo "  coverage              - Run the suite and report covered lines per file"
	@echo "  container-coverage    - Report covered lines inside the container"
	@echo "  install               - Install under PREFIX (default /usr/local)"
	@echo "  uninstall             - Remove installed files"
	@echo "  package               - Build and install the Arch package from committed HEAD"
	@echo "  help                  - Show this help"
	@echo
	@echo Options:
	@echo "  DEBUG=1               - Build with debug symbols"
	@echo "  PREFIX=/usr           - Install prefix"
	@echo
	@echo Examples:
	@echo "  make container-build"
	@echo "  ./build/release/airpods --gestures"

WINDOWS_IMAGE ?= airpods-build-windows
.PHONY: windows-image container-build-windows
windows-image:
	$(DOCKER) build -t $(WINDOWS_IMAGE) -f Dockerfile.windows .

container-build-windows: windows-image
	$(DOCKER) run --rm -u $(shell id -u):$(shell id -g) -e GIT_HASH="$(GIT_HASH)" \
		-v "$(CURDIR)":/work -w /work $(WINDOWS_IMAGE) \
		cmake -S . -B build/windows -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=/opt/qt6-win-static/lib/cmake/Qt6/qt.toolchain.cmake \
		-DQT_HOST_PATH=/usr -DCMAKE_BUILD_TYPE=Release \
		-DCPPWINRT_INCLUDE_DIR=/opt/cppwinrt -DOPENSSL_USE_STATIC_LIBS=TRUE
	$(DOCKER) run --rm -u $(shell id -u):$(shell id -g) \
		-v "$(CURDIR)":/work -w /work $(WINDOWS_IMAGE) \
		cmake --build build/windows --parallel 4

# ------------------------------------------------------------------- tests ---
# CMake rather than this Makefile: QTest needs moc, and CMAKE_AUTOMOC plus
# CTest is the whole of that rule.
CMAKE ?= cmake
CTEST ?= ctest
# Host and container need separate directories: a CMake cache records the
# absolute path it was configured at, and the container's is /work.
TEST_DIR ?= $(BUILD_BASE)/test
CONTAINER_TEST_DIR = $(BUILD_BASE)/test-container
COVERAGE_DIR ?= $(BUILD_BASE)/coverage
CONTAINER_COVERAGE_DIR = $(BUILD_BASE)/coverage-container

# Qt6 adds this for itself, in the spelling GCC accepts. clangd and clang-tidy
# are clang and reject it, so the database they read loses it. The build keeps
# it: only the tooling copy is trimmed.
GCC_ONLY_FLAG = -mno-direct-extern-access

.PHONY: test test-build container-test
test-build:
	$(CMAKE) -S . -B "$(TEST_DIR)" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	$(CMAKE) --build "$(TEST_DIR)" --parallel
	sed -i 's/ $(GCC_ONLY_FLAG)//g' "$(TEST_DIR)/compile_commands.json"

test: test-build
	$(CTEST) --test-dir "$(TEST_DIR)" --output-on-failure

# Which lines the suite reaches, per file. Read the columns, not the total: most
# of src/ui is Qt painting and most of src/bt is socket work, neither of which a
# unit test was ever going to enter, so a single percentage across all of it
# measures the shape of the program rather than the tests.
#
# A separate directory from $(TEST_DIR): --coverage and -O0 are not what anyone
# wants to run, and the counters would be mixed with the optimised build's.
.PHONY: coverage container-coverage
coverage:
	$(CMAKE) -S . -B "$(COVERAGE_DIR)" -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS="--coverage -O0 -g" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
	$(CMAKE) --build "$(COVERAGE_DIR)" --parallel
	$(CTEST) --test-dir "$(COVERAGE_DIR)" --output-on-failure
	@command -v gcovr >/dev/null || { echo "gcovr not found (Arch: gcovr)"; exit 1; }
	gcovr --root . --filter 'src/' --exclude '.*_windows\.hpp' \
		--sort uncovered-percent --txt --print-summary

container-coverage: image $(CONTAINER_PASSWD)
	$(DOCKER_RUN) make coverage

# The rewrite is what makes the database usable outside the container: /work is
# this directory bind mounted, so the paths name the same files, generated moc
# output included. clangd reads it in preference to compile_flags.txt, so no
# flag list has to be kept by hand.
container-test: image $(CONTAINER_PASSWD)
	$(DOCKER_RUN) make test
	sed 's|/work|$(CURDIR)|g' "$(CONTAINER_TEST_DIR)/compile_commands.json" > compile_commands.json

.PHONY: wine-smoke
wine-smoke:
	python3 tests/wine_smoke.py build/windows/airpods.exe
