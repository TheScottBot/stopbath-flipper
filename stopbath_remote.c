/*
 * StopBath Remote, FE2: application skeleton and display.
 *
 * Renders every display state from the shared fixtures, cycling through them
 * on a timer so each can be judged on the device in daylight (the FE2
 * hardware gate), and implements the screen lock. Nothing here talks to
 * anything: no protocol, no transport (FE3 and FE4).
 *
 * The SDK facing code in this file is deliberately thin. What a press does is
 * decided in remote_input; what goes where on the screen is decided in
 * remote_display. Both have no SDK dependency and are tested on the host.
 * This file translates firmware enumerations, replays a composed layout onto
 * the canvas, and owns the furi resources.
 */
#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>

#include "remote_display/remote_display_fixtures.h"
#include "remote_display/remote_display_layout.h"
#include "remote_input/remote_input_model.h"

/* Enough for a burst of presses while the main loop is drawing. The input
 * callback never blocks the GUI thread on a full queue; it drops and counts,
 * because a stalled GUI would hide the very display this phase exists to
 * prove. */
#define INPUT_EVENT_QUEUE_DEPTH 8

/* How long the main loop waits for an input before checking the carousel. */
#define MAIN_LOOP_INPUT_WAIT_MILLISECONDS 250

/* Long enough to read a screen and judge it, short enough that all of them
 * come round in under a minute. */
#define FIXTURE_CAROUSEL_INTERVAL_MILLISECONDS 4000

typedef struct {
    FuriMessageQueue* input_event_queue;
    /* Guards composed_layout between the main loop, which composes it, and
     * the GUI thread, which replays it. The GUI service thread has a 2 KB
     * stack (its manifest at the pinned commit), so the layout is composed
     * here on the application's own stack and only replayed there. */
    FuriMutex* layout_mutex;
    RemoteDisplayLayout composed_layout;
    /* Main loop only. */
    RemoteInputModel input_model;
    RemoteDisplayState display_state;
    int fixture_index;
    uint32_t fixture_shown_at_tick;
    /* Written and read on the GUI thread only. */
    uint32_t dropped_input_events;
} StopBathRemoteApplication;

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

static Font canvas_font_for(RemoteLayoutFont layout_font) {
    return layout_font == RemoteLayoutFontPrimary ? FontPrimary : FontSecondary;
}

static Align canvas_alignment_for(RemoteLayoutAnchor anchor) {
    switch(anchor) {
    case RemoteLayoutAnchorLeft:
        return AlignLeft;
    case RemoteLayoutAnchorCenter:
        return AlignCenter;
    case RemoteLayoutAnchorRight:
        return AlignRight;
    }
    return AlignLeft;
}

/* Replays a composed layout. Shapes first, then texts, then the firmware's
 * own button hint, so inverted text lands on its box and the hint on top. */
static void replay_layout(Canvas* canvas, const RemoteDisplayLayout* layout) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    for(int shape_index = 0; shape_index < layout->shape_count; shape_index++) {
        const RemoteLayoutShape* shape = &layout->shapes[shape_index];
        if(shape->kind == RemoteLayoutShapeFilledBox) {
            canvas_draw_box(canvas, shape->x, shape->y, (size_t)shape->width, (size_t)shape->height);
        } else {
            canvas_draw_frame(canvas, shape->x, shape->y, (size_t)shape->width, (size_t)shape->height);
        }
    }

    if(layout->qr_area_shown) {
        /* FE5 draws the matrix here. Until then the frame marks the area so
         * the column beside it can be judged at its real width. */
        canvas_draw_frame(canvas, layout->qr_x, layout->qr_y, (size_t)layout->qr_size, (size_t)layout->qr_size);
    }

    for(int text_index = 0; text_index < layout->text_count; text_index++) {
        const RemoteLayoutText* text = &layout->texts[text_index];
        canvas_set_font(canvas, canvas_font_for(text->font));
        canvas_set_color(canvas, text->inverted ? ColorWhite : ColorBlack);
        /* AlignBottom leaves y as the baseline, which is what the layout
         * carries (canvas.c at the pinned commit). */
        canvas_draw_str_aligned(canvas, text->x, text->y, canvas_alignment_for(text->anchor), AlignBottom, text->text);
    }
    canvas_set_color(canvas, ColorBlack);

    if(layout->center_button_shown) {
        canvas_set_font(canvas, FontSecondary);
        elements_button_center(canvas, layout->center_button_label);
    }
}

static void draw_screen(Canvas* canvas, void* opaque_application_pointer) {
    StopBathRemoteApplication* remote_application = opaque_application_pointer;

    /* Replayed under the mutex so a recomposition cannot tear the frame. A
     * failed acquire leaves the previous frame on screen. */
    if(furi_mutex_acquire(remote_application->layout_mutex, FuriWaitForever) != FuriStatusOk) {
        return;
    }
    replay_layout(canvas, &remote_application->composed_layout);
    furi_mutex_release(remote_application->layout_mutex);
}

