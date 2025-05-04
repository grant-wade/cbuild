# cbuild.h example

This is a simple project that has a subproject dependency that will be built 
first then linked to the output of the main executable.

## How to use

```bash

$ gcc -o cbuild build.c

$ ./cbuild
COMMAND    build subproject math
COMPILE    math.c
LINK       build/libmath.a
✔ Build succeeded.
COMPILE    main.c
LINK       build/main
✔ Build succeeded.

```

