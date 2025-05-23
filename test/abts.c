/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "abts.h"
#include "abts_tests.h"
#include "testutil.h"
#define INCL_LOADEXCEPTQ
#include "exceptq.h"

#define ABTS_STAT_SIZE 6
static char status[ABTS_STAT_SIZE] = {'|', '/', '-', '|', '\\', '-'};
static int curr_char;
static int verbose = 0;
static int exclude = 0;
static int quiet = 0;
static int list_tests = 0;

const char **testlist = NULL;

static int find_test_name(const char *testname) {
    int i;
    for (i = 0; testlist[i] != NULL; i++) {
        if (!strcmp(testlist[i], testname)) {
            return 1;
        }
    }
    return 0;
}

/* Determine if the test should be run at all */
static int should_test_run(const char *testname) {
    int found = 0;
    if (list_tests == 1) {
        return 0;
    }
    if (testlist == NULL) {
        return 1;
    }
    found = find_test_name(testname);
    if ((found && !exclude) || (!found && exclude)) {
        return 1;
    }
    return 0;
}

static void reset_status(void)
{
    curr_char = 0;
}

static void update_status(void)
{
    if (!quiet) {
        curr_char = (curr_char + 1) % ABTS_STAT_SIZE;
        fprintf(stdout, "\b%c", status[curr_char]);
        fflush(stdout);
    }
}

static void end_suite(abts_suite *suite)
{
    if (suite != NULL) {
        sub_suite *last = suite->tail;
        if (!quiet) {
            fprintf(stdout, "\b");
            fflush(stdout);
        }
        if (last->skipped > 0) {
            fprintf(stdout, "SKIPPED %d of %d\n", last->skipped, last->num_test);
            fflush(stdout);
        }
        if (last->failed == 0) {
            fprintf(stdout, "SUCCESS\n");
            fflush(stdout);
        }
        else {
            fprintf(stdout, "FAILED %d of %d\n", last->failed, last->num_test);
            fflush(stdout);
        }
    }
}

abts_suite *abts_add_suite(abts_suite *suite, const char *suite_name_full)
{
    sub_suite *subsuite;
    char *p;
    const char *suite_name;
    curr_char = 0;
    
    /* Only end the suite if we actually ran it */
    if (suite && suite->tail &&!suite->tail->not_run) {
        end_suite(suite);
    }

    subsuite = malloc(sizeof(*subsuite));
    subsuite->num_test = 0;
    subsuite->failed = 0;
    subsuite->skipped = 0;
    subsuite->next = NULL;
    /* suite_name_full may be an absolute path depending on __FILE__ 
     * expansion */
    suite_name = strrchr(suite_name_full, '/');
    if (!suite_name) {
        suite_name = strrchr(suite_name_full, '\\');
    }
    if (suite_name) {
        suite_name++;
    } else {
        suite_name = suite_name_full;
    }
    p = strrchr(suite_name, '.');
    if (p) {
        subsuite->name = memcpy(calloc(p - suite_name + 1, 1),
                                suite_name, p - suite_name);
    }
    else {
        subsuite->name = strdup(suite_name);
    }

    if (list_tests) {
        fprintf(stdout, "%s\n", subsuite->name);
    }
    
    subsuite->not_run = 0;

    if (suite == NULL) {
        suite = malloc(sizeof(*suite));
        suite->head = subsuite;
        suite->tail = subsuite;
    }
    else {
        suite->tail->next = subsuite;
        suite->tail = subsuite;
    }

    if (!should_test_run(subsuite->name)) {
        subsuite->not_run = 1;
        return suite;
    }

    reset_status();
    fprintf(stdout, "%-20s:  ", subsuite->name);
    update_status();
    fflush(stdout);

    return suite;
}

static void abts_free_suite(abts_suite *suite)
{
    if (suite) {
        sub_suite *dptr, *next;

        for (dptr = suite->head; dptr; dptr = next) {
            next = dptr->next;
            free(dptr->name);
            free(dptr);
        }

        free(suite);
    }
}

void abts_run_test(abts_suite *ts, test_func f, void *value)
{
    abts_case tc;
    sub_suite *ss;

    if (!should_test_run(ts->tail->name)) {
        return;
    }
    ss = ts->tail;

    tc.failed = 0;
    tc.skipped = 0;
    tc.suite = ss;
    
    ss->num_test++;
    update_status();

    f(&tc, value);
    
    if (tc.failed) {
        ss->failed++;
    }

    if (tc.skipped) {
        ss->skipped++;
    }
}

