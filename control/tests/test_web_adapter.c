#include "ipc_gateway.h"
#include "web_control_adapter.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int sem_held;
static int take_sem(void *context)
{
    (void) context;
    if (sem_held) return -1;
    sem_held = 1;
    return 0;
}
static void give_sem(void *context) { (void) context; sem_held = 0; }
static void barrier(void *context) { (void) context; }

static control_request_t drive(void)
{
    control_request_t request = {0};
    request.throttle = 0.2F;
    request.deadman = true;
    return request;
}

/* Same path as m85_gateway_runtime_submit_web(): translate, then queue. */
static int submit(ipc_gateway_t *gateway, const control_request_t *request)
{
    vc_web_t command;
    if (web_adapter_request_to_command(request, &command)) return -1;
    return ipc_gateway_submit_web(gateway, &command);
}

static void assert_stop_slot(ipc_endpoint_t *endpoint, uint32_t action)
{
    ipc_packet_t packet;
    vc_web_t command;
    assert(ipc_read(endpoint, IPC_STOP, &packet) == IPC_OK);
    assert(ipc_unpack_web(&packet, &command) == 0);
    assert(command.action == action);
    assert(command.deadman == 0U && command.linear == 0 && command.steering == 0);
}

static void test_request_to_command(void)
{
    control_request_t request;
    vc_web_t command;

    /* Drive: throttle/steering become rounded permille. */
    request = drive();
    request.throttle = 0.5F;
    request.steering = -0.2504F;
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_DRIVE && command.mode == VC_MODE_MANUAL);
    assert(command.deadman == 1U && command.linear == 500 && command.steering == -250);

    /* Out-of-range and non-finite inputs are rejected, not clamped. */
    request = drive();
    request.throttle = 1.01F;
    assert(web_adapter_request_to_command(&request, &command) == -1);
    request = drive();
    request.steering = NAN;
    assert(web_adapter_request_to_command(&request, &command) == -1);
    request = drive();
    request.client_mode = VEHICLE_STATE_COUNT;
    assert(web_adapter_request_to_command(&request, &command) == -1);
    request = drive();
    request.mode_request = MODE_REQUEST_COUNT;
    assert(web_adapter_request_to_command(&request, &command) == -1);

    /* ESTOP outranks STOP; neither carries motion. */
    request = drive();
    request.estop_request = true;
    request.manual_abort_request = true;
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_ESTOP && command.linear == 0 && command.deadman == 0U);

    /* A client_mode that only mirrors the M33 abort state is not a new STOP:
     * it becomes a neutral DRIVE keep-alive, whatever the stick says. */
    request = drive();
    request.client_mode = 2U; /* VEHICLE_AUTO_ABORT */
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_DRIVE && command.mode == VC_MODE_AUTO);
    assert(command.deadman == 0U && command.linear == 0 && command.steering == 0);
    request.client_mode = VEHICLE_MANUAL_ABORT;
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_DRIVE && command.mode == VC_MODE_MANUAL);
    assert(command.deadman == 0U && command.linear == 0);
    /* reset_abort from the abort screen is still an explicit STOP. */
    request.reset_abort_request = true;
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_STOP);

    /* Mode switch requests: the explicit request wins over client_mode. */
    request = (control_request_t){0};
    request.client_mode = VEHICLE_AUTO;
    request.mode_request = MODE_REQUEST_MANUAL;
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_MODE && command.mode == VC_MODE_MANUAL);
    request.client_mode = VEHICLE_MANUAL;
    request.mode_request = MODE_REQUEST_AUTO;
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_MODE && command.mode == VC_MODE_AUTO);
    request.mode_request = MODE_REQUEST_NONE;
    request.client_mode = VEHICLE_AUTO;
    assert(web_adapter_request_to_command(&request, &command) == 0);
    assert(command.action == VC_WEB_DRIVE && command.mode == VC_MODE_AUTO);
}

