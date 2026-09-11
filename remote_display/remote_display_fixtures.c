#include "remote_display_fixtures.h"

/* The experiment credentials come from an untracked header when the author
 * has made one, and from the tracked example otherwise, so the tests and the
 * continuous integration build never need the real ones. */
#if __has_include("../experiment_credentials.h")
#include "../experiment_credentials.h"
#else
#include "../experiment_credentials.example.h"
#endif

/* The experiment payloads. The Wi-Fi grammar is the one StopBath's
 * BuildGuestJoinPayload produces, assembled here at compile time without
 * escaping (see experiment_credentials.example.h); the address is the bare
 * local gateway per D30, upper case so it encodes in alphanumeric mode and
 * fits version 1. Neither is a wire value. */
#define FIXTURE_WIFI_PAYLOAD  "WIFI:T:WPA;S:" EXPERIMENT_WIFI_SSID ";P:" EXPERIMENT_WIFI_PASSPHRASE ";;"
#define FIXTURE_GUEST_PAYLOAD "HTTP://192.168.72.1/"

static const RemoteDisplayFixture fixtures[] = {
    {"not connected",
     {.status = RemoteDisplayStatusPresenting,
      .page = RemoteDisplayPageWifi,
      .payload = FIXTURE_WIFI_PAYLOAD,
      .delivered_count = 2,
      .error_code = "",
      .link_connected = false,
      .screen_locked = false}},
    {"ready",
     {.status = RemoteDisplayStatusReady,
      .page = RemoteDisplayPageNone,
      .payload = "",
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"presenting wifi",
     {.status = RemoteDisplayStatusPresenting,
      .page = RemoteDisplayPageWifi,
      .payload = FIXTURE_WIFI_PAYLOAD,
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"presenting guest",
     {.status = RemoteDisplayStatusPresenting,
      .page = RemoteDisplayPageGuest,
      .payload = FIXTURE_GUEST_PAYLOAD,
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"guest connected wifi",
     {.status = RemoteDisplayStatusGuestConnected,
      .page = RemoteDisplayPageWifi,
      .payload = FIXTURE_WIFI_PAYLOAD,
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"guest connected guest three delivered",
     {.status = RemoteDisplayStatusGuestConnected,
      .page = RemoteDisplayPageGuest,
      .payload = FIXTURE_GUEST_PAYLOAD,
      .delivered_count = 3,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"terminating",
     {.status = RemoteDisplayStatusTerminating,
      .page = RemoteDisplayPageNone,
      .payload = "",
      .delivered_count = 3,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"recovery required",
     {.status = RemoteDisplayStatusRecoveryRequired,
      .page = RemoteDisplayPageNone,
      .payload = "",
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"ready with error",
     {.status = RemoteDisplayStatusReady,
      .page = RemoteDisplayPageNone,
      .payload = "",
      .delivered_count = 0,
      .error_code = REMOTE_DISPLAY_FIXTURE_ERROR_CODE,
      .link_connected = true,
      .screen_locked = false}},
    {"guest connected with error",
     {.status = RemoteDisplayStatusGuestConnected,
      .page = RemoteDisplayPageGuest,
      .payload = FIXTURE_GUEST_PAYLOAD,
      .delivered_count = 5,
      .error_code = REMOTE_DISPLAY_FIXTURE_ERROR_CODE,
      .link_connected = true,
      .screen_locked = false}},
    {"presenting wifi locked",
     {.status = RemoteDisplayStatusPresenting,
      .page = RemoteDisplayPageWifi,
      .payload = FIXTURE_WIFI_PAYLOAD,
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = true}},
    {"ready locked",
     {.status = RemoteDisplayStatusReady,
      .page = RemoteDisplayPageNone,
      .payload = "",
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = true}},
    {"unknown status",
     {.status = REMOTE_DISPLAY_FIXTURE_UNKNOWN_STATUS,
      .page = RemoteDisplayPageNone,
      .payload = "",
      .delivered_count = 0,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
    {"many delivered",
     {.status = RemoteDisplayStatusGuestConnected,
      .page = RemoteDisplayPageGuest,
      .payload = FIXTURE_GUEST_PAYLOAD,
      .delivered_count = 1234,
      .error_code = "",
      .link_connected = true,
      .screen_locked = false}},
};

const RemoteDisplayFixture* remote_display_fixtures(void) {
    return fixtures;
}

int remote_display_fixture_count(void) {
    return (int)(sizeof(fixtures) / sizeof(fixtures[0]));
}