static void report_summary(const char *kind_header,
                           const char *name_header,
                           const char *percent_header)
{
    fprintf(stdout, "%-15s\t\tTotal\t%s\t%s\n",
            kind_header, name_header, percent_header);
    fprintf(stdout, "===================================================\n");
}

static int report(abts_suite *suite)
{
    int failed_count = 0;
    int skipped_count = 0;
    sub_suite *dptr;

    if (suite && suite->tail &&!suite->tail->not_run) {
        end_suite(suite);
    }

    for (dptr = suite->head; dptr; dptr = dptr->next) {
        failed_count += dptr->failed;
    }

    for (dptr = suite->head; dptr; dptr = dptr->next) {
        skipped_count += dptr->skipped;
    }

    if (list_tests) {
        return 0;
    }

    /* Report skipped tests */
    if (skipped_count > 0) {
        dptr = suite->head;
        report_summary("Skipped Tests", "Skip", "Skipped %");
        while (dptr != NULL) {
            if (dptr->skipped != 0) {
                float percent = ((float)dptr->skipped / (float)dptr->num_test);
                fprintf(stdout, "%-15s\t\t%5d\t%4d\t%8.2f%%\n", dptr->name,
                        dptr->num_test, dptr->skipped, percent * 100);
            }
            dptr = dptr->next;
        }
    }

    if (failed_count == 0) {
        printf("All tests passed.\n");
        return 0;
    }

    /* Report failed tests */
    dptr = suite->head;
    if (skipped_count > 0) {
        fprintf(stdout, "\n");
    }
    report_summary("Failed Tests", "Fail", " Failed %");
    while (dptr != NULL) {
        if (dptr->failed != 0) {
            float percent = ((float)dptr->failed / (float)dptr->num_test);
            fprintf(stdout, "%-15s\t\t%5d\t%4d\t%8.2f%%\n", dptr->name,
                    dptr->num_test, dptr->failed, percent * 100);
        }
        dptr = dptr->next;
    }
    return 1;
}

void abts_log_message(const char *fmt, ...)
{
    va_list args;
    update_status();

    if (verbose) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}

#define IMPL_abts_T_equal(T, NAME, FMT, CAST) \
void abts_##NAME##_equal(abts_case *tc, const T expected, const T actual, int lineno) \
{ \
    update_status(); \
    if (tc->failed) return; \
    \
    if (expected == actual) return; \
    \
    tc->failed = TRUE; \
    if (verbose) { \
        fprintf(stderr, "Line %d: expected <%" FMT ">, but saw <%" FMT ">\n", \
                lineno, CAST expected, CAST actual); \
        fflush(stderr); \
    } \
}
IMPL_abts_T_equal(int,                int,    "d",   (int))
IMPL_abts_T_equal(unsigned int,       uint,   "u",   (unsigned int))
IMPL_abts_T_equal(long,               long,   "ld",  (long))
IMPL_abts_T_equal(unsigned long,      ulong,  "lu",  (unsigned long))
IMPL_abts_T_equal(long long,          llong,  "lld", (long long))
IMPL_abts_T_equal(unsigned long long, ullong, "llu", (unsigned long long))
IMPL_abts_T_equal(size_t,             size,   "lu",  (unsigned long))

#define IMPL_abts_T_nequal(T, NAME, FMT, CAST) \
void abts_##NAME##_nequal(abts_case *tc, const T expected, const T actual, int lineno) \
{ \
    update_status(); \
    if (tc->failed) return; \
    \
    if (expected != actual) return; \
    \
    tc->failed = TRUE; \
    if (verbose) { \
        fprintf(stderr, "Line %d: expected something other than <%" FMT ">, " \
                "but saw <%" FMT ">\n", \
                lineno, CAST expected, CAST actual); \
        fflush(stderr); \
    } \
}
IMPL_abts_T_nequal(int,                int,    "d",   (int))
IMPL_abts_T_nequal(unsigned int,       uint,   "u",   (unsigned int))
IMPL_abts_T_nequal(long,               long,   "ld",  (long))
IMPL_abts_T_nequal(unsigned long,      ulong,  "lu",  (unsigned long))
IMPL_abts_T_nequal(long long,          llong,  "lld", (long long))
IMPL_abts_T_nequal(unsigned long long, ullong, "llu", (unsigned long long))
IMPL_abts_T_nequal(size_t,             size,   "lu",  (unsigned long))