static void test_status_to_response(void)
{
    vc_status_t status = {0};
    control_response_t response;

    status.state = VC_STOPPED;
    status.mode = VC_MODE_AUTO;
    status.reason = VC_WAITING;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_AUTO && response.front_distance_mm == -1);
    assert(response.stop_reason == STOP_REASON_NONE && !response.tor_active);
    assert(response.request_reject_reason == REQUEST_REJECT_NONE);

    /* A refused AUTO start is reported explicitly instead of leaving the UI
     * in AUTO_PENDING until its timeout. */
    status.mode = VC_MODE_MANUAL;
    status.state = VC_STOPPED;
    status.reason = VC_AUTO_REFUSED_PATH;
    status.web_seq = 11U;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_MANUAL);
    assert(response.request_reject_reason == REQUEST_REJECT_PATH_NOT_READY);
    assert(response.stop_reason == STOP_REASON_NONE);
    assert(!response.armed && response.web_seq == 11U);
    /* Each refused prerequisite has its own wire reason. */
    status.reason = VC_AUTO_REFUSED_TOF;
    web_adapter_status_to_response(&status, &response);
    assert(response.request_reject_reason == REQUEST_REJECT_SENSOR_NOT_READY);
    assert(response.stop_reason == STOP_REASON_NONE);
    status.reason = VC_AUTO_REFUSED_LINK;
    web_adapter_status_to_response(&status, &response);
    assert(response.request_reject_reason == REQUEST_REJECT_LINK_NOT_READY);
    assert(response.stop_reason == STOP_REASON_NONE);
    /* A real running link/ToF stop is still a stop, not a refusal. */
    status.reason = VC_LINK;
    web_adapter_status_to_response(&status, &response);
    assert(response.request_reject_reason == REQUEST_REJECT_NONE);
    assert(response.stop_reason == STOP_REASON_COMM_TIMEOUT);

    status.mode = VC_MODE_MANUAL;
    status.reason = VC_OPERATOR;
    status.tof_valid = 1U;
    status.tof_mm = 480U;
    status.reason = VC_LINK;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_MANUAL && response.front_distance_mm == 480);
    assert(response.stop_reason == STOP_REASON_COMM_TIMEOUT);

    status.state = VC_TOR;
    status.armed = 1U;
    status.web_seq = 12U;
    status.reason = VC_AI;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_AUTO && response.tor_active);
    assert(response.stop_reason == STOP_REASON_ROAD_UNAVAILABLE);
    assert(response.armed && response.web_seq == 12U);

    status.state = VC_EMERGENCY;
    status.reason = VC_TOF;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_MANUAL_ABORT && !response.tor_active);
    assert(response.stop_reason == STOP_REASON_SENSOR_ERROR);

    status.reason = VC_OPERATOR;
    web_adapter_status_to_response(&status, &response);
    assert(response.stop_reason == STOP_REASON_MANUAL_ABORT_BUTTON);
    assert(response.request_reject_reason == REQUEST_REJECT_NONE);

    /* Distinct stop causes (2026-09-27): distance pre-stop / emergency,
     * button ESTOP, AI obstacle. */
    status.reason = VC_TOF_NEAR;
    web_adapter_status_to_response(&status, &response);
    assert(response.stop_reason == STOP_REASON_DISTANCE_EMERGENCY);
    status.reason = VC_TOF_PRESTOP;
    web_adapter_status_to_response(&status, &response);
    assert(response.stop_reason == STOP_REASON_DISTANCE_PRESTOP);
    status.reason = VC_ESTOP;
    web_adapter_status_to_response(&status, &response);
    assert(response.stop_reason == STOP_REASON_BUTTON_EMERGENCY);
    status.reason = VC_AI_OBSTACLE;
    web_adapter_status_to_response(&status, &response);
    assert(response.stop_reason == STOP_REASON_AI_OBSTACLE);
    assert(response.request_reject_reason == REQUEST_REJECT_NONE);
    /* 2026-09-28: a close person/car in AUTO is a TOR that names the cause. */
    status.state = VC_TOR; status.mode = VC_MODE_AUTO; status.reason = VC_TOR_OBSTACLE;
    web_adapter_status_to_response(&status, &response);
    assert(response.tor_active && response.mode == VEHICLE_AUTO);
    assert(response.stop_reason == STOP_REASON_AI_OBSTACLE);
    assert(!response.obstacle_alarm && response.obstacle_kind == OBSTACLE_KIND_NONE);

    /* Person/car alarm word from the M85 camera task. */
    web_adapter_obstacle_alarm((3U << AUTONOMY_ALARM_SEQ_SHIFT) | AUTONOMY_ALARM_ACTIVE |
                               AUTONOMY_ALARM_PERSON, &response);
    assert(response.obstacle_alarm && response.obstacle_kind == OBSTACLE_KIND_PERSON &&
           response.alarm_seq == 3U);
    web_adapter_obstacle_alarm((4U << AUTONOMY_ALARM_SEQ_SHIFT) | AUTONOMY_ALARM_ACTIVE |
                               AUTONOMY_ALARM_PERSON | AUTONOMY_ALARM_CAR, &response);
    assert(response.obstacle_kind == OBSTACLE_KIND_PERSON_AND_CAR && response.alarm_seq == 4U);
    web_adapter_obstacle_alarm(4U << AUTONOMY_ALARM_SEQ_SHIFT, &response);
    assert(!response.obstacle_alarm && response.obstacle_kind == OBSTACLE_KIND_NONE &&
           response.alarm_seq == 4U);
}

