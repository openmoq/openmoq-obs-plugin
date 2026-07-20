# OBS MOQ Plugin

## Introduction

This is an OBS Studio plugin that streams video over [MOQ](https://datatracker.ietf.org/wg/moq/about/), using [openmoq/moq5](https://github.com/openmoq/moq5) as the underlying protocol implementation.

**This project is under active development.** It may contain bugs, and several features are still in progress or not yet implemented. See the [Roadmap](#roadmap) section below for what's currently missing.

## Dependencies

* [libmoq](https://github.com/openmoq/moq5) (openmoq/moq5), MOQ protocol implementation, built with the `service` component. Requires [this fork/PR](https://github.com/openmoq/moq5/pull/1) (see [Required libmoq fork](#required-libmoq-fork) below).
* [OBS Studio fork with dynamic service registration](https://github.com/obsproject/obs-studio/pull/12911), required to use this plugin (see [Required OBS fork](#required-obs-fork) below).
* CMake 3.28+
* A C++ compiler with C++17 support (GCC 13+, Clang, or MSVC)

### Required OBS fork

To use this plugin, OBS Studio needs support for dynamically detecting services registered by plugins. That support is currently in review as [obs-studio PR #12911](https://github.com/obsproject/obs-studio/pull/12911), and is being considered for inclusion in the **v33.0** release (not guaranteed).

Until that PR is merged, you'll need to build OBS Studio from that PR's branch/fork to be able to select and configure this plugin's service from the OBS UI.

### Required libmoq fork

This plugin currently requires a fork of libmoq (openmoq/moq5), available as [PR #1](https://github.com/openmoq/moq5/pull/1). Make sure to check out that branch/fork before building libmoq below, the plugin will not work against upstream `moq5` as-is.

### Building libmoq (openmoq/moq5)

This plugin links against libmoq (openmoq/moq5) (`find_package(libmoq REQUIRED COMPONENTS service)`). libmoq must be built and installed on the system **before** configuring this plugin.

#### 1. Build and install libmoq

libmoq's picoquic-backed adapters (`MOQ_BUILD_PQ_THREADED`, used by the service tier here) need picoquic's private `picoquic_internal.h`, which picoquic does not install. Because of that, libmoq must be built in "source-tree mode": pointed at a picoquic source checkout plus a built picotls, rather than at any picoquic already installed on the system.

```bash
# Fetch pinned picoquic + picotls sources and build picotls.
# Prints (and writes to .deps/picoquic-ci/picoquic_deps.env) the two paths
# needed below.
scripts/setup_picoquic_deps.sh

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMOQ_BUILD_SERVICE=ON \
  -DMOQ_BUILD_MSF=ON \
  -DMOQ_BUILD_MEDIA_OBJECT=ON \
  -DMOQ_BUILD_ADAPTER_PICOQUIC=ON \
  -DMOQ_BUILD_PQ_THREADED=ON \
  -DMOQ_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DMOQ_PICOQUIC_SOURCE_DIR="$(pwd)/.deps/picoquic-ci/picoquic" \
  -DMOQ_PICOTLS_PREFIX="$(pwd)/.deps/picoquic-ci/picotls/build"

cmake --build build -j"$(nproc)"
cmake --install build
```

#### 2. Configure this plugin against libmoq

Because libmoq was built in source-tree mode, anything that consumes it via `find_package(libmoq)`, including this plugin, re-triggers the same picoquic resolution (`libmoqConfig.cmake` calls `find_dependency(Picoquic)`). So this plugin's own configure needs the same two picoquic variables, **and** needs OBS's default warnings-as-errors disabled.

```bash
cmake -S . -B build \
  -DMOQ_PICOQUIC_SOURCE_DIR=</path/to/moq5/>.deps/picoquic-ci/picoquic \
  -DMOQ_PICOTLS_PREFIX=</path/to/moq5/>.deps/picoquic-ci/picotls/build \
  -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF

cmake --build build -j"$(nproc)"
```

#### Installing the built plugin

Copy the resulting shared object into your OBS install's plugin directory, e.g.:

```bash
cp build/obs-moq.so </path/to/obs-install>/lib/x86_64-linux-gnu/obs-plugins/
```

## Usage

1. Select the MOQ service in OBS's stream settings.
2. In the **Server** field, enter the URL of the MOQ relay you want to publish to.
3. In the **Stream Key** field, enter the MOQ namespace, with each namespace tuple part separated by a dash (`-`). For example, a namespace of `["live", "user123"]` would be entered as `live-user123`.

## Tested against

This plugin has been, and continues to be, tested using:

* [moqx](https://github.com/openmoq/moqx)
* [moq-playa](https://github.com/openmoq/moq-playa)

## Roadmap

The plugin currently only supports **H.264 video**. Planned next steps:

* Add support for audio tracks (AAC, Opus, AC-3)
* Add support for other video/audio codecs (HEVC, AV1)
* Add CMAF support
* Achieve a lower latency target

