#include "control_json.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
typedef struct
{
    const char *p, *end;
} parser_t;
static void ws(parser_t *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n'))
        ++j->p;
}
static bool take(parser_t *j, char c)
{
    ws(j);
    if (j->p == j->end || *j->p != c)
        return false;
    ++j->p;
    return true;
}
static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static bool unicode(parser_t *j, unsigned *v)
{
    *v = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (j->p == j->end)
            return false;
        int h = hex(*j->p++);
        if (h < 0)
            return false;
        *v = (*v << 4) | (unsigned)h;
    }
    return true;
}
/* Unknown strings/keys may be arbitrarily long within the payload. Overflow
 * marks a key as unknown; enum strings must fit. Escaped ASCII is decoded. */
static bool string(parser_t *j, char *out, size_t cap, bool *fit)
{
    size_t n = 0;
    bool fits = true;
    if (!take(j, '"'))
        return false;
    while (j->p < j->end)
    {
        unsigned c = (unsigned char)*j->p++;
        if (c == '"')
        {
            if (out && cap)
                out[n < cap ? n : cap - 1] = 0;
            if (fit)
                *fit = fits;
            return true;
        }
        if (c < 32)
            return false;
        if (c == '\\')
        {
            if (j->p == j->end)
                return false;
            c = (unsigned char)*j->p++;
            switch (c)
            {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                c = 8;
                break;
            case 'f':
                c = 12;
                break;
            case 'n':
                c = 10;
                break;
            case 'r':
                c = 13;
                break;
            case 't':
                c = 9;
                break;
            case 'u':
            {
                if (!unicode(j, &c))
                    return false;
                if (c >= 0xd800 && c <= 0xdbff)
                {
                    unsigned low;
                    if (j->end - j->p < 2 || j->p[0] != '\\' || j->p[1] != 'u')
                        return false;
                    j->p += 2;
                    if (!unicode(j, &low) || low < 0xdc00 || low > 0xdfff)
                        return false;
                    c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                }
                else if (c >= 0xdc00 && c <= 0xdfff)
                    return false;
                break;
            }
            default:
                return false;
            }
        }
        else if (c >= 128)
        {
            unsigned extra, minimum;
            if (c >= 0xc2 && c <= 0xdf)
            {
                extra = 1;
                minimum = 0x80;
                c &= 0x1f;
            }
            else if (c >= 0xe0 && c <= 0xef)
            {
                extra = 2;
                minimum = 0x800;
                c &= 0x0f;
            }
            else if (c >= 0xf0 && c <= 0xf4)
            {
                extra = 3;
                minimum = 0x10000;
                c &= 0x07;
            }
            else
                return false;
            for (unsigned i = 0; i < extra; ++i)
            {
                if (j->p == j->end)
                    return false;
                unsigned next = (unsigned char)*j->p++;
                if ((next & 0xc0) != 0x80)
                    return false;
                c = (c << 6) | (next & 0x3f);
            }
            if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
                return false;
        }
        if (out)
        {
            if (c == 0 || c > 127 || n + 1 >= cap)
                fits = false;
            if (n + 1 < cap)
                out[n] = (char)(c <= 127 ? c : 127);
        }
        ++n;
    }
    return false;
}
static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool number(parser_t *j, float *out)
{
    const char *start;
    ws(j);
    start = j->p;
    if (j->p < j->end && *j->p == '-')
        ++j->p;
    if (j->p == j->end)
        return false;
    if (*j->p == '0')
        ++j->p;
    else
    {
        if (*j->p < '1' || *j->p > '9')
            return false;
        while (j->p < j->end && digit(*j->p))
            ++j->p;
    }
    if (j->p < j->end && *j->p == '.')
    {
        ++j->p;
        if (j->p == j->end || !digit(*j->p))
            return false;
        while (j->p < j->end && digit(*j->p))
            ++j->p;
    }
    if (j->p < j->end && (*j->p == 'e' || *j->p == 'E'))
    {
        ++j->p;
        if (j->p < j->end && (*j->p == '+' || *j->p == '-'))
            ++j->p;
        if (j->p == j->end || !digit(*j->p))
            return false;
        while (j->p < j->end && digit(*j->p))
            ++j->p;
    }
    if (out)
    {
        /* Convert with bounded arithmetic. newlib strtod may allocate internal
         * big-integer buffers, so it is deliberately not used by this parser. */
        const char *p = start;
        bool negative = *p == '-';
        if (negative)
            ++p;
        double mantissa = 0;
        unsigned significant = 0, discarded = 0, fractional = 0;
        bool fraction = false;
        while (p < j->p && *p != 'e' && *p != 'E')
        {
            if (*p == '.')
            {
                fraction = true;
                ++p;
                continue;
            }
            unsigned d = (unsigned)(*p++ - '0');
            if (fraction)
                ++fractional;
            if (significant == 0 && d == 0)
                continue;
            if (significant < 17)
            {
                mantissa = mantissa * 10 + d;
                ++significant;
            }
            else
                ++discarded;
        }
        int exponent = 0;
        if (p < j->p)
        {
            ++p;
            bool minus = p < j->p && *p == '-';
            if (p < j->p && (*p == '-' || *p == '+'))
                ++p;
            while (p < j->p)
            {
                unsigned d = (unsigned)(*p++ - '0');
                if (exponent < 10000)
                    exponent = exponent * 10 + (int)d;
            }
            if (minus)
                exponent = -exponent;
        }
        exponent += (int)discarded - (int)fractional;
        double v = mantissa;
        if (mantissa != 0)
        {
            if (exponent > 400)
                return false;
            if (exponent < -400)
                v = 0;
            else if (exponent >= 0)
            {
                while (exponent--)
                    v *= 10;
            }
            else
            {
                while (exponent++)
                    v /= 10;
            }
        }
        if (negative)
            v = -v;
        /* The reference command range is -1..1; no control decisions here. */
        if (v < -1.0 || v > 1.0)
            return false;
        *out = (float)v;
    }
    return true;
}
static bool literal(parser_t *j, const char *word)
{
    size_t n = strlen(word);
    ws(j);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, word, n))
        return false;
    j->p += n;
    return true;
}
static bool boolean(parser_t *j, bool *out)
{
    if (literal(j, "true"))
    {
        *out = true;
        return true;
    }
    if (literal(j, "false"))
    {
        *out = false;
        return true;
    }
    return false;
}
static bool skip(parser_t *j, unsigned depth)
{
    ws(j);
    if (j->p == j->end || depth > 16)
        return false;
    char c = *j->p;
    if (c == '"')
        return string(j, NULL, 0, NULL);
    if (c == '{' || c == '[')
    {
        ++j->p;
        char close = c == '{' ? '}' : ']';
        ws(j);
        if (j->p < j->end && *j->p == close)
        {
            ++j->p;
            return true;
        }
        do
        {
            if (c == '{' && (!string(j, NULL, 0, NULL) || !take(j, ':')))
                return false;
            if (!skip(j, depth + 1))
                return false;
            ws(j);
            if (j->p < j->end && *j->p == close)
            {
                ++j->p;
                return true;
            }
        } while (take(j, ','));
        return false;
    }
    if (c == 't')
        return literal(j, "true");
    if (c == 'f')
        return literal(j, "false");
    if (c == 'n')
        return literal(j, "null");
    return number(j, NULL);
}
static bool enumeration(parser_t *j, const char *const *names, size_t count, int *out)
{
    char value[32];
    bool fit;
    if (!string(j, value, sizeof(value), &fit) || !fit)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(value, names[i]))
        {
            *out = (int)i;
            return true;
        }
    return false;
}
bool control_json_decode(const char *data, size_t len, control_request_t *out)
{
    /* Every key is required exactly once; order matches the switch below. */
    enum
    {
        KEY_CLIENT_MODE,
        KEY_THROTTLE,
        KEY_STEERING,
        KEY_MODE_REQUEST,
        KEY_DEADMAN,
        KEY_ESTOP_REQUEST,
        KEY_MANUAL_ABORT_REQUEST,
        KEY_RESET_ABORT_REQUEST,
        KEY_COUNT
    };
    static const char *const keys[KEY_COUNT] = {
        "client_mode",          "throttle",           "steering", "mode_request",
        "deadman",              "estop_request",       "manual_abort_request",
        "reset_abort_request"};
    const unsigned all_keys = (1u << KEY_COUNT) - 1u;
    control_request_t r = {0};
    unsigned seen = 0;
    parser_t j = {data, data};
    if (!data || !out || len > CONTROL_JSON_LIMIT)
        return false;
    j.end = data + len;
    if (!take(&j, '{'))
        return false;
    ws(&j);
    if (j.p < j.end && *j.p == '}')
        return false;
    do
    {
        char key[32];
        bool fit;
        int field = -1, v;
        if (!string(&j, key, sizeof(key), &fit) || !take(&j, ':'))
            return false;
        if (fit)
            for (int i = 0; i < KEY_COUNT; ++i)
                if (!strcmp(key, keys[i]))
                {
                    field = i;
                    break;
                }
        if (field >= 0)
        {
            if (seen & (1u << field))
                return false;
            seen |= 1u << field;
        }
        switch (field)
        {
        case KEY_CLIENT_MODE:
            if (!enumeration(&j, control_modes, VEHICLE_STATE_COUNT, &v))
                return false;
            r.client_mode = (vehicle_state_t)v;
            break;
        case KEY_THROTTLE:
            if (!number(&j, &r.throttle))
                return false;
            break;
        case KEY_STEERING:
            if (!number(&j, &r.steering))
                return false;
            break;
        case KEY_MODE_REQUEST:
            if (!enumeration(&j, control_requests, MODE_REQUEST_COUNT, &v))
                return false;
            r.mode_request = (mode_request_t)v;
            break;
        case KEY_DEADMAN:
            if (!boolean(&j, &r.deadman))
                return false;
            break;
        case KEY_ESTOP_REQUEST:
            if (!boolean(&j, &r.estop_request))
                return false;
            break;
        case KEY_MANUAL_ABORT_REQUEST:
            if (!boolean(&j, &r.manual_abort_request))
                return false;
            break;
        case KEY_RESET_ABORT_REQUEST:
            if (!boolean(&j, &r.reset_abort_request))
                return false;
            break;
        default:
            if (!skip(&j, 0))
                return false;
        }
        ws(&j);
        if (j.p < j.end && *j.p == '}')
        {
            ++j.p;
            ws(&j);
            if (j.p != j.end || seen != all_keys)
                return false;
            *out = r;
            return true;
        }
    } while (take(&j, ','));
    return false;
}
bool control_json_encode(const control_response_t *s, char *data, size_t cap, size_t *len)
{
    if (!s || !data || !len || !cap || (unsigned)s->mode >= VEHICLE_STATE_COUNT ||
        (unsigned)s->stop_reason >= STOP_REASON_COUNT ||
        (unsigned)s->request_reject_reason >= REQUEST_REJECT_COUNT)
        return false;
    int n = snprintf(data, cap,
                     "{\"mode\":\"%s\",\"front_distance_mm\":%" PRId32
                     ",\"armed\":%s,\"web_seq\":%" PRIu32
                     ",\"tor_active\":%s,\"tor_remaining_ms\":%" PRIu32
                     ",\"stop_reason\":\"%s\",\"request_reject_reason\":\"%s\"}",
                     control_modes[s->mode], s->front_distance_mm, s->armed ? "true" : "false",
                     s->web_seq, s->tor_active ? "true" : "false", s->tor_remaining_ms,
                     control_stops[s->stop_reason],
                     control_rejects[s->request_reject_reason]);
    if (n < 0 || (size_t)n >= cap || (size_t)n > CONTROL_JSON_LIMIT)
        return false;
    *len = (size_t)n;
    return true;
}
