/*
 * One fixture per display state worth looking at. Shared by the host tests
 * and by the FE2 application, which cycles through them on the device so the
 * author can judge each one in daylight (specification FE2, hardware gate).
 * Shared rather than duplicated per specification 0.7.
 *
 * Any code or payload in here is fixture text, not a wire value. The status
 * and error code sets are FE3's to settle.
 */
#pragma once

#include "remote_display_layout.h"

typedef struct {
    const char* fixture_name;
    RemoteDisplayState display_state;
} RemoteDisplayFixture;

/* Sentinel for the unknown status fixture: outside the enumeration on
 * purpose, so the fallback path is exercised. */
#define REMOTE_DISPLAY_FIXTURE_UNKNOWN_STATUS (RemoteDisplayStatusCount + 3)

/* Fixture text, not a wire value; see above. */
#define REMOTE_DISPLAY_FIXTURE_ERROR_CODE "ERROR_FIXTURE"

const RemoteDisplayFixture* remote_display_fixtures(void);
int remote_display_fixture_count(void);
