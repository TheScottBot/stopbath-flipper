/*
 * StopBath Remote, FE4: transport and session.
 *
 * The application now talks to the appliance (or the development peer) over
 * the USB link. It reports button events, renders the display state it is
 * sent, presents the page's QR and NFC, and owns the screen lock. It holds
 * no authoritative state and interprets no button (specification 2.1, 2.5):
 * every such decision is in remote_session, remote_display, remote_input,
 * remote_qr and remote_ndef, all of which are tested on the host. This file
 * is the SDK edge: firmware enumerations, the canvas, the furi resources, and
 * the USB transport in remote_transport.
 */
#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <nfc/nfc.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/type_4_tag/type_4_tag.h>
#include <toolbox/simple_array.h>

#include "remote_display/remote_display_layout.h"
#include "remote_display/remote_ndef.h"
#include "remote_display/remote_qr.h"
#include "remote_input/remote_input_model.h"
#include "remote_transport.h"
#include "session/remote_session.h"

/* The firmware log tag. The application keeps the firmware command line on USB
 * channel 0 while the link runs on channel 1, so a development machine can open
 * channel 0 and run `log` to watch this trace live while the link works. See
 * docs/DIAGNOSTICS.md. */
#define TAG "StopBathRemote"

/* The identifier this peripheral sends in HELLO. A token, not a name. */
#define PERIPHERAL_TOKEN "flipper-zero"

/* Enough for a burst of presses while the main loop is drawing. The input
 * callback never blocks the GUI thread on a full queue; it drops and counts. */
#define INPUT_EVENT_QUEUE_DEPTH 8

/* The loop services the USB link by polling, so it wakes often enough that an
 * incoming display update is rendered promptly. A pressed button wakes the
 * loop immediately through the input queue, so this bounds only how stale an
 * inbound record can be, not how responsive a press is. Fifty milliseconds is
 * imperceptible on screen and light on a device that is USB powered whenever
 * the link is up. */
#define MAIN_LOOP_SERVICE_INTERVAL_MILLISECONDS 50

/* How often to retry opening the link if it did not open at startup. The USB
 * mode is locked while an RPC session is active, which happens for a moment
 * when the host installs and launches the application (ufbt launch), so the
 * first open can fail; retrying opens the link as soon as the lock clears. */
#define LINK_OPEN_RETRY_INTERVAL_MILLISECONDS 1000

/*
 * The identity the NFC surface presents, unchanged from the FD15 to FD17
 * experiment: a seven byte UID, the ATQA the firmware's unit test uses for
 * such a card, the SAK bit its ISO14443-4 detection tests, and a spelled out
 * ATS with a frame waiting time a threaded reply can meet. See the evaluation
 * log NFC section for the derivation.
 */
static const uint8_t nfc_presentation_uid[] = {0x04, 0x53, 0x42, 0x52, 0x4D, 0x54, 0x01};
static const uint8_t nfc_presentation_atqa[] = {0x44, 0x00};
#define NFC_PRESENTATION_SAK_ISO14443_4 0x20
#define NFC_PRESENTATION_ATS_TL         5
#define NFC_PRESENTATION_ATS_T0         ((1U << 4) | (1U << 5) | (1U << 6) | 8U)
#define NFC_PRESENTATION_ATS_TA1        (1U << 7)
#define NFC_PRESENTATION_ATS_TB1        (8U << 4)
#define NFC_PRESENTATION_ATS_TC1        (1U << 1)

typedef struct {
    Nfc* nfc;
    Type4TagData* tag_data;
    NfcListener* listener;
    RemoteNdefMessage presented_message;
    bool presenting;
    uint32_t reader_events;
} NfcPresentation;

typedef struct {
    FuriMessageQueue* input_event_queue;
    /* Guards composed_layout and the code bitmap between the main loop, which
     * composes them, and the GUI thread, which replays them. */
    FuriMutex* layout_mutex;
    RemoteDisplayLayout composed_layout;
    RemoteQrBitmap code_bitmap;
    bool code_bitmap_valid;
    /* Main loop only. */
    RemoteInputModel input_model;
    RemoteSession session;
    RemoteTransport* transport;
    RemoteDisplayState display_state;
    uint32_t dropped_input_events;
    NfcPresentation nfc_presentation;
    /* Remembered values so the diagnostic trace logs each change once, rather
     * than every loop. Zeroed at start, which matches the initial link and
     * display, so nothing spurious is logged before anything happens. */
    RemoteSessionLinkState traced_link_state;
    uint32_t traced_reconnections;
    uint32_t traced_malformed;
    uint32_t traced_guard_drops;
    uint32_t traced_output_drops;
    uint32_t traced_handshake_retries;
    int traced_status;
    char traced_error_code[REMOTE_DISPLAY_ERROR_CODE_CAPACITY];
} StopBathRemoteApplication;