void abts_str_equal(abts_case *tc, const char *expected, const char *actual, int lineno)
{
    update_status();
    if (tc->failed) return;

    if (!expected && !actual) return;
    if (expected && actual)
        if (!strcmp(expected, actual)) return;

    tc->failed = TRUE;
    if (verbose) {
        fprintf(stderr, "Line %d: expected <%s>, but saw <%s>\n", lineno, expected, actual);
        fflush(stderr);
    }
}

void abts_str_nequal(abts_case *tc, const char *expected, const char *actual,
                       size_t n, int lineno)
{
    update_status();
    if (tc->failed) return;

    if (!strncmp(expected, actual, n)) return;

    tc->failed = TRUE;
    if (verbose) {
        fprintf(stderr, "Line %d: expected something other than <%s>, but saw <%s>\n",
                lineno, expected, actual);
        fflush(stderr);
    }
}

void abts_ptr_notnull(abts_case *tc, const void *ptr, int lineno)
{
    update_status();
    if (tc->failed) return;

    if (ptr != NULL) return;

    tc->failed = TRUE;
    if (verbose) {
        fprintf(stderr, "Line %d: expected non-NULL, but saw NULL\n", lineno);
        fflush(stderr);
    }
}
 
void abts_ptr_equal(abts_case *tc, const void *expected, const void *actual, int lineno)
{
    update_status();
    if (tc->failed) return;

    if (expected == actual) return;

    tc->failed = TRUE;
    if (verbose) {
        fprintf(stderr, "Line %d: expected <%p>, but saw <%p>\n", lineno, expected, actual);
        fflush(stderr);
    }
}

void abts_fail(abts_case *tc, const char *message, int lineno)
{
    update_status();
    if (tc->failed) return;

    tc->failed = TRUE;
    if (verbose) {
        fprintf(stderr, "Line %d: %s\n", lineno, message);
        fflush(stderr);
    }
}

void abts_skip(abts_case *tc, const char *message, int lineno)
{
    update_status();
    if (tc->skipped) return;

    tc->skipped = TRUE;
    if (verbose) {
        fprintf(stderr, "\bSKIP: Line %d: %s\n", lineno, message);
        fflush(stderr);
    }
}

void abts_assert(abts_case *tc, const char *message, int condition, int lineno)
{
    update_status();
    if (tc->failed) return;

    if (condition) return;

    tc->failed = TRUE;
    if (verbose) {
        fprintf(stderr, "Line %d: %s\n", lineno, message);
        fflush(stderr);
    }
}

void abts_true(abts_case *tc, int condition, int lineno)
{
    update_status();
    if (tc->failed) return;

    if (condition) return;

    tc->failed = TRUE;
    if (verbose) {
        fprintf(stderr, "Line %d: Condition is false, but expected true\n", lineno);
        fflush(stderr);
    }
}

void abts_not_impl(abts_case *tc, const char *message, int lineno)
{
    update_status();

    tc->suite->not_impl++;
    if (verbose) {
        fprintf(stderr, "Line %d: %s\n", lineno, message);
        fflush(stderr);
    }
}

int main(int argc, const char *const argv[]) {
    EXCEPTIONREGISTRATIONRECORD exRegRec;
    int i;
    int rv;
    int list_provided = 0;
    abts_suite *suite = NULL;
   
    LoadExceptq(&exRegRec, "I", "testall");
    initialize();

    quiet = !isatty(STDOUT_FILENO);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) {
            verbose = 1;
            continue;
        }
        if (!strcmp(argv[i], "-x")) {
            exclude = 1;
            continue;
        }
        if (!strcmp(argv[i], "-l")) {
            list_tests = 1;
            continue;
        }
        if (!strcmp(argv[i], "-q")) {
            quiet = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "Invalid option: `%s'\n", argv[i]);
            exit(1);
        }
        list_provided = 1;
    }

    if (list_provided) {
        /* Waste a little space here, because it is easier than counting the
         * number of tests listed.  Besides it is at most three char *.
         */
        testlist = calloc(argc + 1, sizeof(char *));
        for (i = 1; i < argc; i++) {
            testlist[i - 1] = argv[i];
        }
    }

    for (i = 0; i < (sizeof(alltests) / sizeof(struct testlist *)); i++) {
        suite = alltests[i].func(suite);
    }

    rv = report(suite);
    UninstallExceptq(&exRegRec);
    abts_free_suite(suite);
    return rv;
}
       
