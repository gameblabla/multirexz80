#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Include the production code under test */
#include "source/video/snk_ikari_psychos/snk_psychos.c"

#define DST_BUF_SIZE 256

START_TEST(test_memcpy_bounds_invariant)
{
    /* Invariant: offset + size must never exceed destination buffer capacity.
       Any ROM-derived (offset, size) pair that would write beyond DST_BUF_SIZE
       must not corrupt memory outside the allocated buffer. */

    struct { uint32_t offset; uint32_t size; } payloads[] = {
        /* Exact exploit: offset + size overflows buffer */
        { DST_BUF_SIZE - 4, 64 },
        /* Boundary: offset exactly at end of buffer */
        { DST_BUF_SIZE, 1 },
        /* Valid: fits within buffer */
        { 0, 16 },
        /* Large size with zero offset */
        { 0, DST_BUF_SIZE + 1 },
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        uint32_t offset = payloads[i].offset;
        uint32_t size   = payloads[i].size;

        /* Security invariant: offset and size must be validated before use */
        ck_assert_msg(
            offset <= DST_BUF_SIZE,
            "offset %u exceeds destination buffer capacity %d",
            offset, DST_BUF_SIZE
        );
        ck_assert_msg(
            size <= DST_BUF_SIZE,
            "size %u exceeds destination buffer capacity %d",
            size, DST_BUF_SIZE
        );
        ck_assert_msg(
            (uint64_t)offset + (uint64_t)size <= (uint64_t)DST_BUF_SIZE,
            "offset %u + size %u = %lu exceeds destination buffer capacity %d",
            offset, size, (uint64_t)offset + (uint64_t)size, DST_BUF_SIZE
        );
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_memcpy_bounds_invariant);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}