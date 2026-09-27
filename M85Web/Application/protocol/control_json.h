#ifndef CONTROL_JSON_H
#define CONTROL_JSON_H
#include "control_protocol.h"
#include <stddef.h>
#define CONTROL_JSON_LIMIT 512u
bool control_json_decode(const char *data, size_t len, control_request_t *out);
bool control_json_encode(const control_response_t *state, char *data, size_t capacity, size_t *len);
#endif