/* Recomposes the screen from the current state and lock, on the main
 * loop's stack, and publishes it for the GUI thread. */
static void recompose_screen(StopBathRemoteApplication* remote_application) {
    remote_application->display_state.screen_locked =
        remote_input_model_is_screen_locked(&remote_application->input_model);

    RemoteDisplayLayout layout;
    remote_display_layout_compose(&remote_application->display_state, &layout);

    /* An indefinite acquire on a normal mutex cannot time out; any other
     * status means the mutex itself is unusable, and a remote that cannot
     * publish its own screen has nothing sensible left to do. */
    furi_check(furi_mutex_acquire(remote_application->layout_mutex, FuriWaitForever) == FuriStatusOk);
    remote_application->composed_layout = layout;
    furi_mutex_release(remote_application->layout_mutex);
}

static void enqueue_input_event(InputEvent* input_event, void* opaque_application_pointer) {
    StopBathRemoteApplication* remote_application = opaque_application_pointer;
    /* Zero timeout: this runs on the GUI thread and must never block it. */
    if(furi_message_queue_put(remote_application->input_event_queue, input_event, 0) != FuriStatusOk) {
        /* Counted rather than silently lost. Surfaced in the diagnostic view
         * from FE3 on; until then it is readable in a debugger. */
        remote_application->dropped_input_events++;
    }
}

/* Applies one firmware input event to the model and returns true when the
 * application should exit. */
static bool apply_input_event(StopBathRemoteApplication* remote_application, const InputEvent* input_event) {
    int pressed_button = remote_button_for_input_key(input_event->key);
    int press_kind = remote_press_kind_for_input_type(input_event->type);

    RemoteInputOutcome outcome =
        remote_input_model_apply(&remote_application->input_model, pressed_button, press_kind);
    if(outcome.kind == RemoteInputOutcomeScreenLocked || outcome.kind == RemoteInputOutcomeScreenUnlocked) {
        recompose_screen(remote_application);
    }

    /* Reportable events have nowhere to go until FE4 wires the transport.
     * They are neither queued nor remembered (specification 2.10). */
    return outcome.kind == RemoteInputOutcomeExitRequested;
}

static void show_fixture(StopBathRemoteApplication* remote_application, int fixture_index) {
    const RemoteDisplayFixture* fixtures = remote_display_fixtures();
    remote_application->display_state = fixtures[fixture_index].display_state;
    remote_application->fixture_index = fixture_index;
    remote_application->fixture_shown_at_tick = furi_get_tick();
    recompose_screen(remote_application);
}

static void advance_carousel_when_due(StopBathRemoteApplication* remote_application) {
    uint32_t elapsed_ticks = furi_get_tick() - remote_application->fixture_shown_at_tick;
    if(elapsed_ticks < furi_ms_to_ticks(FIXTURE_CAROUSEL_INTERVAL_MILLISECONDS)) {
        return;
    }
    int next_fixture_index = (remote_application->fixture_index + 1) % remote_display_fixture_count();
    show_fixture(remote_application, next_fixture_index);
}

int32_t stopbath_remote_main(void* launch_arguments) {
    UNUSED(launch_arguments);

    /* The application state lives in static storage rather than on this
     * thread's stack: the composed layout and the display state together
     * are about a kilobyte, and the manifest's stack is sized for the
     * composition work, not for holding them as well. One instance can
     * exist, because the loader runs one application at a time. */
    static StopBathRemoteApplication remote_application;
    memset(&remote_application, 0, sizeof(remote_application));
    remote_input_model_initialise(&remote_application.input_model);
    remote_display_state_initialise(&remote_application.display_state);

    /* furi allocations abort on exhaustion rather than returning NULL, so
     * these checks document the contract more than they guard it; they cost
     * nothing and make a future SDK change that returns NULL fail loudly. */
    remote_application.input_event_queue =
        furi_message_queue_alloc(INPUT_EVENT_QUEUE_DEPTH, sizeof(InputEvent));
    furi_check(remote_application.input_event_queue != NULL);
    remote_application.layout_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_check(remote_application.layout_mutex != NULL);

    ViewPort* view_port = view_port_alloc();
    furi_check(view_port != NULL);
    view_port_draw_callback_set(view_port, draw_screen, &remote_application);
    view_port_input_callback_set(view_port, enqueue_input_event, &remote_application);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    show_fixture(&remote_application, 0);

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
        advance_carousel_when_due(&remote_application);
        view_port_update(view_port);
    }

    /* Release in the reverse order of acquisition, on the only exit path. */
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_mutex_free(remote_application.layout_mutex);
    furi_message_queue_free(remote_application.input_event_queue);

    return 0;
}
