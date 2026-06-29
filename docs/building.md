# Building GIGGLE

GIGGLE builds with CMake and links against an existing ROOT install. The only
dependency you supply is ROOT. Everything else is either standard build tooling
you most likely already have, a component of your system, or a library vendored
and built from source.

## Prerequisites

What you supply:

| Requirement | Notes |
|---|---|
| [ROOT](https://root.cern) | The one external dependency, installed system-wide. It is not vendored, and the custom-formula feature uses its cling interpreter at runtime. `root-config` must be on your `PATH`, or pass `-DROOT_DIR=…`. |
| C++17 compiler | A recent Clang, GCC, or Apple Clang. |
| [CMake](https://cmake.org) ≥ 3.16 | The configure and build driver. |
| `git` | To clone the repository with its submodules. |

GIGGLE was developed and tested with **ROOT 6.36.04**. It does not insist on a
particular version, but other versions are untested. They may well work, and if
a build or a fit ever misbehaves, matching 6.36.04 is the first thing to try.

Already on your system, with nothing to install:

- **OpenGL**: a built-in framework on macOS, or the mesa/libGL development
  package on Linux, which is usually already present. The interface renders
  through it, using a 3.2 core profile. If a Linux build cannot find OpenGL,
  install the development package: on Debian or Ubuntu that is
  `sudo apt install libgl1-mesa-dev` (use your distribution's equivalent
  elsewhere).
- **An Objective-C toolchain** (macOS only), for the native file dialog. It ships
  with Apple Clang and Xcode.

Handled for you, vendored as submodules and built from source:

- GLFW, Dear ImGui (docking branch), ImPlot, nlohmann/json,
  nativefiledialog-extended, and stb.

ROOT should be built with the **same C++ standard** as GIGGLE (C++17). If it
isn't, CMake prints a warning telling you which line to change. It does not stop
the build, but a mismatch can cause hard-to-diagnose link or runtime errors. See
[C++ standard](#the-c-standard) below.

## Getting the source

Clone with submodules:

```sh
git clone --recurse-submodules <repo-url> giggle
cd giggle
```

If you already cloned without `--recurse-submodules`:

```sh
git submodule update --init --recursive
```

The build will fail at the `vendor/` step without the submodules. Dear ImGui and
ImPlot in particular are compiled from explicit source lists and have no upstream
CMake files.

## Configure and build

Run these from the top level of the cloned repository, the folder that contains
`CMakeLists.txt`:

```sh
cmake -S . -B build
cmake --build build
```

The first command creates the `build/` directory for you (you do not need to make
it yourself) and prepares the build; the second compiles GIGGLE. The first build
also compiles the vendored libraries, so it can take a few minutes; later builds
are much faster.

- ROOT is located automatically: if `ROOT_DIR` is not set, CMake runs
  `root-config --prefix`, adds the result to the search path, and then finds
  ROOT.
- The build is optimized (**Release**) by default, which is what you want for
  normal use. A debug build (`-DCMAKE_BUILD_TYPE=Debug`) is only useful for
  stepping through the source in a debugger; GIGGLE behaves the same either way.
- `compile_commands.json` is written into `build/` for editors and clang
  tooling.

To point at a specific ROOT install instead of `root-config`:

```sh
cmake -S . -B build -DROOT_DIR=/path/to/root
# or:  cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/root
```

### The C++ standard

The C++ standard is set to 17 at the top of the root `CMakeLists.txt`. If your
ROOT reports a different standard, the configure step warns:

```
ROOT was built with C++NN but this project is set to C++17. …change the
set(CMAKE_CXX_STANDARD …) line near the top of CMakeLists.txt …
```

Edit that one line to match your ROOT and reconfigure.

## Running

The executable is `build/src/giggle`.

```sh
# Open the bundled sample spectrum
./build/src/giggle docs/assets/sample.root

# Or just launch and open a file from the menu
./build/src/giggle
```

## Platforms

- **macOS** and **Linux** are the primary targets.
- **Windows** is supported via **WSL2** with WSLg (build and run it as a Linux
  application).

## Troubleshooting

- **`find_package(ROOT …)` fails.** Make sure `root-config` is on your `PATH`
  (e.g. `source /path/to/root/bin/thisroot.sh`), or pass `-DROOT_DIR=…`.
- **The vendor build can't find imgui/implot sources.** You're missing the
  submodules. Run `git submodule update --init --recursive`.
- **Link or runtime errors after a clean build.** Check the C++-standard warning
  from the configure step; ROOT and GIGGLE must agree.
- **The layout looks wrong, or you want to reset it.** Delete the `imgui.ini`
  next to the executable, or use **View → Reset layout** in the app.
