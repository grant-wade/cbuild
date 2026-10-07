#include "../cbuild.h"

static_assert(CBUILD_VERSION_MAJOR == 0, "Unexpected major version");
static_assert(CBUILD_VERSION_MINOR == 1, "Unexpected minor version");
static_assert(CBUILD_VERSION_PATCH == 1, "Unexpected patch version");

const char *cbuild_cpp_include_smoke_version() {
    return CBUILD_VERSION;
}
