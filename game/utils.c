#define COUNTOF(X) (sizeof(X)/sizeof((X)[0]))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define LIT(s) (string) {.count=sizeof(s)-1, .data=(u8*)(s)}

typedef enum {
    // Value are chosen such that -d is the direction opposite to d
    DIR_UP    = +1,
    DIR_DOWN  = -1,
    DIR_LEFT  = +2,
    DIR_RIGHT = -2,
} Direction;

typedef struct {
    float x, y, w, h;
} Rect;

bool almost_equals(float a, float b, float epsilon)
{
    return fabs(a - b) <= epsilon;
}

bool animate_f32_to_target(float* value, float target, float delta_t, float rate)
{
	*value += (target - *value) * (1.0 - pow(2.0f, -rate * delta_t));
	if (almost_equals(*value, target, 0.001f)) {
		*value = target;
		return true; // reached
	}
	return false;
}

void animate_rect_to_target(Rect *value, Rect target, float delta_t, float rate)
{
    animate_f32_to_target(&value->x, target.x, delta_t, rate);
    animate_f32_to_target(&value->y, target.y, delta_t, rate);
    animate_f32_to_target(&value->w, target.w, delta_t, rate);
    animate_f32_to_target(&value->h, target.h, delta_t, rate);
}

#define m4_identity m4_make_scale(v3(1, 1, 1))

void draw_subimage(Gfx_Image *image, float rotate,
                  float dst_x, float dst_y, float dst_w, float dst_h,
                  float src_x, float src_y, float src_w, float src_h)
{
    Matrix4 model = m4_identity;

    model = m4_translate(model, v3(dst_x, dst_y, 0.0));

    model = m4_translate(model, v3(dst_w/2, dst_h/2, 0.0));
    model = m4_rotate_z(model, M_PI / 2 * rotate);
    model = m4_translate(model, v3(-dst_w/2, -dst_h/2, 0.0));

    Draw_Quad *q = draw_image_xform(image, model, v2(dst_w, dst_h), COLOR_WHITE);

    q->uv = v4(
        (float) src_x / image->width,
        (float) src_y / image->height,
        (float) (src_x + src_w) / image->width,
        (float) (src_y + src_h) / image->height
    );
}

void draw_rect_border(Rect rect, float line_w, Vector4 color)
{
    draw_line(v2(rect.x,          rect.y         ), v2(rect.x + rect.w, rect.y         ), line_w, color);
    draw_line(v2(rect.x + rect.w, rect.y         ), v2(rect.x + rect.w, rect.y + rect.h), line_w, color);
    draw_line(v2(rect.x + rect.w, rect.y + rect.h), v2(rect.x,          rect.y + rect.h), line_w, color);
    draw_line(v2(rect.x,          rect.y + rect.h), v2(rect.x,          rect.y         ), line_w, color);
}

Rect padded_rect(Rect r, float pad)
{
    r.x -= pad;
    r.y -= pad;
    r.w += pad * 2;
    r.h += pad * 2;
    return r;
}

bool mouse_in_rect(Rect rect)
{
    return input_frame.mouse_x >= rect.x && input_frame.mouse_x < rect.x + rect.w &&
           input_frame.mouse_y >= rect.y && input_frame.mouse_y < rect.y + rect.h;
}

#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#else
#include <time.h>
#include <stdlib.h>
#endif

/*
 * Returns the current absolute time in microsecods
 * TODO: Specify since when the time is calculated
 */
uint64_t get_absolute_time_us(void)
{
    #ifdef _WIN32
    FILETIME filetime;
    GetSystemTimePreciseAsFileTime(&filetime);
    uint64_t time = (uint64_t) filetime.dwLowDateTime | ((uint64_t) filetime.dwHighDateTime << 32);
    time /= 10;
    return time;
    #else
    struct timespec buffer;
    if (clock_gettime(CLOCK_REALTIME, &buffer))
        abort();
    uint64_t time = buffer.tv_sec * 1000000 + buffer.tv_nsec / 1000;
    return time;
    #endif
}