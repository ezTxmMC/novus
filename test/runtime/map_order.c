/* The two ways a map stays in key order (runtime/nv_data.h), seen from C:
 * adding a key out of order to a big sorted map must keep the map in
 * position-list form (a binary search per iteration, no sort of everything),
 * a removal must drop it, and the map header must stay small, because every
 * object field map and map literal pays for it. */
#include "../../runtime/novus_rt.h"

#define KEYS 200
#define MAP_HEADER_MAX_BYTES 40
#define KEY_BYTES 16

static char names[KEYS * 5][KEY_BYTES];

static const char *name_of(int number) {
    snprintf(names[number], KEY_BYTES, "k%05d", number);
    return names[number];
}

static int keys_in_order(NvMap *m) {
    int i;
    nv_map_order(m);
    for (i = 1; i < m->len; i++) {
        if (strcmp(nv_map_nth(m, i - 1)->key, nv_map_nth(m, i)->key) >= 0) {
            return 0;
        }
    }
    return 1;
}

static void report(const char *label, NvMap *m) {
    printf("%s: %d keys, position list: %s, in key order: %s\n", label, m->len, m->ord ? "yes" : "no",
           keys_in_order(m) ? "yes" : "no");
}

int main(int argc, char **argv) {
    NvMap *m;
    int i;
    nv_init_args(argc, argv);
    printf("map header within %d bytes: %s\n", MAP_HEADER_MAX_BYTES, sizeof(NvMap) <= MAP_HEADER_MAX_BYTES ? "yes" : "no");

    m = nv_map_new();
    for (i = 0; i < KEYS; i++) {
        nv_map_set_static(m, name_of(i * 2), nv_int(i));
    }
    report("sorted fill", m);

    nv_map_set_static(m, name_of(3), nv_int(-1));
    printf("one key out of order: position list %s\n", m->ord ? "yes" : "no");
    report("after iterating", m);

    for (i = 0; i < KEYS; i++) {
        nv_map_set_static(m, name_of(KEYS * 2 + i * 2 + 1), nv_int(i));
        nv_map_set_static(m, name_of(i * 2 + 1), nv_int(i));
        keys_in_order(m);
    }
    report("grown by interleaved inserts", m);
    printf("position list kept through the growth: %s\n", m->ord ? "yes" : "no");

    nv_map_remove(m, name_of(10));
    printf("after a removal: position list %s\n", m->ord ? "yes" : "no");
    report("after the removal", m);
    printf("removed key gone: %s, neighbour present: %s\n", nv_map_has(m, name_of(10)) ? "no" : "yes",
           nv_map_has(m, name_of(12)) ? "yes" : "no");

    m = nv_map_new();
    for (i = 0; i < 10; i++) {
        nv_map_set_static(m, name_of(10 - i), nv_int(i));
    }
    report("small and reversed", m);
    return 0;
}
