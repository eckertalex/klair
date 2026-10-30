#define BUILD_NAME "klair"

// includes
#include "base/base_inc.h"

// implementations
#include "base/base_inc.c"

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {
    printf("%s\n", BUILD_VERSION_FULL);

    return 0;
}
