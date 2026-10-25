# Building GIGGLE

GIGGLE builds with CMake and links against an existing ROOT install. The only
dependency you supply is ROOT. Everything else is either standard build tooling
you most likely already have, a component of your system, or a library vendored
and built from source.

## Prerequisites

What you supply:

| Requirement | Notes |
|---|---|
| [ROOT](https://root.cern) | The one external dependency, installed system-wide. It is not vendored, and the custom-formula feature uses its cling interpreter at runtime. `root-config` must be on your `PATH`, or pass `-DROOT_DIR=…`. See [Where ROOT comes from](#where-root-comes-from). |
| C++17 compiler | A recent Clang, GCC, or Apple Clang. |
| [CMake](https://cmake.org) ≥ 3.16 | The configure and build driver. |
| `git` | To clone the repository with its submodules. |

GIGGLE was developed and tested with **ROOT 6.36.04**. It does not insist on a
particular version, and 6.38 and 6.40 are known to build and run too. If a
build or a fit ever misbehaves, matching 6.36.04 is the first thing to try.

Handled for you, vendored as submodules and built from source:

- GLFW, Dear ImGui (docking branch), ImPlot, nlohmann/json,
  nativefiledialog-extended, and stb.

### System packages

**macOS.** Nothing to install beyond Xcode's command line tools
(`xcode-select --install`). They provide the compiler, the OpenGL framework,
and the Objective-C toolchain the native file dialog uses. Get CMake and git
from Homebrew or their own installers if you don't have them.

**Linux.** The vendored libraries are built from source, and they need a
handful of development packages: OpenGL, the Wayland and X11 headers that GLFW
builds its window support against, and GTK 3 for the open/save dialogs. Without
them the configure step stops inside `vendor/` (the exact messages are listed
under [Troubleshooting](#troubleshooting)).

Debian or Ubuntu, including WSL2:

```sh
sudo apt install build-essential cmake git pkg-config libgl1-mesa-dev \
    libwayland-dev libxkbcommon-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libgtk-3-dev
```

Fedora:

```sh
sudo dnf install gcc-c++ cmake git pkgconf-pkg-config mesa-libGL-devel \
    wayland-devel libxkbcommon-devel libXrandr-devel libXinerama-devel \
    libXcursor-devel libXi-devel gtk3-devel
```

Other distributions: the same packages under their local names, that is the
development variants of mesa GL, wayland, xkbcommon, Xrandr, Xinerama, Xcursor,
Xi, and gtk3, plus pkg-config.

### Where ROOT comes from

Any ROOT with `root-config` on your `PATH` should work. A few sources come with
a detail worth knowing:

- **Built from source, or the official binary package on macOS.** Works as is.
- **Official binary tarballs on Linux.** They are linked against your
  distribution's packages, so install ROOT's own dependency list first
  ([root.cern/install/dependencies](https://root.cern/install/dependencies/)).
  On Ubuntu 23.10 and newer, `libvdt-dev` in particular is required, or CMake
  stops with `Could NOT find Vdt`.
- **Homebrew (`brew install root`) and conda-forge.** Both build ROOT with
  C++20 rather than C++17. You will see a warning at configure time. It is
  harmless; see [The C++ standard](#the-c-standard).
- **conda-forge ROOT on macOS, version 6.40 or newer.** conda's libc++ refuses
  to compile some C++20 headers unless you add
  `-DCMAKE_CXX_FLAGS=-D_LIBCPP_DISABLE_AVAILABILITY` to the configure command.
- **conda-forge ROOT on Linux.** Not supported. An activated conda environment
  replaces the compiler and shadows the system GTK and X11 libraries the
  interface needs; the build either fails or produces a binary that crashes on
  start. Use a ROOT built for your system instead: the official binary tarball,
  your distribution's package (Fedora and Arch ship one), or a build from
  source.

## Getting the source

Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/KhangPham0/GIGGLE.git giggle
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
ROOT was built with C++NN but this project is set to C++17. …
```

This is normally harmless. ROOT's CMake targets carry their own standard, so the
one library that includes ROOT headers is compiled as C++NN automatically while
the rest of GIGGLE stays at C++17. Homebrew and conda-forge ROOT, both built
with C++20, build and run this way. Only if the build or the application
actually misbehaves, edit that one line to match your ROOT and reconfigure.

## Running

The executable is `build/src/giggle`.

```sh
# Open the bundled sample spectrum
./build/src/giggle docs/assets/sample.root

# Or just launch and open a file from the menu
./build/src/giggle
```

## Platforms

- **macOS** (Apple Silicon and Intel) and **Linux** are the primary targets.
- **Windows** is supported via **WSL2** with WSLg, which needs Windows 11 or
  Windows 10 build 19044 or newer. Install Ubuntu with `wsl --install`, then
  follow the Linux instructions above inside it; the window appears on the
  Windows desktop. Under WSLg GIGGLE runs on Wayland, and GLFW prints two
  messages about the window position not being supported. They are harmless.

## Troubleshooting

- **`find_package(ROOT …)` fails.** Make sure `root-config` is on your `PATH`
  (e.g. `source /path/to/root/bin/thisroot.sh`), or pass `-DROOT_DIR=…`.
- **`Could NOT find Vdt`.** Your ROOT binary was built against the
  distribution's vdt package. Install it (`sudo apt install libvdt-dev` on
  Debian/Ubuntu) and reconfigure.
- **`Failed to find wayland-scanner`, `pkg-config tool not found`, or
  `The following required packages were not found: gtk+-3.0`.** Linux system
  packages are missing. Install the list under
  [System packages](#system-packages) and reconfigure.
- **The vendor build can't find imgui/implot sources.** You're missing the
  submodules. Run `git submodule update --init --recursive`.
- **`error: expected ')'` inside `__charconv/from_chars_floating_point.h`.**
  conda-forge ROOT 6.40 or newer on macOS. Add
  `-DCMAKE_CXX_FLAGS=-D_LIBCPP_DISABLE_AVAILABILITY` to the configure command.
- **Link or runtime errors after a clean build.** Check the C++-standard
  warning from the configure step and try matching the standard to your ROOT.
- **`Wayland: The platform does not support setting the window position`.**
  Printed by GLFW on Wayland desktops (WSLg, GNOME, KDE). Harmless.
- **The layout looks wrong, or you want to reset it.** Delete the `imgui.ini`
  next to the executable, or use **View → Reset layout** in the app.
