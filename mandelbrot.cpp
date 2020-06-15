#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION 1

#include "colormaps.h"
#include "stb_image_write.h"

#define MAX_STEPS 1 << 11
#define MIN_STEPS 1 << 7
#define COUNT(a) (sizeof(a) / sizeof(0 [a]))

#define LERP(a, b, u) ((a) * (1 - (u)) + (b) * (u))

union BufferData {
    uint32_t value;
    struct {
        uint8_t r, g, b, a;
    };
};

static inline long double rand_range(long double min, long double max) {
    long double u = (long double)rand() / RAND_MAX;
    return LERP(min, max, u);
}

static uint32_t mandelbrot(long double x, long double y) {
    long double r = x;
    long double i = y;
    long double mag_sq = r * r + i * i;
    uint32_t steps = 0;
    while (steps < MAX_STEPS && mag_sq <= 4) {
        long double rr = r * r - i * i + x;
        i = 2 * r * i + y;
        r = rr;
        mag_sq = r * r + i * i;
        steps++;
    }
    return steps;
}

static uint32_t choose_center(long double& x, long double& y) {
    uint32_t steps;
    do {
        x = rand_range(-1.5, 1);
        y = rand_range(0, 1);
        steps = mandelbrot(x, y);
    } while (steps < MIN_STEPS || steps >= MAX_STEPS);
    return steps;
}

struct CalcBufferData {
    int thread_id;
    BufferData* buffer;
    int start_line, last_line;
    int width, height;
    uint32_t smin, smax;
    long double xmin, xmax, ymin, ymax;
};

static void* calc_buffer(void* p) {
    CalcBufferData* data = (CalcBufferData*)p;
    BufferData* b = data->buffer + data->start_line * data->width;
#ifdef DEBUG
    printf("Thread %d: filling from %d to %d.\n", data->thread_id,
           data->start_line, data->last_line - 1);
    fflush(stdout);
#endif
    for (int j = data->start_line; j < data->last_line; j++) {
        const long double v = (long double)j / (data->height - 1.0);
        const double y = LERP(data->ymin, data->ymax, v);
        for (int i = 0; i < data->width; i++, b++) {
            const long double u = (long double)i / (data->width - 1.0);
            const double x = LERP(data->xmin, data->xmax, u);
            const uint32_t steps =
                1 + mandelbrot(x, y);  // Add 1 due to log scaling
            b->value = steps;
            if (steps < data->smin) {
                data->smin = steps;
            } else if (steps > data->smax) {
                data->smax = steps;
            }
        }
    }
#ifdef DEBUG
    printf("Thread %d: done.\n", data->thread_id);
    fflush(stdout);
#endif
    return NULL;
}

struct GenImageData {
    int thread_id;
    BufferData* buffer;
    int start_line, last_line;
    int width, height;
    double log_min, log_delta;
    uint8_t* colormap;
    int max_index;
};

static void* gen_image(void* p) {
    GenImageData* data = (GenImageData*)p;
    BufferData* b = data->buffer + data->start_line * data->width;
#ifdef DEBUG
    printf("Thread %d: generating image from %d to %d.\n", data->thread_id,
           data->start_line, data->last_line - 1);
    fflush(stdout);
#endif
    for (int j = data->start_line; j < data->last_line; j++) {
        for (int i = 0; i < data->width; i++, b++) {
            double value = (log(b->value) - data->log_min) / data->log_delta;
            if (value < 0)
                value = 0;
            else if (value > 1)
                value = 1;
            const int index = int(0.5 + value * data->max_index);
            const uint8_t* sample = data->colormap + 3 * index;
            b->r = *sample++;
            b->g = *sample++;
            b->b = *sample++;
            b->a = 0xFF;
        }
    }
#ifdef DEBUG
    printf("Thread %d: done.\n", data->thread_id);
    fflush(stdout);
#endif
    return NULL;
}

