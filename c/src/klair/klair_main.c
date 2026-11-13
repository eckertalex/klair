#define BUILD_NAME "klair"

#include "base/base_inc.h"
#include "net/net_inc.h"
#include "klair/klair.h"

#include "base/base_inc.c"
#include "net/net_inc.c"
#include "klair/klair_temperature.c"
#include "klair/klair_config.c"
#include "klair/klair_args.c"
#include "klair/klair_http.c"
#include "klair/klair_client.c"
#include "klair/klair_cli.c"

int main(int argc, char **argv) {
    Arena *arena = arena_alloc(GB(1));
    return klair_run(arena, (u64)argc, argv);
}