static NfcCommand nfc_presentation_listener_callback(NfcGenericEvent event, void* opaque_application_pointer) {
    UNUSED(event);
    StopBathRemoteApplication* remote_application = opaque_application_pointer;
    remote_application->nfc_presentation.reader_events++;
    return NfcCommandContinue;
}

static void nfc_presentation_stop(NfcPresentation* presentation) {
    if(!presentation->presenting) {
        return;
    }
    nfc_listener_stop(presentation->listener);
    nfc_listener_free(presentation->listener);
    presentation->listener = NULL;
    type_4_tag_free(presentation->tag_data);
    presentation->tag_data = NULL;
    /* The message held the passphrase; clear it as soon as it is no longer
     * presented (specification Part 5). */
    memset(&presentation->presented_message, 0, sizeof(presentation->presented_message));
    presentation->presenting = false;
}

static void nfc_presentation_start(NfcPresentation* presentation, const RemoteNdefMessage* message, void* callback_context) {
    presentation->tag_data = type_4_tag_alloc();
    furi_check(presentation->tag_data != NULL);

    Iso14443_4aData* iso14443_4a_data = type_4_tag_get_base_data(presentation->tag_data);
    furi_check(iso14443_4a_set_uid(iso14443_4a_data, nfc_presentation_uid, sizeof(nfc_presentation_uid)));
    iso14443_4a_data->ats_data.tl = NFC_PRESENTATION_ATS_TL;
    iso14443_4a_data->ats_data.t0 = NFC_PRESENTATION_ATS_T0;
    iso14443_4a_data->ats_data.ta_1 = NFC_PRESENTATION_ATS_TA1;
    iso14443_4a_data->ats_data.tb_1 = NFC_PRESENTATION_ATS_TB1;
    iso14443_4a_data->ats_data.tc_1 = NFC_PRESENTATION_ATS_TC1;
    Iso14443_3aData* iso14443_3a_data = iso14443_4a_get_base_data(iso14443_4a_data);
    memcpy(iso14443_3a_data->atqa, nfc_presentation_atqa, sizeof(nfc_presentation_atqa));
    iso14443_3a_data->sak = NFC_PRESENTATION_SAK_ISO14443_4;

    simple_array_init(presentation->tag_data->ndef_data, (uint32_t)message->length);
    memcpy(simple_array_get_data(presentation->tag_data->ndef_data), message->bytes, message->length);

    presentation->listener = nfc_listener_alloc(presentation->nfc, NfcProtocolType4Tag, presentation->tag_data);
    furi_check(presentation->listener != NULL);
    nfc_listener_start(presentation->listener, nfc_presentation_listener_callback, callback_context);

    presentation->presented_message = *message;
    presentation->presenting = true;
}

static void nfc_presentation_update(StopBathRemoteApplication* remote_application, const RemoteNdefMessage* message) {
    NfcPresentation* presentation = &remote_application->nfc_presentation;
    if(message == NULL) {
        nfc_presentation_stop(presentation);
        return;
    }
    bool unchanged = presentation->presenting && presentation->presented_message.length == message->length &&
                     memcmp(presentation->presented_message.bytes, message->bytes, message->length) == 0;
    if(unchanged) {
        return;
    }
    nfc_presentation_stop(presentation);
    nfc_presentation_start(presentation, message, remote_application);
}

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

