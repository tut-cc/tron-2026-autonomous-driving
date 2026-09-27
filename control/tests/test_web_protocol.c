/* Host test for the browser <-> M85 JSON codec (M85Web/Application/protocol). */
#include "control_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool decode(const char *json, control_request_t *out)
{
    return control_json_decode(json, strlen(json), out);
}

#define FULL(extra) \
    "{\"client_mode\":\"MANUAL\",\"throttle\":0.5,\"steering\":-0.25,\"mode_request\":\"NONE\"," \
    "\"deadman\":true,\"estop_request\":false,\"manual_abort_request\":false," \
    "\"reset_abort_request\":false" extra "}"

int main(void)
{
    control_request_t r;
    control_response_t s = {VEHICLE_AUTO_ABORT, 150, true, 0U, STOP_REASON_OBSTACLE,
                            REQUEST_REJECT_NONE, true, 42U};
    char out[CONTROL_JSON_LIMIT];
    size_t n;

    assert(decode(FULL(""), &r));
    assert(r.client_mode == VEHICLE_MANUAL && r.mode_request == MODE_REQUEST_NONE);
    assert(r.throttle == 0.5F && r.steering == -0.25F && r.deadman && !r.estop_request);
    /* Unknown keys are skipped. */
    assert(decode(FULL(",\"extra\":{\"a\":[1,2]}"), &r));
    /* Every known key is required exactly once. */
    assert(!decode("{\"client_mode\":\"MANUAL\"}", &r));
    assert(!decode(FULL(",\"deadman\":false"), &r));
    /* Unknown enum names are rejected. */
    assert(!decode("{\"client_mode\":\"REVERSE\",\"throttle\":0,\"steering\":0,"
                   "\"mode_request\":\"NONE\",\"deadman\":false,\"estop_request\":false,"
                   "\"manual_abort_request\":false,\"reset_abort_request\":false}", &r));
    /* Every wire name round-trips through the tables. */
    for (unsigned i = 0; i < VEHICLE_STATE_COUNT; ++i)
        assert(strcmp(control_vehicle_state_name((vehicle_state_t)i), control_modes[i]) == 0);
    assert(strcmp(control_stop_reason_name(STOP_REASON_COUNT), "UNKNOWN") == 0);
    /* AUTO refusal reasons added 2026-09-26 keep the existing wire values. */
    assert(strcmp(control_reject_reason_name(REQUEST_REJECT_SENSOR_NOT_READY), "SENSOR_NOT_READY") == 0);
    assert(strcmp(control_reject_reason_name(REQUEST_REJECT_MODE_MISMATCH), "MODE_MISMATCH") == 0);
    assert(strcmp(control_reject_reason_name(REQUEST_REJECT_LINK_NOT_READY), "LINK_NOT_READY") == 0);
    assert(strcmp(control_reject_reason_name(REQUEST_REJECT_PATH_NOT_READY), "PATH_NOT_READY") == 0);
    assert(strcmp(control_reject_reason_name(REQUEST_REJECT_COUNT), "UNKNOWN") == 0);

    assert(control_json_encode(&s, out, sizeof(out), &n));
    assert(n == strlen(out));
    assert(strcmp(out, "{\"mode\":\"AUTO_ABORT\",\"front_distance_mm\":150,\"armed\":true,"
                       "\"web_seq\":42,\"tor_active\":true,\"tor_remaining_ms\":0,"
                       "\"stop_reason\":\"OBSTACLE\","
                       "\"request_reject_reason\":\"NONE\"}") == 0);
    s.stop_reason = STOP_REASON_COUNT; /* out-of-range enum must not index the table */
    assert(!control_json_encode(&s, out, sizeof(out), &n));
    puts("PASS web protocol: JSON decode required/duplicate/unknown keys and enums, encode wire names and bounds");
    return 0;
}