static void test_gateway(void)
{
    ipc_shared_t shared;
    ipc_endpoint_t endpoint;
    ipc_gateway_t gateway;
    control_request_t request;
    ipc_packet_t packet;
    vc_web_t command;
    memset(&shared, 0, sizeof(shared));
    endpoint.ram = &shared;
    endpoint.sync.ctx = NULL;
    endpoint.sync.try_lock = take_sem;
    endpoint.sync.unlock = give_sem;
    endpoint.sync.barrier = barrier;
    assert(ipc_control_init(&endpoint, 0x42U, 0U) == 0);
    assert(ipc_gateway_init(&gateway, &endpoint) == 0);

    /* A web reset is STOP-only and remains urgent when a later DRIVE arrives. */
    request = drive();
    request.reset_abort_request = true;
    assert(submit(&gateway, &request) == 0);
    request = drive();
    assert(submit(&gateway, &request) == 0);
    assert(ipc_gateway_step(&gateway, 10U) == 0);
    assert_stop_slot(&endpoint, VC_WEB_STOP);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    /* An explicit manual abort is urgent too, even if a later DRIVE arrives. */
    request = drive();
    request.manual_abort_request = true;
    assert(submit(&gateway, &request) == 0);
    request = drive();
    assert(submit(&gateway, &request) == 0);
    assert(ipc_gateway_step(&gateway, 20U) == 0);
    assert_stop_slot(&endpoint, VC_WEB_STOP);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    /* ESTOP is not downgraded by a later STOP while it is still pending. */
    request = drive();
    request.estop_request = true;
    assert(submit(&gateway, &request) == 0);
    request = drive();
    request.manual_abort_request = true;
    assert(submit(&gateway, &request) == 0);
    assert(ipc_gateway_step(&gateway, 25U) == 0);
    assert_stop_slot(&endpoint, VC_WEB_ESTOP);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    /* STOP is a barrier: even a newer neutral Web input cannot be forwarded
     * until a fresh M33 status proves outputs are disarmed and zero. */
    request = drive();
    request.client_mode = VEHICLE_MANUAL_ABORT;
    assert(submit(&gateway, &request) == 0);
    assert(ipc_gateway_step(&gateway, 30U) == 0);
    assert(ipc_read(&endpoint, IPC_STOP, &packet) == IPC_EMPTY);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    vc_status_t status = {0};
    vc_status_t delivered = {0};
    status.state = VC_STOPPED;
    status.control_ms = 25U; /* status from the same tick is not an ack */
    ipc_pack_status(&packet, 0x42U, 1U, &status);
    assert(ipc_write(&endpoint, &packet) == IPC_OK);
    assert(ipc_gateway_step(&gateway, 31U) == 0);
    assert(ipc_gateway_get_status(&gateway, &delivered) == IPC_OK);
    assert(delivered.control_ms == 25U);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    status.control_ms = 31U;
    status.armed = 1U; /* a later status with live output is not an ack */
    status.web_seq = gateway.stop_sent_seq;
    ipc_pack_status(&packet, 0x42U, 2U, &status);
    assert(ipc_write(&endpoint, &packet) == IPC_OK);
    assert(ipc_gateway_step(&gateway, 32U) == 0);
    assert(ipc_gateway_get_status(&gateway, &delivered) == IPC_OK);
    assert(delivered.armed == 1U);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    status.control_ms = 32U;
    status.armed = 0U;
    status.web_seq = gateway.stop_sent_seq - 1U; /* old command is not STOP ack */
    ipc_pack_status(&packet, 0x42U, 3U, &status);
    assert(ipc_write(&endpoint, &packet) == IPC_OK);
    assert(ipc_gateway_step(&gateway, 33U) == 0);
    assert(ipc_gateway_get_status(&gateway, &delivered) == IPC_OK);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    status.control_ms = 33U;
    status.web_seq = gateway.stop_sent_seq;
    ipc_pack_status(&packet, 0x42U, 4U, &status);
    assert(ipc_write(&endpoint, &packet) == IPC_OK);
    assert(ipc_gateway_step(&gateway, 34U) == 0);
    assert(ipc_gateway_get_status(&gateway, &delivered) == IPC_OK);
    assert(delivered.control_ms == 33U && delivered.armed == 0U);
    assert(ipc_gateway_get_status(&gateway, &delivered) == IPC_EMPTY);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);

    /* The pre-ack input was discarded.  Fresh abort-screen mirror input is
     * forwarded as a neutral keep-alive, never as another STOP. */
    assert(ipc_gateway_step(&gateway, 35U) == 0);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_EMPTY);
    assert(submit(&gateway, &request) == 0);
    assert(ipc_gateway_step(&gateway, 36U) == 0);
    assert(ipc_read(&endpoint, IPC_WEB, &packet) == IPC_OK);
    assert(ipc_unpack_web(&packet, &command) == 0);
    assert(command.action == VC_WEB_DRIVE && command.deadman == 0U && command.linear == 0);
    assert(command.timestamp_ms == 36U && command.seq != 0U); /* stamped at send */

    /* An invalid request never reaches the gateway. */
    request = drive();
    request.throttle = -2.0F;
    assert(submit(&gateway, &request) == -1);
}