int main(int argc, char* argv[]) {
    int np = get_nprocs();
    const int num_threads = np > 2 ? np - 2 : 1;

#ifdef DEBUG
    printf("Running with %d threads.\n", num_threads);
    fflush(stdout);
#endif

    if (argc < 4) {
        fprintf(stderr, "Usage: %s WIDTH HEIGHT FILENAME\n", argv[0]);
        return 1;
    }

    const int wid = atoi(argv[1]);
    const int hei = atoi(argv[2]);
    if (wid <= 0 || hei <= 0) {
        fprintf(stderr, "Width (%d) and height (%d) must be greater than 0.",
                wid, hei);
        return 2;
    }

    srand(time(NULL));

    long double x, y;
    const uint32_t steps = choose_center(x, y);
    const long double dx = powl(steps, rand_range(-2.5, -1));
    const long double dy = dx * hei / wid;

#ifdef DEBUG
    printf("Image window: (%Lg, %Lg) x (%Lg, %Lg).\n", xmin, ymin, xmax, ymax);
#endif

    BufferData* buffer = (BufferData*)malloc(sizeof(BufferData) * wid * hei);
    const int lines_per_thread = hei / num_threads + 1;
    const int pitch = lines_per_thread * wid;

    pthread_t thread[num_threads];
    CalcBufferData cb_data[num_threads];
    for (int t = 0; t < num_threads; t++) {
        cb_data[t] = {t,
                      buffer,
                      t * lines_per_thread,
                      (t == num_threads - 1) ? hei : (t + 1) * lines_per_thread,
                      wid,
                      hei,
                      MAX_STEPS + 1,
                      0,
                      x - dx,
                      x + dx,
                      y - dy,
                      y + dy};
        pthread_create(thread + t, NULL, calc_buffer, (void*)(cb_data + t));
    }

    uint32_t smin = MAX_STEPS + 1;
    uint32_t smax = 0;
    for (int t = 0; t < num_threads; t++) {
        pthread_join(thread[t], NULL);
#ifdef DEBUG
        printf("Joined thread %d.\n", t);
        fflush(stdout);
#endif
        if (cb_data[t].smin < smin) smin = cb_data[t].smin;
        if (cb_data[t].smax > smax) smax = cb_data[t].smax;
    }

    const double log_min = log(smin);
    const double log_delta = log(smax) - log_min;

    const uint8_t* colormaps[] = {twilight_shifted, magma, bone, cmrmap};
    const int size[] = {COUNT(twilight_shifted), COUNT(magma), COUNT(bone),
                        COUNT(cmrmap)};
    int cmap_choice = rand() % COUNT(colormaps);
    uint8_t* colormap = (uint8_t*)colormaps[cmap_choice];
    int max_index = size[cmap_choice] / 3 - 1;

#ifdef DEBUG
    printf("Using colormap %d.\n", cmap_choice);
#endif

    GenImageData gi_data[num_threads];
    for (int t = 0; t < num_threads; t++) {
        gi_data[t] = {t,
                      buffer,
                      t * lines_per_thread,
                      (t == num_threads - 1) ? hei : (t + 1) * lines_per_thread,
                      wid,
                      hei,
                      log_min,
                      log_delta,
                      colormap,
                      max_index};
        pthread_create(thread + t, NULL, gen_image, (void*)(gi_data + t));
    }

    for (int t = 0; t < num_threads; t++) {
        pthread_join(thread[t], NULL);
#ifdef DEBUG
        printf("Joined thread %d.\n", t);
        fflush(stdout);
#endif
    }

#ifdef DEBUG
    printf("Saving image.\n");
    fflush(stdout);
#endif

    stbi_write_png_compression_level = 10;
    stbi_write_png(argv[3], wid, hei, 4, buffer, wid * 4);

#ifdef DEBUG
    printf("Done.\n");
    fflush(stdout);
#endif

    free(buffer);

    return 0;
}
