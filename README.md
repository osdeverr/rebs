<img src="logo_readme.png" width="300">

[![build badge](https://github.com/osdeverr/rebs/actions/workflows/build.yml/badge.svg)](https://github.com/osdeverr/rebs/actions?query=workflow%3Abuild)

# Re - a modern build system for the future

- **[Download Re's latest release (Windows, Linux)](https://github.com/osdeverr/rebs/releases/latest)**
- **[Examples](/examples)**
- **[Wiki](https://github.com/osdeverr/rebs/wiki)**

**Re** is a project aiming to create what C++ hasn't ever had - a lightweight, easy-to-use build system that lets you focus on writing code and not on writing build scripts. **Check out our [examples folder](/examples) to see it for yourself!**

Some of Re's notable features are:

- **Declarative format:** with Re, you *don't* write build logic. The entire build process is governed by simple, declarative YAML configs. This simplifies the project structure a lot, even if it sacrifices some use cases and in some cases forces you to restructure your project a bit.
- **Trivial setup:** the simplest Re project is literally [just two lines of configuration](/examples/hello-world/re.yml).
- **CMake support:** Re supports loading (most) CMake targets as its own, allowing you to depend on any CMake libraries as if they were using Re. *(You can run `re` inside of a CMake project directory too!)*
- **Easy dependencies:** Re provides a unified interface for dependencies in the config file and supports Git, GitHub and [vcpkg](https://github.com/microsoft/vcpkg) dependencies out of the box. [See for yourself how easy it is!](/examples/easy-dependencies)
- **Out-of-the-box CI:** Re supports GitHub Actions out of the box and lets you automate unit-testing, artifact creation and publishing releases right away. **([example 1 - artifacts and releases](https://github.com/osdeverr/find-msvc/blob/main/.github/workflows/build.yml), [example 2 - unit testing](https://github.com/zwalloc/ulib-process/blob/master/.github/workflows/unit-tests.yml))**
- **Cross-platform support:** Re projects build the same way on **Windows**, **Linux** and **macOS**. If you haven't used any platform-specific features, your project will always get built just fine via invoking `re` on all three platforms.
- **Self-build support:** Re is built using, you guessed it, Re. An older version of Re is used to "bootstrap" the build. As Re is quite extensive, its ability to build itself proves its capabilities.

**WARNING:** Re is still under development, and problems may very well arise. Please [create an issue](https://github.com/osdeverr/rebs/issues/new) if something goes wrong or [create a pull request](https://github.com/osdeverr/rebs/pulls) if you wanna make something right.

## So why Re?

### It just works

Building Re projects doesn’t require you to configure them, write build scripts or really do anything else — just type in “re” in your command line and you’re all set!

### It likes company

Re integrates with Microsoft's [vcpkg](https://github.com/microsoft/vcpkg) package manager, allowing you to access tons of useful packages from the get-go. No special setup needed - it's available out of the box!

As well as that, Re supports first-class C++ development inside of Visual Studio Code with the help of one tiny plugin, making your development experience comfortable from the get-go.

### It's not stupid

Re automatically gathers the source tree and the dependencies for every project you build, allowing you to focus on the actual important part of software development - *writing code.*

### It's quite fast

Despite its extensive features, Re does not struggle to perform and does its job at an acceptable speed. *(TODO: performance comparisons!)*

## How does Re work?

You define your project by creating a YAML file named `re.yml`. This file defines everything that's needed to build your code: build options, dependencies, actions, you name it.

### Example Project

#### `re.yml`
```yaml
type: executable
name: hello-world

deps:
    - vcpkg:fmt
```

#### `main.cpp`
```cpp
#include <fmt/format.h>

int main()
{
    fmt::print("Hello World from Re!\n");
}
```

#### **Build Instructions**
Create the above-described files in a separate directory and run `re`. Yes, that's it.

**NOTE:** If you're running Windows, you will need to have at least Visual Studio Build Tools installed to use Re.

**TIP:** Re provides an easy way to create a bare-bones `re.yml`: just type in `re new <type> <path>`!

## Goals
1. Create an awesome C++ build system.
2. See Goal #1.

## Non-goals
1. **Cover ALL use cases:** while Re strives to be feature-rich, it's probably not a good choice for *cross-compiling Android native applications from a Raspberry Pi.*
2. **Be the fastest:** Re's focus is usability, not extreme optimization. You'd be better off with a raw Makefile if you need to build your project in a tenth of a second.
3. **Support other languages:** while it's technically possible to support languages other than C++, currently the focus is on doing C++ right.

## TODO

Unfortunately, Re is not yet a finished project. Some notable features are still missing, namely:
- Sourcing the code in targets from remote locations for easier ports
- "Developer dependencies" for reusable tooling steps and whatnot
- Smarter dependency version resolution
- More documentation coverage

**If you're willing to help us out, feel free to create a pull request and add those missing features - or propose your own!**
