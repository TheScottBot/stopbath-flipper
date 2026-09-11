/*
 * StopBath Remote, FE1: first light.
 *
 * Launches, draws, shows locally which button was pressed, and implements the
 * screen lock. Nothing here talks to anything: no protocol, no transport, no
 * appliance concern (specification FE1, Excluded).
 *
 * The SDK facing code in this file is deliberately thin. Everything that
 * decides what a press means to the device lives in remote_input, which has
 * no SDK dependency and is tested on the host.
 */
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include "remote_input/remote_input_model.h"

/* Enough for a burst of presses while the main loop is drawing. The input
 * callback never blocks the GUI thread on a full queue; it drops and counts,
 * because a stalled GUI would hide the very display this phase exists to
 * prove. */
#define INPUT_EVENT_QUEUE_DEPTH 8

/* How long the main loop waits for an input before refreshing the screen,
 * so the free heap figure stays current without a timer of its own. */
#define MAIN_LOOP_INPUT_WAIT_MILLISECONDS 250

typedef struct {
    FuriMessageQueue* input_event_queue;
    FuriMutex* model_mutex;
    RemoteInputModel input_model;
    /* Presentation only: what to say about the last press. */
    const char* last_press_label;
    uint32_t dropped_input_events;
} StopBathRemoteApplication;

/* Human readable labels for the local display. These are not wire names;
 * the wire spelling of a reportable event belongs to the protocol library
 * from FE3 on, so nothing here is duplicated there. */
static const char* label_for_reportable_event(RemoteReportableEvent reportable_event) {
    switch(reportable_event) {
    case RemoteReportableEventCenterShort:
        return "Centre short";
    case RemoteReportableEventCenterLong:
        return "Centre long";
    case RemoteReportableEventLeftShort:
        return "Left short";
    case RemoteReportableEventRightShort:
        return "Right short";
    case RemoteReportableEventBackShort:
        return "Back short";
    case RemoteReportableEventCount:
        break;
    }
    return "Unknown event";
}

static const char* label_for_outcome(const RemoteInputOutcome* outcome) {
    switch(outcome->kind) {
    case RemoteInputOutcomeIgnored:
        return "Ignored";
    case RemoteInputOutcomeScreenLocked:
        return "Locked";
    case RemoteInputOutcomeScreenUnlocked:
        return "Unlocked";
    case RemoteInputOutcomeSuppressedByLock:
        return "Blocked by lock";
    case RemoteInputOutcomeReportableEvent:
        return label_for_reportable_event(outcome->reportable_event);
    case RemoteInputOutcomeExitRequested:
        return "Exit";
    }
    return "Unknown outcome";
}

/* The firmware's button and press enumerations are mapped by explicit
 * switch rather than by numeric equality, so a reordering in a future API
 * cannot silently change which button is which. Anything unmapped becomes
 * an out of range value that the model ignores. */
static int remote_button_for_input_key(InputKey input_key) {
    switch(input_key) {
    case InputKeyUp:
        return RemoteInputButtonUp;
    case InputKeyDown:
        return RemoteInputButtonDown;
    case InputKeyRight:
        return RemoteInputButtonRight;
    case InputKeyLeft:
        return RemoteInputButtonLeft;
    case InputKeyOk:
        return RemoteInputButtonCenter;
    case InputKeyBack:
        return RemoteInputButtonBack;
    case InputKeyMAX:
        break;
    }
    return RemoteInputButtonCount;
}

/* The long press classification is the firmware's own (InputTypeLong, 300 ms
 * at the pinned commit, recorded in the evaluation log 4.1), per the author's
 * FE1 decision. This is the one place to change if a longer deliberate hold
 * is wanted after field use. */
static int remote_press_kind_for_input_type(InputType input_type) {
    switch(input_type) {
    case InputTypeShort:
        return RemoteInputPressShort;
    case InputTypeLong:
        return RemoteInputPressLong;
    case InputTypePress:
    case InputTypeRelease:
    case InputTypeRepeat:
        return RemoteInputPressOther;
    case InputTypeMAX:
        break;
    }
    return RemoteInputPressKindCount;
}

