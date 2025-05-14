# CBuild - A Single-Header C Build System

CBuild is a lightweight, easy-to-use build system for C projects that eliminates the complexity of traditional build tools. With just a single header file, you can configure and manage your entire build process.

## Features

- **Single Header** - Just include `cbuild.h` in your build script
- **Self-Rebuilding** - Build scripts can rebuild themselves when changed
- **Simple API** - Intuitive macros and functions for defining build targets
- **Cross-Platform** - Works on Windows, macOS, and Linux
- **No External Dependencies** - Pure C implementation
- **Flexible Target System** - Create executables, static libraries, and shared libraries
- **Sub-Project Support** - Easily manage dependencies and sub-projects with their own build.c

## Getting Started

1. Copy `cbuild.h` to your project
2. Create a build script (e.g., `build.c`)
3. Define your build targets
4. Compile and run your build script

## Basic Usage

```c
#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char** argv) {
    // Rebuild the build script itself if it changes
    CBUILD_SELF_REBUILD("build.c", "cbuild.h");

    // Set output directory
    cbuild_set_output_dir("build");

    // Define targets
    target_t* my_lib;
    CBUILD_STATIC_LIBRARY(my_lib,
        CBUILD_SOURCES(my_lib, "src/*.c");
        CBUILD_INCLUDES(my_app, "include");
    );

    target_t* my_app;
    CBUILD_EXECUTABLE(my_app,
        CBUILD_SOURCES(my_app, "main.c");
        CBUILD_INCLUDES(my_app, "include");
    );

    // Link libraries
    cbuild_target_link_library(my_app, my_lib);

    // Setup subcommands (name, target, [optional: command], [optional: callback], [optional: args])
    // can be invoked with ./cbuild run
    cbuild_register_subcommand("run", my_app, "./build/my_app", NULL, NULL)

    // Run the build
    return cbuild_run(argc, argv);
}
```

## Command Line Interface

Build the cbuild exe (only neeeded once):
```bash
$ gcc -o cbuild build.c
```

Build all targets:
```bash
$ ./cbuild
```

Clean build directory:
```bash
$ ./cbuild clean
```

## Why CBuild?

- **Simplicity**: No complex configuration files or build DSLs
- **Control**: Pure C code gives you full control over the build process
- **Speed**: Lightweight implementation for fast builds
- **Transparency**: Easy to understand what's happening in your build

## License

[BSD-3 Clause License](LICENSE)

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Acknowledgments

- [nob.h](https://github.com/tsoding/nob.h)
- [tup](https://github.com/gittup/tup)
