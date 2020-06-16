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

uint32_t max_steps = 1 << 11;
uint32_t min_steps = 1 << 7;

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
    while (steps < max_steps && mag_sq <= 4) {
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
    } while (steps < min_steps || steps >= max_steps);
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
    const uint8_t* colormaps[] = {twilight_shifted, magma, bone, cmrmap};
    const int size[] = {COUNT(twilight_shifted), COUNT(magma), COUNT(bone),
                        COUNT(cmrmap)};

    const char* filename = NULL;
    int wid = 960;
    int hei = 540;
    bool center_set = false;
    bool size_set = false;
    long double x = 0;
    long double y = 0;
    long double dx = 0;
    long double dy = 0;
    int cmap_choice = -1;
    unsigned int seed = time(NULL);
    int num_threads = get_nprocs() - 1;
    if (num_threads <= 0) num_threads = 1;

    for (int i = 1; i < argc; i++) {
#ifdef DEBUG
        printf("Parsing argument %s\n", argv[i]);
#endif
        if (argv[i][0] != '-') {
            filename = argv[i];
#ifdef DEBUG
            printf("Output filename: %s\n", filename);
#endif
            continue;
        }
        switch (argv[i][1]) {
            case 'h':
                printf(
                    "Usage: %s [OPTION]... FILENAME\n\n"
                    "Options:\n"
                    "  -h                    Show this help message and exit.\n"
                    "  -g WIDTH HEIGHT       Image size (defaults 960 540).\n"
                    "  -c X Y                View window center.\n"
                    "  -s DX DY              View window size.\n"
                    "  -z MIN MAX            Minimal value to accept a random "
                    "coordinate as image\n"
                    "                        center and maximal value for the "
                    "fractal calculation\n"
                    "                        (defaults 128 2048).\n"
                    "  -m COLORMAP           Colormap: twilight_shifted, "
                    "magma, bone, CMRmap.\n"
                    "  -r RNG_SEED           Random number generator seed.\n"
                    "  -p NUM                Number of threads to use.\n",
                    argv[0]);
                return 0;
                break;
            case 'g':
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 1]);
                    return 1;
                }
                wid = atoi(argv[i]);
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 2]);
                    return 1;
                }
                hei = atoi(argv[i]);
                if (wid <= 0 || hei <= 0) {
                    fprintf(
                        stderr,
                        "Width (%d) and height (%d) must be greater than 0.\n",
                        wid, hei);
                    return 1;
                }
                break;
            case 'c':
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 1]);
                    return 1;
                }
                x = strtold(argv[i], NULL);
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 2]);
                    return 1;
                }
                y = strtold(argv[i], NULL);
                center_set = true;
                break;
            case 's':
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 1]);
                    return 1;
                }
                dx = strtold(argv[i], NULL) / 2;
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 2]);
                    return 1;
                }
                dy = strtold(argv[i], NULL) / 2;
                size_set = true;
                break;
            case 'z':
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 1]);
                    return 1;
                }
                min_steps = strtoul(argv[i], NULL, 0);
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 2]);
                    return 1;
                }
                max_steps = strtoul(argv[i], NULL, 0);
                if (min_steps >= max_steps) {
                    fprintf(stderr, "MIN (%u) must be greater than MAX (%u).\n",
                            min_steps, max_steps);
                    return 1;
                }
                break;
            case 'm':
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 1]);
                    return 1;
                }
                if (strcmp(argv[i], "twilight_shifted") == 0)
                    cmap_choice = 0;
                else if (strcmp(argv[i], "magma") == 0)
                    cmap_choice = 1;
                else if (strcmp(argv[i], "bone") == 0)
                    cmap_choice = 2;
                else if (strcmp(argv[i], "cmrmap") == 0)
                    cmap_choice = 3;
                else {
                    fprintf(stderr,
                            "Error: invalid colormap choice %s.  Try -h for "
                            "help.\n",
                            argv[i]);
                    return 1;
                }
                break;
            case 'r':
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 1]);
                    return 1;
                }
                seed = strtoul(argv[i], NULL, 0);
                break;
            case 'p':
                if (++i == argc) {
                    fprintf(stderr, "Error: missing value after option %s.\n",
                            argv[i - 1]);
                    return 1;
                }
                num_threads = atoi(argv[i]);
                if (num_threads <= 0) {
                    fprintf(stderr,
                            "Error: invalid value for number of threads (%s == "
                            "%d).\n",
                            argv[i], num_threads);
                    return 1;
                }
                break;
            default:
                fprintf(stderr, "Error: unexpected parameter %s.\n", argv[i]);
                return 1;
        }
    }

    if (filename == NULL) {
        fprintf(stderr,
                "Error: missing filename!\nUsage: %s [OPTIONS] FILENAME\n",
                argv[0]);
        return 1;
    }

#ifdef DEBUG
    printf("Seed: %u\nRunning with %d threads.\nImage size: %d x %d\n", seed,
           num_threads, wid, hei);
#endif

    srand(seed);

    uint32_t steps;
    if (center_set)
        steps = mandelbrot(x, y);
    else
        steps = choose_center(x, y);

    if (!size_set) {
        dx = powl(steps, rand_range(-2.5, -1));
        dy = dx * hei / wid;
    }

#ifdef DEBUG
    printf("Image window: (%Lg, %Lg) x (%Lg, %Lg).\n", x - dx, y - dy, x + dx,
           y + dy);
#endif

    if (cmap_choice < 0) cmap_choice = rand() % COUNT(colormaps);

#ifdef DEBUG
    printf("Using colormap %d.\n", cmap_choice);
#endif

    uint8_t* colormap = (uint8_t*)colormaps[cmap_choice];
    const int max_index = size[cmap_choice] / 3 - 1;

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
                      max_steps + 1,
                      0,
                      x - dx,
                      x + dx,
                      y - dy,
                      y + dy};
        pthread_create(thread + t, NULL, calc_buffer, (void*)(cb_data + t));
    }

    uint32_t smin = max_steps + 1;
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
    stbi_write_png(filename, wid, hei, 4, buffer, wid * 4);

#ifdef DEBUG
    printf("Done.\n");
    fflush(stdout);
#endif

    free(buffer);

    return 0;
}