static void draw_screen(Canvas* canvas, void* opaque_application_pointer) {
    StopBathRemoteApplication* remote_application = opaque_application_pointer;

    /* The draw callback runs on the GUI thread while the main loop mutates
     * the model, so the read is taken under the same mutex. A failed acquire
     * leaves the previous frame on screen rather than drawing torn state. */
    if(furi_mutex_acquire(remote_application->model_mutex, FuriWaitForever) != FuriStatusOk) {
        return;
    }
    bool screen_locked = remote_input_model_is_screen_locked(&remote_application->input_model);
    const char* last_press_label = remote_application->last_press_label;
    furi_mutex_release(remote_application->model_mutex);
    uint32_t dropped_input_events = remote_application->dropped_input_events;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "StopBath Remote");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "Last:");
    canvas_draw_str(canvas, 30, 24, last_press_label);

    if(screen_locked) {
        /* Inverted banner so the locked state is obvious at a glance
         * (specification 2.4). */
        canvas_draw_box(canvas, 0, 28, 128, 12);
        canvas_invert_color(canvas);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, "LOCKED  (hold Down)");
        canvas_invert_color(canvas);
    } else {
        canvas_draw_str(canvas, 2, 37, "Unlocked  (Down locks)");
    }

    /* Free heap is shown so the FE1 hardware gate can record the headroom
     * the evaluation log lists as unmeasured. */
    char diagnostic_line[40];
    snprintf(
        diagnostic_line,
        sizeof(diagnostic_line),
        "Heap free %lu  dropped %lu",
        (unsigned long)memmgr_get_free_heap(),
        (unsigned long)dropped_input_events);
    canvas_draw_str(canvas, 2, 50, diagnostic_line);

    canvas_draw_str(canvas, 2, 62, "Hold Back to exit");
}

static void enqueue_input_event(InputEvent* input_event, void* opaque_application_pointer) {
    StopBathRemoteApplication* remote_application = opaque_application_pointer;
    /* Zero timeout: this runs on the GUI thread and must never block it. */
    if(furi_message_queue_put(remote_application->input_event_queue, input_event, 0) != FuriStatusOk) {
        /* Counted rather than silently lost, and shown on screen. The counter
         * is written only here and read only in draw_screen, and both run on
         * the GUI thread, so it needs no lock. */
        remote_application->dropped_input_events++;
    }
}

/* Applies one firmware input event to the model under the mutex and returns
 * true when the application should exit. */
static bool apply_input_event(StopBathRemoteApplication* remote_application, const InputEvent* input_event) {
    int pressed_button = remote_button_for_input_key(input_event->key);
    int press_kind = remote_press_kind_for_input_type(input_event->type);

    /* An indefinite acquire on a normal mutex cannot time out; any other
     * status means the mutex itself is unusable, and a remote that cannot
     * touch its own model safely has nothing sensible left to do. */
    furi_check(furi_mutex_acquire(remote_application->model_mutex, FuriWaitForever) == FuriStatusOk);
    RemoteInputOutcome outcome =
        remote_input_model_apply(&remote_application->input_model, pressed_button, press_kind);
    /* Press and release phases arrive for every key and would otherwise
     * overwrite the label of the classified press they bracket. */
    if(outcome.kind != RemoteInputOutcomeIgnored) {
        remote_application->last_press_label = label_for_outcome(&outcome);
    }
    furi_mutex_release(remote_application->model_mutex);

    return outcome.kind == RemoteInputOutcomeExitRequested;
}

int32_t stopbath_remote_main(void* launch_arguments) {
    UNUSED(launch_arguments);

    StopBathRemoteApplication remote_application = {
        .input_event_queue = NULL,
        .model_mutex = NULL,
        .last_press_label = "none yet",
        .dropped_input_events = 0,
    };
    remote_input_model_initialise(&remote_application.input_model);

    /* furi allocations abort on exhaustion rather than returning NULL, so
     * these checks document the contract more than they guard it; they cost
     * nothing and make a future SDK change that returns NULL fail loudly. */
    remote_application.input_event_queue =
        furi_message_queue_alloc(INPUT_EVENT_QUEUE_DEPTH, sizeof(InputEvent));
    furi_check(remote_application.input_event_queue != NULL);
    remote_application.model_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_check(remote_application.model_mutex != NULL);

    ViewPort* view_port = view_port_alloc();
    furi_check(view_port != NULL);
    view_port_draw_callback_set(view_port, draw_screen, &remote_application);
    view_port_input_callback_set(view_port, enqueue_input_event, &remote_application);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    bool exit_requested = false;
    while(!exit_requested) {
        InputEvent input_event;
        FuriStatus queue_status = furi_message_queue_get(
            remote_application.input_event_queue, &input_event, MAIN_LOOP_INPUT_WAIT_MILLISECONDS);
        if(queue_status == FuriStatusOk) {
            exit_requested = apply_input_event(&remote_application, &input_event);
        } else if(queue_status != FuriStatusErrorTimeout) {
            /* Any status other than a message or a timeout means the queue
             * itself is broken, and there is nothing sensible to do with a
             * remote that cannot read its own buttons. */
            furi_crash("input queue failed");
        }
        view_port_update(view_port);
    }

    /* Release in the reverse order of acquisition, on the only exit path. */
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_mutex_free(remote_application.model_mutex);
    furi_message_queue_free(remote_application.input_event_queue);

    return 0;
}
