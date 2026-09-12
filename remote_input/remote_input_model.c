#include "remote_input_model.h"

void remote_input_model_initialise(RemoteInputModel* input_model) {
    input_model->screen_locked = false;
}

static RemoteInputOutcome outcome_of_kind(RemoteInputOutcomeKind kind) {
    RemoteInputOutcome outcome = {kind, RemoteReportableEventCenterShort};
    return outcome;
}

static RemoteInputOutcome outcome_reporting(RemoteReportableEvent reportable_event) {
    RemoteInputOutcome outcome = {RemoteInputOutcomeReportableEvent, reportable_event};
    return outcome;
}

/*
 * The lock button is handled before the lock is consulted, because it is the
 * one control that must keep working while locked: a locked device that
 * cannot be unlocked is a brick until the cable is out (specification 2.4).
 */
static RemoteInputOutcome apply_lock_button(RemoteInputModel* input_model, RemoteInputPressKind press_kind) {
    if(press_kind == RemoteInputPressShort && !input_model->screen_locked) {
        input_model->screen_locked = true;
        return outcome_of_kind(RemoteInputOutcomeScreenLocked);
    }
    if(press_kind == RemoteInputPressLong && input_model->screen_locked) {
        input_model->screen_locked = false;
        return outcome_of_kind(RemoteInputOutcomeScreenUnlocked);
    }
    /* Locking twice or unlocking an unlocked screen changes nothing. A short
     * press deliberately cannot unlock, so a pocket cannot toggle the lock. */
    return outcome_of_kind(RemoteInputOutcomeIgnored);
}

/*
 * The control table of specification 2.3, reduced to the rows that do
 * something. Every combination not listed here is ignored. This function
 * decides only whether a press is one the table names; whether it may leave
 * the device is decided by the caller against the lock.
 */
static RemoteInputOutcome classify_unlocked_press(RemoteInputButton pressed_button, RemoteInputPressKind press_kind) {
    switch(pressed_button) {
    case RemoteInputButtonCenter:
        if(press_kind == RemoteInputPressShort) return outcome_reporting(RemoteReportableEventCenterShort);
        if(press_kind == RemoteInputPressLong) return outcome_reporting(RemoteReportableEventCenterLong);
        break;
    case RemoteInputButtonLeft:
        if(press_kind == RemoteInputPressShort) return outcome_reporting(RemoteReportableEventLeftShort);
        break;
    case RemoteInputButtonRight:
        if(press_kind == RemoteInputPressShort) return outcome_reporting(RemoteReportableEventRightShort);
        break;
    case RemoteInputButtonBack:
        /* A short back press reports nothing: BACK_SHORT was dropped from the
         * protocol at promotion (seam Q13, 2026-09-12), so there is no event to
         * send and the appliance would refuse one. A long back press still
         * exits the application, which is local and never reported. */
        if(press_kind == RemoteInputPressLong) return outcome_of_kind(RemoteInputOutcomeExitRequested);
        break;
    case RemoteInputButtonUp:
    case RemoteInputButtonDown:
    case RemoteInputButtonCount:
        break;
    }
    return outcome_of_kind(RemoteInputOutcomeIgnored);
}

RemoteInputOutcome remote_input_model_apply(
    RemoteInputModel* input_model,
    int pressed_button,
    int press_kind) {
    /* Range checks first, on the raw integers, so that a value the firmware
     * adds in a later release or a corrupted event can never index anything. */
    if(pressed_button < 0 || pressed_button >= (int)RemoteInputButtonCount) {
        return outcome_of_kind(RemoteInputOutcomeIgnored);
    }
    if(press_kind < 0 || press_kind >= (int)RemoteInputPressKindCount) {
        return outcome_of_kind(RemoteInputOutcomeIgnored);
    }

    RemoteInputButton button = (RemoteInputButton)pressed_button;
    RemoteInputPressKind kind = (RemoteInputPressKind)press_kind;

    if(button == RemoteInputButtonDown) {
        return apply_lock_button(input_model, kind);
    }

    RemoteInputOutcome outcome = classify_unlocked_press(button, kind);
    if(outcome.kind == RemoteInputOutcomeIgnored) {
        return outcome;
    }
    /* The one guard rule: while locked, nothing that the table names may
     * leave the device or end the application. The press is swallowed, not
     * queued, so nothing can be replayed later (specification 2.4, 2.10). */
    if(input_model->screen_locked) {
        return outcome_of_kind(RemoteInputOutcomeSuppressedByLock);
    }
    return outcome;
}

bool remote_input_model_is_screen_locked(const RemoteInputModel* input_model) {
    return input_model->screen_locked;
}
