/* Bit-exactness of CPU0/src/obstacle_kernels.c against the previous in-line
 * implementation of obstacle_detector.cc (transcribed below as ref_*). */
#include "ai/obstacle_kernels.h"
#include "user_config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(e) do { if(!(e)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#e);exit(1);} } while(0)

static uint32_t rng = 12345U;
static uint32_t next_u32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---- reference: previous prepare_input ---------------------------------- */
static void ref_rgb(uint16_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{ *r=(uint8_t)(((p>>11)&0x1FU)*255U/31U); *g=(uint8_t)(((p>>5)&0x3FU)*255U/63U); *b=(uint8_t)((p&0x1FU)*255U/31U); }
static void ref_prepare(const uint16_t *px, uint16_t w, uint16_t h, uint16_t stride, int8_t *out, uint32_t bytes)
{
    od_letterbox_t box = od_letterbox(w, h);
    memset(out, 0x80, bytes);
    for (uint32_t my = 0; my < box.scaled_height; my++) {
        uint32_t sy = (my * (uint32_t)h) / box.scaled_height, oy = box.pad_y + my;
        for (uint32_t mx = 0; mx < box.scaled_width; mx++) {
            uint32_t sx = (mx * (uint32_t)w) / box.scaled_width; uint8_t r, g, b;
            ref_rgb(px[sy * stride + sx], &r, &g, &b);
            uint32_t oi = (oy * OD_INPUT_WIDTH + box.pad_x + mx) * OD_INPUT_CHANNELS;
            out[oi] = (int8_t)((int32_t)r - 128); out[oi+1] = (int8_t)((int32_t)g - 128); out[oi+2] = (int8_t)((int32_t)b - 128);
        }
    }
}

/* ---- reference: previous add_output_candidates ------------------------- */
static float ref_sig(float v) { return 1.0F / (1.0F + expf(-v)); }
static float ref_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float ref_deq(int8_t v, float s, int32_t z) { return ((int32_t)v - z) * s; }
static void ref_decode(const int8_t *d, int32_t rows, int32_t cols, float s, int32_t z, od_candidate_t *c, uint32_t *n)
{
    static const float SA[6] = {12.0F, 18.0F, 37.0F, 49.0F, 52.0F, 132.0F};
    static const float LA[6] = {115.0F, 73.0F, 119.0F, 199.0F, 242.0F, 238.0F};
    const float *an = (rows >= 20) ? SA : LA; float sx = 320.0F / (float)cols, sy = 320.0F / (float)rows;
    for (int32_t r = 0; r < rows; r++) for (int32_t col = 0; col < cols; col++) for (int32_t a = 0; a < 3; a++) {
        int32_t o = ((r * cols + col) * 255) + a * 85;
        float obj = ref_sig(ref_deq(d[o + 4], s, z)); float best = 0.0F; uint16_t id = 0;
        for (uint16_t k = 0; k < 80; k++) { float cs = ref_sig(ref_deq(d[o + 5 + k], s, z)); if (cs > best) { best = cs; id = k; } }
        float score = obj * best;
        if (id >= 4U || score < AI_OBSTACLE_SCORE_THRESHOLD) continue;
        float tx = ref_sig(ref_deq(d[o], s, z)), ty = ref_sig(ref_deq(d[o + 1], s, z));
        float tw = ref_clamp(ref_deq(d[o + 2], s, z), -10.0F, 10.0F), th = ref_clamp(ref_deq(d[o + 3], s, z), -10.0F, 10.0F);
        float cx = (tx + (float)col) * sx, cy = (ty + (float)r) * sy;
        float bw = expf(tw) * an[a * 2], bh = expf(th) * an[a * 2 + 1];
        od_candidate_t cand; memset(&cand, 0, sizeof(cand));
        cand.x = ref_clamp(cx - bw * 0.5F, 0.0F, 320.0F); cand.y = ref_clamp(cy - bh * 0.5F, 0.0F, 320.0F);
        cand.width = ref_clamp(bw, 0.0F, 320.0F - cand.x); cand.height = ref_clamp(bh, 0.0F, 320.0F - cand.y);
        cand.score = score; cand.class_id = id; cand.suppressed = false;
        od_add_candidate(&cand, c, n);
    }
}

static int same_candidates(const od_candidate_t *a, uint32_t na, const od_candidate_t *b, uint32_t nb)
{
    if (na != nb) return 0;
    for (uint32_t i = 0; i < na; i++)
        if (memcmp(&a[i].x, &b[i].x, 5 * sizeof(float)) || a[i].class_id != b[i].class_id || a[i].suppressed != b[i].suppressed) return 0;
    return 1;
}

int main(void)
{
    static uint16_t frame[1024 * 600 + 64];
    static int8_t ref[320 * 320 * 3 + 16], got[320 * 320 * 3 + 16];
    static const uint16_t sizes[][3] = {{320,240,320},{320,240,336},{640,480,640},{1024,600,1024},{160,120,160},{240,320,240},{200,300,208},{320,320,320},{17,5,17}};
    for (unsigned t = 0; t < sizeof(sizes) / sizeof(sizes[0]); t++) {
        for (unsigned rep = 0; rep < 3; rep++) {
            for (unsigned i = 0; i < sizeof(frame) / sizeof(frame[0]); i++) frame[i] = (uint16_t)next_u32();
            uint32_t bytes = 320 * 320 * 3 + (rep == 2 ? 16U : 0U);
            memset(ref, 0x11, sizeof ref); memset(got, 0x22, sizeof got);
            ref_prepare(frame, sizes[t][0], sizes[t][1], sizes[t][2], ref, bytes);
            od_prepare_input(frame, sizes[t][0], sizes[t][1], sizes[t][2], got, bytes);
            assert(memcmp(ref, got, bytes) == 0);
        }
    }
    static int8_t out20[20 * 20 * 255], out10[10 * 10 * 255];
    static const float scales[] = {0.05F, 0.1F, 0.1875F, 0.25F, 0.5F};
    static const int32_t zps[] = {-128, -40, 0, 17, 60};
    unsigned total = 0;
    for (unsigned t = 0; t < 400; t++) {
        float s = scales[t % 5]; int32_t z = zps[(t / 5) % 5];
        for (unsigned i = 0; i < sizeof out20; i++) out20[i] = (int8_t)(next_u32() >> 24);
        for (unsigned i = 0; i < sizeof out10; i++) out10[i] = (int8_t)(next_u32() >> 24);
        /* Bias some anchors high so candidates (and the 64-slot replacement) are exercised. */
        for (unsigned k = 0; k < 40 + t % 200; k++) { unsigned a = next_u32() % 1200; out20[a / 3 * 255 + (a % 3) * 85 + 4] = 127; out20[a / 3 * 255 + (a % 3) * 85 + 5 + next_u32() % 6] = 127; }
        if (t % 7 == 0) memset(out10, 127, sizeof out10); /* saturated ties */
        od_candidate_t rc[OD_MAX_CANDIDATES], gc[OD_MAX_CANDIDATES]; uint32_t rn = 0, gn = 0;
        ref_decode(out20, 20, 20, s, z, rc, &rn); ref_decode(out10, 10, 10, s, z, rc, &rn);
        od_decode_output(out20, 20, 20, s, z, gc, &gn); od_decode_output(out10, 10, 10, s, z, gc, &gn);
        assert(same_candidates(rc, rn, gc, gn));
        total += rn;
    }
    assert(total > 1000U);
    printf("PASS obstacle kernels: prepare_input bit-exact (9 frame shapes incl. stride/portrait/odd), decode_output bit-exact (400 random grids, %u candidates, saturated ties, 64-slot replacement)\n", total);
    return 0;
}
