#include <assert.h>
#include <stdio.h>

#include "frame_stream_overlay.h"

int main(void)
{
    assert(frame_stream_http_class_is_visible(OBSTACLE_CLASS_PERSON));
    assert(frame_stream_http_class_is_visible(OBSTACLE_CLASS_CAR));
    assert(!frame_stream_http_class_is_visible(OBSTACLE_CLASS_BICYCLE));
    assert(!frame_stream_http_class_is_visible(OBSTACLE_CLASS_MOTORCYCLE));
    assert(!frame_stream_http_class_is_visible(OBSTACLE_DETECTOR_CLASS_COUNT));
    puts("frame_stream overlay contract tests: PASS");
    return 0;
}
