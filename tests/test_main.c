#include <stdio.h>

#include "nyx/vm.h"
#include "test_harness.h"

int nyx_test_failures = 0;
int nyx_test_count = 0;

void run_scanner_tests(void);
void run_chunk_tests(void);
void run_table_tests(void);
void run_vm_tests(void);

int main(void) {
    initVM();

    run_scanner_tests();
    run_chunk_tests();
    run_table_tests();
    run_vm_tests();

    freeVM();

    fprintf(stderr, "\n%d/%d checks passed\n", nyx_test_count - nyx_test_failures, nyx_test_count);
    return nyx_test_failures > 0 ? 1 : 0;
}
