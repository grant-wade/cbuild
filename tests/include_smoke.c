#include "../cbuild.h"

#if CBUILD_VERSION_MAJOR != 0 || CBUILD_VERSION_MINOR != 1 || CBUILD_VERSION_PATCH != 2
#error "Unexpected CBuild version"
#endif

const char *cbuild_include_smoke_version(void) {
    return CBUILD_VERSION;
}
