#include "base/nop_map.h"
#include "test_util.h"

int main(void)
{
    nop_map_t *m = nop_map_create(0);
    int a = 1, b = 2, c = 3;

    CHECK(m != NULL);
    CHECK_EQ_INT(nop_map_put(m, "alpha", &a), 0);
    CHECK_EQ_INT(nop_map_put(m, "beta",  &b), 0);
    CHECK_EQ_INT(nop_map_put(m, "gamma", &c), 0);
    CHECK_EQ_INT(nop_map_size(m), 3);

    CHECK(nop_map_get(m, "alpha") == &a);
    CHECK(nop_map_get(m, "beta")  == &b);
    CHECK(nop_map_get(m, "gamma") == &c);
    CHECK(nop_map_get(m, "missing") == NULL);

    /* replace */
    CHECK_EQ_INT(nop_map_put(m, "alpha", &c), 0);
    CHECK(nop_map_get(m, "alpha") == &c);
    CHECK_EQ_INT(nop_map_size(m), 3);

    nop_map_destroy(m);
    TEST_RETURN();
}