static void test_tor(void)
{
    vc_status_t status = {0};
    control_response_t response;
    web_tor_tracker_t tracker = {false, 0U};

    /* TOR: still AUTO for the UI, tor_active, no stop reason. */
    status.state = VC_TOR; status.mode = VC_MODE_AUTO; status.reason = VC_TOR_REQUEST;
    status.control_ms = 1000U;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_AUTO && response.tor_active);
    assert(response.stop_reason == STOP_REASON_NONE && response.request_reject_reason == REQUEST_REJECT_NONE);
    /* Countdown from the M33 clock. */
    assert(web_adapter_tor_remaining_ms(&status, &tracker) == VC_TOR_TIMEOUT_MS);
    status.control_ms = 1000U + 1200U;
    assert(web_adapter_tor_remaining_ms(&status, &tracker) == VC_TOR_TIMEOUT_MS - 1200U);
    status.control_ms = 1000U + VC_TOR_TIMEOUT_MS + 50U;
    assert(web_adapter_tor_remaining_ms(&status, &tracker) == 0U);
    /* Leaving TOR resets; a new TOR starts a new countdown (clock wrap too). */
    status.state = VC_AUTO; assert(web_adapter_tor_remaining_ms(&status, &tracker) == 0U && !tracker.in_tor);
    status.state = VC_TOR; status.control_ms = 0xFFFFFF00U;
    assert(web_adapter_tor_remaining_ms(&status, &tracker) == VC_TOR_TIMEOUT_MS);
    status.control_ms = 0x00000100U; /* 512 ms later across the wrap */
    assert(web_adapter_tor_remaining_ms(&status, &tracker) == VC_TOR_TIMEOUT_MS - 512U);
    assert(web_adapter_tor_remaining_ms(NULL, &tracker) == 0U && web_adapter_tor_remaining_ms(&status, NULL) == 0U);

    /* TOR unanswered: AUTO_ABORT with TOR_TIMEOUT until the operator resets. */
    status.state = VC_STOPPED; status.mode = VC_MODE_MANUAL; status.reason = VC_TOR_TIMEOUT; status.armed = 0U;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_AUTO_ABORT && response.stop_reason == STOP_REASON_TOR_TIMEOUT);
    assert(!response.tor_active && !response.armed && response.request_reject_reason == REQUEST_REJECT_NONE);
    /* After the reset (Web STOP -> VC_OPERATOR) it is plain MANUAL again. */
    status.reason = VC_OPERATOR;
    web_adapter_status_to_response(&status, &response);
    assert(response.mode == VEHICLE_MANUAL && response.stop_reason == STOP_REASON_MANUAL_ABORT_BUTTON);
    printf("PASS web adapter TOR: tor_active, M33-clock countdown (wrap-safe, clamp, reset), TOR_TIMEOUT -> AUTO_ABORT until reset\n");
}

int main(void)
{
    test_request_to_command();
    test_status_to_response();
    test_gateway();
    test_tor();
    puts("PASS web adapter: request->command ranges, reset/manual abort retained over DRIVE and STOP-only, "
         "ESTOP not downgraded, abort mirror is neutral keep-alive, mode request precedence, status->response");
    return 0;
}
