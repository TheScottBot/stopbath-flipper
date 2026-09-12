/*
 * FE1 tests first: button to local action mapping, lock state transitions,
 * and unknown input ignored rather than crashing.
 */
#include "test_support.h"

#include "../remote_input/remote_input_model.h"

static RemoteInputModel unlocked_model(void) {
    RemoteInputModel input_model;
    remote_input_model_initialise(&input_model);
    return input_model;
}

static RemoteInputModel locked_model(void) {
    RemoteInputModel input_model = unlocked_model();
    remote_input_model_apply(&input_model, RemoteInputButtonDown, RemoteInputPressShort);
    return input_model;
}

/* Every row of the control table in specification 2.3 that reports upward. */
typedef struct {
    RemoteInputButton pressed_button;
    RemoteInputPressKind press_kind;
    RemoteReportableEvent expected_event;
    const char* description;
} ReportableRow;

static const ReportableRow reportable_rows[] = {
    {RemoteInputButtonCenter, RemoteInputPressShort, RemoteReportableEventCenterShort, "centre short"},
    {RemoteInputButtonCenter, RemoteInputPressLong, RemoteReportableEventCenterLong, "centre long"},
    {RemoteInputButtonLeft, RemoteInputPressShort, RemoteReportableEventLeftShort, "left short"},
    {RemoteInputButtonRight, RemoteInputPressShort, RemoteReportableEventRightShort, "right short"},
};

static void every_reportable_row_reports_its_event_while_unlocked(RemoteTestReport* report) {
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(reportable_rows); row_index++) {
        const ReportableRow* row = &reportable_rows[row_index];
        RemoteInputModel input_model = unlocked_model();
        RemoteInputOutcome outcome =
            remote_input_model_apply(&input_model, row->pressed_button, row->press_kind);
        REMOTE_TEST_ASSERT_EQUAL_INT(
            report, RemoteInputOutcomeReportableEvent, outcome.kind, row->description);
        REMOTE_TEST_ASSERT_EQUAL_INT(
            report, row->expected_event, outcome.reportable_event, row->description);
        REMOTE_TEST_ASSERT(
            report, !remote_input_model_is_screen_locked(&input_model), "a report does not lock");
    }
}

/* Rows the table does not name: the up button entirely, long presses of left
 * and right, and the press phases that are not a classified press. */
typedef struct {
    RemoteInputButton pressed_button;
    RemoteInputPressKind press_kind;
    const char* description;
} IgnoredRow;

static const IgnoredRow ignored_rows[] = {
    {RemoteInputButtonUp, RemoteInputPressShort, "up short is unused"},
    {RemoteInputButtonUp, RemoteInputPressLong, "up long is unused"},
    {RemoteInputButtonLeft, RemoteInputPressLong, "left long is not in the table"},
    {RemoteInputButtonRight, RemoteInputPressLong, "right long is not in the table"},
    {RemoteInputButtonBack, RemoteInputPressShort, "back short reports nothing, BACK_SHORT dropped at promotion"},
    {RemoteInputButtonCenter, RemoteInputPressOther, "a press phase is not a classified press"},
    {RemoteInputButtonBack, RemoteInputPressOther, "a release phase is not a classified press"},
    {RemoteInputButtonDown, RemoteInputPressOther, "a repeat phase never touches the lock"},
};

static void unnamed_rows_are_ignored_and_change_nothing(RemoteTestReport* report) {
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(ignored_rows); row_index++) {
        const IgnoredRow* row = &ignored_rows[row_index];
        RemoteInputModel input_model = unlocked_model();
        RemoteInputOutcome outcome =
            remote_input_model_apply(&input_model, row->pressed_button, row->press_kind);
        REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteInputOutcomeIgnored, outcome.kind, row->description);
        REMOTE_TEST_ASSERT(
            report, !remote_input_model_is_screen_locked(&input_model), row->description);
    }
}

static void a_short_down_press_locks_the_screen(RemoteTestReport* report) {
    RemoteInputModel input_model = unlocked_model();
    REMOTE_TEST_ASSERT(report, !remote_input_model_is_screen_locked(&input_model), "starts unlocked");
    RemoteInputOutcome outcome =
        remote_input_model_apply(&input_model, RemoteInputButtonDown, RemoteInputPressShort);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteInputOutcomeScreenLocked, outcome.kind, "lock outcome");
    REMOTE_TEST_ASSERT(report, remote_input_model_is_screen_locked(&input_model), "now locked");
}

static void a_long_down_press_unlocks_the_screen(RemoteTestReport* report) {
    RemoteInputModel input_model = locked_model();
    RemoteInputOutcome outcome =
        remote_input_model_apply(&input_model, RemoteInputButtonDown, RemoteInputPressLong);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteInputOutcomeScreenUnlocked, outcome.kind, "unlock outcome");
    REMOTE_TEST_ASSERT(report, !remote_input_model_is_screen_locked(&input_model), "now unlocked");
}

static void a_short_down_press_cannot_unlock(RemoteTestReport* report) {
    /* The unlock must be deliberate (specification 2.4): the same gesture
     * that locked must not also unlock, or a pocket could toggle it. */
    RemoteInputModel input_model = locked_model();
    RemoteInputOutcome outcome =
        remote_input_model_apply(&input_model, RemoteInputButtonDown, RemoteInputPressShort);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteInputOutcomeIgnored, outcome.kind, "already locked");
    REMOTE_TEST_ASSERT(report, remote_input_model_is_screen_locked(&input_model), "still locked");
}