static void replay_layout(Canvas* canvas, const RemoteDisplayLayout* layout, const RemoteQrBitmap* code_bitmap) {
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
        if(code_bitmap != NULL) {
            canvas_draw_xbm(canvas, layout->qr_x, layout->qr_y, (size_t)layout->qr_size, (size_t)layout->qr_size, code_bitmap->bits);
        } else {
            canvas_draw_frame(canvas, layout->qr_x, layout->qr_y, (size_t)layout->qr_size, (size_t)layout->qr_size);
        }
    }

    for(int text_index = 0; text_index < layout->text_count; text_index++) {
        const RemoteLayoutText* text = &layout->texts[text_index];
        canvas_set_font(canvas, canvas_font_for(text->font));
        canvas_set_color(canvas, text->inverted ? ColorWhite : ColorBlack);
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
    if(furi_mutex_acquire(remote_application->layout_mutex, FuriWaitForever) != FuriStatusOk) {
        return;
    }
    replay_layout(
        canvas,
        &remote_application->composed_layout,
        remote_application->code_bitmap_valid ? &remote_application->code_bitmap : NULL);
    furi_mutex_release(remote_application->layout_mutex);
}

/* Takes the display state from the session, renders the page's code and NFC,
 * composes the layout, and publishes it for the GUI thread. Runs on the main
 * loop's stack because the GUI service thread's stack is small. */
static void recompose_screen(StopBathRemoteApplication* remote_application) {
    remote_session_display(&remote_application->session, &remote_application->display_state);

    RemoteDisplayLayout layout;
    remote_display_layout_compose(&remote_application->display_state, &layout);

    /* The QR is drawn only when the payload fits the version 3 ceiling
     * (layout.qr_area_shown). NFC is presented on any active code page
     * (layout.code_page_active), fit or not, because NDEF has no such small
     * ceiling and a tap is the fallback for a payload too large to scan (FE5,
     * FE6). The two surfaces are therefore decided by two different flags. */
    RemoteQrBitmap code_bitmap;
    bool code_bitmap_valid = false;
    if(layout.qr_area_shown) {
        RemoteQrMatrix code_matrix;
        if(remote_qr_encode(remote_application->display_state.payload, &code_matrix)) {
            remote_qr_render_bitmap(&code_matrix, &code_bitmap);
            code_bitmap_valid = true;
        }
    }
    RemoteNdefMessage ndef_message;
    bool ndef_message_valid = false;
    if(layout.code_page_active) {
        if(remote_application->display_state.page == RemoteDisplayPageWifi) {
            ndef_message_valid = remote_ndef_build_wifi_message(remote_application->display_state.payload, &ndef_message);
        } else if(remote_application->display_state.page == RemoteDisplayPageGuest) {
            ndef_message_valid = remote_ndef_build_uri_message(remote_application->display_state.payload, &ndef_message);
        }
    }
    nfc_presentation_update(remote_application, ndef_message_valid ? &ndef_message : NULL);
    bool nfc_presenting = remote_application->nfc_presentation.presenting;
    if(remote_application->display_state.nfc_presenting != nfc_presenting) {
        remote_application->display_state.nfc_presenting = nfc_presenting;
        remote_display_layout_compose(&remote_application->display_state, &layout);
    }

    furi_check(furi_mutex_acquire(remote_application->layout_mutex, FuriWaitForever) == FuriStatusOk);
    remote_application->composed_layout = layout;
    remote_application->code_bitmap_valid = code_bitmap_valid;
    if(code_bitmap_valid) {
        remote_application->code_bitmap = code_bitmap;
    }
    furi_mutex_release(remote_application->layout_mutex);
}

static void enqueue_input_event(InputEvent* input_event, void* opaque_application_pointer) {
    StopBathRemoteApplication* remote_application = opaque_application_pointer;
    if(furi_message_queue_put(remote_application->input_event_queue, input_event, 0) != FuriStatusOk) {
        remote_application->dropped_input_events++;
    }
}

/*
 * Whether the application is in the foreground, which the guard in 2.4 needs.
 * On a Flipper FAP the loader runs exactly one application, and when the
 * desktop locks, input stops reaching this viewport entirely (the FD19
 * finding: lockdown routes input to the desktop layer only). So any input we
 * receive is by definition received while foregrounded, and there is no
 * background state to detect. This returns true and is the one place to
 * revisit if a future firmware gives a FAP a real background state.
 */
static bool application_is_foregrounded(void) {
    return true;
}

/* Applies one firmware input event: the lock and the reportable events go to
 * the session. Returns true when the application should exit. */
