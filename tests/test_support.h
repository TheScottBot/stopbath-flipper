/*
 * The shared test harness for host side tests (specification 0.7: fixtures
 * are shared, not reimplemented per file). No allocation, no dependency
 * beyond the C standard library, so it runs identically under the MinGW
 * compiler on Windows and under the sanitiser build on Linux.
 */
#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int assertions_failed;
    int assertions_passed;
} RemoteTestReport;

typedef void (*RemoteTestFunction)(RemoteTestReport* report);

typedef struct {
    const char* test_name;
    RemoteTestFunction test_function;
} RemoteTestCase;

/* Each assertion names the file and line so a failure in a table driven test
 * points at the row, not at the loop. */
#define REMOTE_TEST_ASSERT(report, condition, description)                             \
    do {                                                                               \
        if(condition) {                                                                \
            (report)->assertions_passed++;                                             \
        } else {                                                                       \
            (report)->assertions_failed++;                                             \
            fprintf(stderr, "    FAILED %s:%d: %s\n", __FILE__, __LINE__, description); \
        }                                                                              \
    } while(0)

#define REMOTE_TEST_ASSERT_EQUAL_INT(report, expected, actual, description)          \
    do {                                                                              \
        long long remote_test_expected_value = (long long)(expected);                 \
        long long remote_test_actual_value = (long long)(actual);                     \
        if(remote_test_expected_value == remote_test_actual_value) {                  \
            (report)->assertions_passed++;                                            \
        } else {                                                                      \
            (report)->assertions_failed++;                                            \
            fprintf(                                                                  \
                stderr,                                                               \
                "    FAILED %s:%d: %s (expected %lld, got %lld)\n",                   \
                __FILE__,                                                             \
                __LINE__,                                                             \
                description,                                                          \
                remote_test_expected_value,                                           \
                remote_test_actual_value);                                            \
        }                                                                             \
    } while(0)

#define REMOTE_TEST_ROW_COUNT(rows) ((int)(sizeof(rows) / sizeof((rows)[0])))

/*
 * Allocation accounting. Every test binary is linked with the heap functions
 * wrapped (see the Makefile), so any allocation made by code under test
 * passes through here and is counted. A test that must not allocate reads
 * the counter before and after. The wrappers forward to the real functions
 * so the C library itself keeps working.
 */
extern int remote_test_allocation_count;
int remote_test_allocation_count = 0;

void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* block, size_t size);
void __real_free(void* block);
void* __wrap_malloc(size_t size);
void* __wrap_calloc(size_t count, size_t size);
void* __wrap_realloc(void* block, size_t size);
void __wrap_free(void* block);

void* __wrap_malloc(size_t size) {
    remote_test_allocation_count++;
    return __real_malloc(size);
}
void* __wrap_calloc(size_t count, size_t size) {
    remote_test_allocation_count++;
    return __real_calloc(count, size);
}
void* __wrap_realloc(void* block, size_t size) {
    remote_test_allocation_count++;
    return __real_realloc(block, size);
}
void __wrap_free(void* block) {
    __real_free(block);
}

/* Runs every case, prints one line per case, and returns the process exit
 * code: zero only when every assertion in every case passed. A case with no
 * assertions at all is a failure, because an empty test proves nothing. */
static inline int remote_test_run_all(const RemoteTestCase* test_cases, int test_case_count) {
    int cases_failed = 0;
    for(int case_index = 0; case_index < test_case_count; case_index++) {
        RemoteTestReport report = {0};
        test_cases[case_index].test_function(&report);
        bool case_passed = (report.assertions_failed == 0) && (report.assertions_passed > 0);
        printf(
            "%s %s (%d assertions)\n",
            case_passed ? "PASS" : "FAIL",
            test_cases[case_index].test_name,
            report.assertions_passed + report.assertions_failed);
        if(!case_passed) {
            cases_failed++;
        }
    }
    printf("%d of %d cases passed\n", test_case_count - cases_failed, test_case_count);
    return cases_failed == 0 ? 0 : 1;
}