static void a_long_down_press_while_unlocked_does_nothing(RemoteTestReport* report) {
    RemoteInputModel input_model = unlocked_model();
    RemoteInputOutcome outcome =
        remote_input_model_apply(&input_model, RemoteInputButtonDown, RemoteInputPressLong);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteInputOutcomeIgnored, outcome.kind, "nothing to unlock");
    REMOTE_TEST_ASSERT(report, !remote_input_model_is_screen_locked(&input_model), "still unlocked");
}

static void while_locked_every_reportable_press_is_suppressed(RemoteTestReport* report) {
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(reportable_rows); row_index++) {
        const ReportableRow* row = &reportable_rows[row_index];
        RemoteInputModel input_model = locked_model();
        RemoteInputOutcome outcome =
            remote_input_model_apply(&input_model, row->pressed_button, row->press_kind);
        REMOTE_TEST_ASSERT_EQUAL_INT(
            report, RemoteInputOutcomeSuppressedByLock, outcome.kind, row->description);
        REMOTE_TEST_ASSERT(
            report, remote_input_model_is_screen_locked(&input_model), "suppression keeps the lock");
    }
}

static void while_locked_unnamed_rows_are_still_ignored(RemoteTestReport* report) {
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(ignored_rows); row_index++) {
        const IgnoredRow* row = &ignored_rows[row_index];
        RemoteInputModel input_model = locked_model();
        RemoteInputOutcome outcome =
            remote_input_model_apply(&input_model, row->pressed_button, row->press_kind);
        REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteInputOutcomeIgnored, outcome.kind, row->description);
        REMOTE_TEST_ASSERT(report, remote_input_model_is_screen_locked(&input_model), row->description);
    }
}

static void a_long_back_press_while_unlocked_requests_exit(RemoteTestReport* report) {
    RemoteInputModel input_model = unlocked_model();
    RemoteInputOutcome outcome =
        remote_input_model_apply(&input_model, RemoteInputButtonBack, RemoteInputPressLong);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteInputOutcomeExitRequested, outcome.kind, "exit requested");
}

static void a_long_back_press_while_locked_is_suppressed(RemoteTestReport* report) {
    /* Leaving the application from a pocket would drop the link silently. */
    RemoteInputModel input_model = locked_model();
    RemoteInputOutcome outcome =
        remote_input_model_apply(&input_model, RemoteInputButtonBack, RemoteInputPressLong);
    REMOTE_TEST_ASSERT_EQUAL_INT(
        report, RemoteInputOutcomeSuppressedByLock, outcome.kind, "no exit while locked");
    REMOTE_TEST_ASSERT(report, remote_input_model_is_screen_locked(&input_model), "still locked");
}

typedef struct {
    int pressed_button;
    int press_kind;
    const char* description;
} OutOfRangeRow;

static const OutOfRangeRow out_of_range_rows[] = {
    {RemoteInputButtonCount, RemoteInputPressShort, "button one past the end"},
    {-1, RemoteInputPressShort, "negative button"},
    {1000000, RemoteInputPressLong, "absurd button"},
    {RemoteInputButtonCenter, RemoteInputPressKindCount, "press kind one past the end"},
    {RemoteInputButtonCenter, -1, "negative press kind"},
    {RemoteInputButtonDown, 1000000, "absurd press kind on the lock button"},
    {RemoteInputButtonCount, RemoteInputPressKindCount, "both out of range"},
};

static void out_of_range_input_is_ignored_whether_locked_or_not(RemoteTestReport* report) {
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(out_of_range_rows); row_index++) {
        const OutOfRangeRow* row = &out_of_range_rows[row_index];

        RemoteInputModel unlocked = unlocked_model();
        RemoteInputOutcome unlocked_outcome =
            remote_input_model_apply(&unlocked, row->pressed_button, row->press_kind);
        REMOTE_TEST_ASSERT_EQUAL_INT(
            report, RemoteInputOutcomeIgnored, unlocked_outcome.kind, row->description);
        REMOTE_TEST_ASSERT(report, !remote_input_model_is_screen_locked(&unlocked), row->description);

        RemoteInputModel locked = locked_model();
        RemoteInputOutcome locked_outcome =
            remote_input_model_apply(&locked, row->pressed_button, row->press_kind);
        REMOTE_TEST_ASSERT_EQUAL_INT(
            report, RemoteInputOutcomeIgnored, locked_outcome.kind, row->description);
        REMOTE_TEST_ASSERT(report, remote_input_model_is_screen_locked(&locked), row->description);
    }
}

static void initialise_resets_a_locked_model(RemoteTestReport* report) {
    RemoteInputModel input_model = locked_model();
    remote_input_model_initialise(&input_model);
    REMOTE_TEST_ASSERT(report, !remote_input_model_is_screen_locked(&input_model), "reset to unlocked");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"every reportable row reports its event while unlocked",
         every_reportable_row_reports_its_event_while_unlocked},
        {"unnamed rows are ignored and change nothing", unnamed_rows_are_ignored_and_change_nothing},
        {"a short down press locks the screen", a_short_down_press_locks_the_screen},
        {"a long down press unlocks the screen", a_long_down_press_unlocks_the_screen},
        {"a short down press cannot unlock", a_short_down_press_cannot_unlock},
        {"a long down press while unlocked does nothing",
         a_long_down_press_while_unlocked_does_nothing},
        {"while locked every reportable press is suppressed",
         while_locked_every_reportable_press_is_suppressed},
        {"while locked unnamed rows are still ignored", while_locked_unnamed_rows_are_still_ignored},
        {"a long back press while unlocked requests exit",
         a_long_back_press_while_unlocked_requests_exit},
        {"a long back press while locked is suppressed", a_long_back_press_while_locked_is_suppressed},
        {"out of range input is ignored whether locked or not",
         out_of_range_input_is_ignored_whether_locked_or_not},
        {"initialise resets a locked model", initialise_resets_a_locked_model},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