static bool apply_input_event(StopBathRemoteApplication* remote_application, const InputEvent* input_event) {
    int pressed_button = remote_button_for_input_key(input_event->key);
    int press_kind = remote_press_kind_for_input_type(input_event->type);

    RemoteInputOutcome outcome =
        remote_input_model_apply(&remote_application->input_model, pressed_button, press_kind);
    switch(outcome.kind) {
    case RemoteInputOutcomeScreenLocked:
    case RemoteInputOutcomeScreenUnlocked:
        remote_session_lock_changed(
            &remote_application->session,
            remote_input_model_is_screen_locked(&remote_application->input_model));
        break;
    case RemoteInputOutcomeReportableEvent:
        /* Reported to the appliance, which decides what it means (2.1). The
         * session drops it unless connected, foregrounded and unlocked. */
        remote_session_report_event(
            &remote_application->session, outcome.reportable_event, application_is_foregrounded());
        break;
    case RemoteInputOutcomeExitRequested:
        return true;
    case RemoteInputOutcomeIgnored:
    case RemoteInputOutcomeSuppressedByLock:
        break;
    }
    return false;
}

static const char* link_state_name(RemoteSessionLinkState link_state) {
    switch(link_state) {
    case RemoteSessionLinkDown:
        return "link-down";
    case RemoteSessionHandshaking:
        return "handshaking";
    case RemoteSessionConnected:
        return "connected";
    case RemoteSessionIncompatible:
        return "incompatible";
    }
    return "unknown";
}

/*
 * Emits a line to the firmware log whenever something the operator would want
 * to see changes: the link state, the handshake retrying, the display record
 * the appliance sent (its status and error code, so a rejection like ACTIVE or
 * BAD_VALUE is visible), and the diagnostic counters. Read live on a
 * development machine with the firmware `log` command over channel 0, which
 * this application leaves the command line on (docs/DIAGNOSTICS.md). Logs only
 * on change, so it is quiet when nothing is happening.
 */
static void trace_diagnostics(StopBathRemoteApplication* remote_application) {
    RemoteSession* session = &remote_application->session;

    if(session->link_state != remote_application->traced_link_state) {
        FURI_LOG_I(
            TAG,
            "link %s -> %s",
            link_state_name(remote_application->traced_link_state),
            link_state_name(session->link_state));
        remote_application->traced_link_state = session->link_state;
    }
    if(session->handshake_retries != remote_application->traced_handshake_retries) {
        FURI_LOG_W(
            TAG,
            "handshake retry #%lu: no DISPLAY acceptance within %dms, resending HELLO",
            (unsigned long)session->handshake_retries,
            REMOTE_SESSION_HANDSHAKE_RETRY_INTERVAL_MILLISECONDS);
        remote_application->traced_handshake_retries = session->handshake_retries;
    }
    if(session->reconnections != remote_application->traced_reconnections) {
        FURI_LOG_I(TAG, "reconnections=%lu", (unsigned long)session->reconnections);
        remote_application->traced_reconnections = session->reconnections;
    }
    if(session->malformed_received != remote_application->traced_malformed) {
        FURI_LOG_W(
            TAG,
            "malformed or unexpected from appliance total=%lu",
            (unsigned long)session->malformed_received);
        remote_application->traced_malformed = session->malformed_received;
    }
    if(session->events_dropped_by_guard != remote_application->traced_guard_drops) {
        FURI_LOG_I(
            TAG,
            "button dropped by guard total=%lu",
            (unsigned long)session->events_dropped_by_guard);
        remote_application->traced_guard_drops = session->events_dropped_by_guard;
    }
    if(session->events_dropped_by_output_full != remote_application->traced_output_drops) {
        FURI_LOG_W(
            TAG,
            "button dropped, outbound queue full total=%lu",
            (unsigned long)session->events_dropped_by_output_full);
        remote_application->traced_output_drops = session->events_dropped_by_output_full;
    }

    /* The record the appliance sent, while connected: its status and any error
     * code, which is where a rejection the operator sees on screen shows up. */
    if(session->link_state == RemoteSessionConnected) {
        int status = remote_application->display_state.status;
        const char* error_code = remote_application->display_state.error_code;
        if(status != remote_application->traced_status ||
           strcmp(error_code, remote_application->traced_error_code) != 0) {
            if(error_code[0] != '\0') {
                FURI_LOG_W(TAG, "DISPLAY status=%d error=%s", status, error_code);
            } else {
                FURI_LOG_I(TAG, "DISPLAY status=%d", status);
            }
            remote_application->traced_status = status;
            snprintf(
                remote_application->traced_error_code,
                sizeof(remote_application->traced_error_code),
                "%s",
                error_code);
        }
    }
}

