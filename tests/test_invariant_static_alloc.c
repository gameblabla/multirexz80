#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "../source/static_alloc.c"

START_TEST(test_realloc_boundary_integrity)
{
    // Invariant: realloc must never write beyond the bounds of the newly allocated buffer
    
    // Test case 1: Shrinking realloc (exploit case - old size > new size)
    size_t old_size = 1024;
    size_t new_size = 64;
    void *ptr = multirexz80_static_malloc(old_size);
    ck_assert_ptr_nonnull(ptr);
    memset(ptr, 0xAA, old_size);
    
    // Allocate sentinel after to detect overflow
    void *sentinel = multirexz80_static_malloc(64);
    ck_assert_ptr_nonnull(sentinel);
    memset(sentinel, 0xBB, 64);
    
    void *new_ptr = multirexz80_static_realloc(ptr, new_size);
    ck_assert_ptr_nonnull(new_ptr);
    
    // Verify sentinel unchanged (no overflow into adjacent allocation)
    unsigned char *check = (unsigned char *)sentinel;
    for (size_t i = 0; i < 64; i++) {
        ck_assert_msg(check[i] == 0xBB, "Memory corruption detected at sentinel[%zu]", i);
    }
    
    // Test case 2: Boundary - realloc to size 1
    void *ptr2 = multirexz80_static_malloc(256);
    ck_assert_ptr_nonnull(ptr2);
    memset(ptr2, 0xCC, 256);
    
    void *sentinel2 = multirexz80_static_malloc(32);
    ck_assert_ptr_nonnull(sentinel2);
    memset(sentinel2, 0xDD, 32);
    
    void *new_ptr2 = multirexz80_static_realloc(ptr2, 1);
    ck_assert_ptr_nonnull(new_ptr2);
    
    check = (unsigned char *)sentinel2;
    for (size_t i = 0; i < 32; i++) {
        ck_assert_msg(check[i] == 0xDD, "Memory corruption at boundary case sentinel[%zu]", i);
    }
    
    // Test case 3: Valid case - growing realloc (should not corrupt)
    void *ptr3 = multirexz80_static_malloc(32);
    ck_assert_ptr_nonnull(ptr3);
    void *new_ptr3 = multirexz80_static_realloc(ptr3, 128);
    ck_assert_ptr_nonnull(new_ptr3);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_realloc_boundary_integrity);
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