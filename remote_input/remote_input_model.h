/*
 * The input model: what the remote does with a physical button press.
 *
 * This module has no SDK dependency so that it builds and tests on a
 * development machine (specification 0.4). The application glue translates
 * firmware input events into the enumerations here and translates the outcome
 * back into screen updates and, from FE4 on, transmitted events.
 *
 * The model owns exactly one piece of state, the screen lock (specification
 * 2.4, 2.5). It holds no belief about the appliance and assigns no meaning to
 * any press: CENTER_LONG is reported as CENTER_LONG and nothing more
 * (specification 2.1).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The physical buttons, named for their position rather than for any meaning. */
typedef enum {
    RemoteInputButtonUp,
    RemoteInputButtonDown,
    RemoteInputButtonRight,
    RemoteInputButtonLeft,
    RemoteInputButtonCenter,
    RemoteInputButtonBack,
    RemoteInputButtonCount,
} RemoteInputButton;

/*
 * How the firmware classified a press. Short and Long are the two the control
 * table in specification 2.3 distinguishes. Other covers every phase the
 * firmware also reports (press, release, repeat) that the table has no row
 * for; naming it rather than filtering it in the glue keeps the "ignore what
 * the table does not name" rule testable here.
 */
typedef enum {
    RemoteInputPressShort,
    RemoteInputPressLong,
    RemoteInputPressOther,
    RemoteInputPressKindCount,
} RemoteInputPressKind;

/*
 * The physical events the remote reports upward, the reported column of the
 * control table in specification 2.3 less BACK_SHORT, which was dropped from
 * the protocol at promotion (seam Q13, 2026-09-12) and so is reported no
 * longer. Their wire spelling belongs to the protocol library (FE3), not here.
 */
typedef enum {
    RemoteReportableEventCenterShort,
    RemoteReportableEventCenterLong,
    RemoteReportableEventLeftShort,
    RemoteReportableEventRightShort,
    RemoteReportableEventCount,
} RemoteReportableEvent;

typedef enum {
    /* Not a control this application acts on or reports. Includes anything
     * out of range, so a corrupt or future input value can never crash. */
    RemoteInputOutcomeIgnored,
    /* The screen lock engaged: a short down press while unlocked. */
    RemoteInputOutcomeScreenLocked,
    /* The screen lock released: a long down press while locked. */
    RemoteInputOutcomeScreenUnlocked,
    /* A press that would otherwise report or exit arrived while locked.
     * Nothing leaves the device (specification 2.4, the one guard rule). */
    RemoteInputOutcomeSuppressedByLock,
    /* A reportable event; reportable_event is valid. */
    RemoteInputOutcomeReportableEvent,
    /* The photographer asked to leave the application: a long back press
     * while unlocked. Local, never reported. See IMPLEMENTATION_DEVIATIONS.md. */
    RemoteInputOutcomeExitRequested,
} RemoteInputOutcomeKind;

typedef struct {
    RemoteInputOutcomeKind kind;
    /* Valid only when kind is RemoteInputOutcomeReportableEvent. */
    RemoteReportableEvent reportable_event;
} RemoteInputOutcome;

typedef struct {
    bool screen_locked;
} RemoteInputModel;

/* Starts unlocked. A device that boots locked has to be unlocked before the
 * first session, which is friction for no protection: nothing is running yet. */
void remote_input_model_initialise(RemoteInputModel* input_model);

/* Applies one classified press and returns what the application should do.
 * Any button or press kind outside its enumeration is ignored and leaves the
 * model unchanged. Takes plain integers so an out of range value can be
 * passed by a test without invoking undefined behaviour on the enum. */
RemoteInputOutcome remote_input_model_apply(
    RemoteInputModel* input_model,
    int pressed_button,
    int press_kind);

bool remote_input_model_is_screen_locked(const RemoteInputModel* input_model);

#ifdef __cplusplus
}
#endif