int32_t stopbath_remote_main(void* launch_arguments) {
    UNUSED(launch_arguments);

    static StopBathRemoteApplication remote_application;
    memset(&remote_application, 0, sizeof(remote_application));
    remote_input_model_initialise(&remote_application.input_model);
    remote_session_initialise(&remote_application.session, PERIPHERAL_TOKEN);
    remote_display_state_initialise(&remote_application.display_state);

    remote_application.input_event_queue =
        furi_message_queue_alloc(INPUT_EVENT_QUEUE_DEPTH, sizeof(InputEvent));
    furi_check(remote_application.input_event_queue != NULL);
    remote_application.layout_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_check(remote_application.layout_mutex != NULL);
    remote_application.nfc_presentation.nfc = nfc_alloc();
    furi_check(remote_application.nfc_presentation.nfc != NULL);
    remote_application.transport = remote_transport_alloc(&remote_application.session);
    furi_check(remote_application.transport != NULL);

    ViewPort* view_port = view_port_alloc();
    furi_check(view_port != NULL);
    view_port_draw_callback_set(view_port, draw_screen, &remote_application);
    view_port_input_callback_set(view_port, enqueue_input_event, &remote_application);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    /* Open the link. If the USB mode is locked (an RPC session or a desktop
     * PIN), the link cannot open now; the application still runs and shows not
     * connected, and the loop retries until the lock clears. */
    bool link_opened = remote_transport_open(remote_application.transport);
    if(!link_opened) {
        FURI_LOG_W("StopBathRemote", "USB mode locked; will retry opening the link");
    }
    uint32_t next_open_attempt_tick = furi_get_tick() + furi_ms_to_ticks(LINK_OPEN_RETRY_INTERVAL_MILLISECONDS);

    recompose_screen(&remote_application);

    const uint32_t tick_frequency = furi_kernel_get_tick_frequency();
    uint32_t last_service_tick = furi_get_tick();
    bool exit_requested = false;
    while(!exit_requested) {
        if(!link_opened && furi_get_tick() >= next_open_attempt_tick) {
            link_opened = remote_transport_open(remote_application.transport);
            next_open_attempt_tick = furi_get_tick() + furi_ms_to_ticks(LINK_OPEN_RETRY_INTERVAL_MILLISECONDS);
        }

        /* Advance the handshake retry clock by the real time elapsed, so a lost
         * DISPLAY acceptance is retried rather than hung on. Done before the
         * service so a retry's HELLO is sent this pass. */
        uint32_t now_tick = furi_get_tick();
        uint32_t elapsed_milliseconds =
            (uint32_t)(((uint64_t)(now_tick - last_service_tick) * 1000u) / tick_frequency);
        last_service_tick = now_tick;
        remote_session_tick(&remote_application.session, elapsed_milliseconds);

        remote_transport_service(remote_application.transport);

        InputEvent input_event;
        FuriStatus queue_status = furi_message_queue_get(
            remote_application.input_event_queue, &input_event, MAIN_LOOP_SERVICE_INTERVAL_MILLISECONDS);
        if(queue_status == FuriStatusOk) {
            exit_requested = apply_input_event(&remote_application, &input_event);
        } else if(queue_status != FuriStatusErrorTimeout) {
            furi_crash("input queue failed");
        }

        recompose_screen(&remote_application);
        trace_diagnostics(&remote_application);
        view_port_update(view_port);
    }

    /* Release in reverse order. Closing the transport tells the session the
     * port is closing, which clears the presented credential; stopping the
     * NFC surface clears the NDEF copy. */
    remote_transport_close(remote_application.transport);
    remote_transport_free(remote_application.transport);
    nfc_presentation_stop(&remote_application.nfc_presentation);
    nfc_free(remote_application.nfc_presentation.nfc);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_mutex_free(remote_application.layout_mutex);
    furi_message_queue_free(remote_application.input_event_queue);

    return 0;
}
