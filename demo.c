#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "mathc/mathc.h"

#include "sdf.h"

typedef struct {
    struct vec2 iResolution;
    void *ud;
    struct vec4 *region;
} image_data;

struct canvas {
    struct vec3 *buf;
    struct vec2 res;
};

#define US_MAXTHREADS 8

typedef struct {
    struct vec3 *buf;
    image_data *data;
    int off;
    void (*draw)(struct vec3 *, struct vec2, image_data *);
    int stride;
} thread_data;

void *draw_thread(void *arg)
{
    thread_data *td;
    image_data *data;
    int x, y;
    int w, h;
    int stride;
    struct vec3 *buf;
    int nthreads;
    int xstart, ystart;
    int xend, yend;
    int maxpos;
    struct vec4 *reg;

    td = arg;
    data = td->data;
    buf = td->buf;

    w = data->iResolution.x;
    h = data->iResolution.y;
    stride = td->stride;
    reg = data->region;

    ystart = td->off + reg->y;
    xstart = reg->x;
    xend = reg->z + reg->x;
    yend = reg->w + reg->y;

    /* This is hard-coded for now */
    nthreads = US_MAXTHREADS;

    maxpos = w * h;

    for (y = ystart; y < yend; y+=nthreads) {
        for (x = xstart; x < xend; x++) {
            int pos;
            struct vec3 *c;
            pos = y*stride + x;

            if (pos > maxpos) continue;
            c = &buf[pos];
            td->draw(c, svec2(x - reg->x, y - reg->y), data);
        }
    }

    return NULL;
}

void draw_with_stride(struct vec3 *buf,
                      struct vec2 res,
                      struct vec4 region,
                      void (*drawfunc)(struct vec3 *, struct vec2, image_data *),
                      void *ud,
                      int stride)
{
    thread_data td[US_MAXTHREADS];
    pthread_t thread[US_MAXTHREADS];
    int t;
    image_data data;

    data.iResolution = res;
    data.ud = ud;
    data.region = &region;

    for (t = 0; t < US_MAXTHREADS; t++) {
        td[t].buf = buf;
        td[t].data = &data;
        td[t].off = t;
        td[t].draw = drawfunc;
        td[t].stride = stride;
        pthread_create(&thread[t], NULL, draw_thread, &td[t]);
    }

    for (t = 0; t < US_MAXTHREADS; t++) {
        pthread_join(thread[t], NULL);
    }
}

void draw(struct vec3 *buf,
          struct vec2 res,
          struct vec4 region,
          void (*drawfunc)(struct vec3 *, struct vec2, image_data *),
          void *ud)
{
    draw_with_stride(buf, res, region, drawfunc, ud, res.x);
}

static float smoothstep(float e0, float e1, float x)
{
    float t;
    t = clampf((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

static void d_heart(struct vec3 *fragColor,
                    struct vec2 fragCoord,
                    image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);
    p = sdf_heart_center(fragCoord, res);

    d = -sdf_heart(p);

    col = svec3(1., 1., 1.);
    col = svec3_subtract(col,
                         svec3_multiply_f(svec3(0., 0.25, 0.25),
                                          sdf_sign(d)));
    alpha = 0;
    alpha = sdf_sign(d) > 0;
    alpha += smoothstep(0.01, 0.0, fabs(d));
    alpha = clampf(alpha, 0, 1);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void heart(struct canvas *ctx,
           float x, float y, 
           float w, float h,
           struct vec3 clr)
{
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_heart, &clr);
}

struct vec3 rgb2color(int r, int g, int b)
{
    float scale = 1.0 / 255;
    return svec3(r * scale, g * scale, b * scale);
}

static int mkcolor(float x)
{
    return floor(x * 255);
}

static void write_ppm(struct vec3 *buf,
                      struct vec2 res,
                      const char *filename)
{
    int x, y;
    FILE *fp;
    unsigned char *ibuf;

    fp = fopen(filename, "w");
    fprintf(fp, "P6\n%d %d\n%d\n", (int)res.x, (int)res.y, 255);

    ibuf = malloc(3 * res.y * res.x * sizeof(unsigned char));
    for (y = 0; y < res.y; y++) {
        for (x = 0; x < res.x; x++) {
            int pos;
            pos = y * res.x + x;

            ibuf[3*pos] = mkcolor(buf[pos].x);
            ibuf[3*pos + 1] = mkcolor(buf[pos].y);
            ibuf[3*pos + 2] = mkcolor(buf[pos].z);
        }
    }

    fwrite(ibuf, 3 * res.y * res.x * sizeof(unsigned char), 1, fp);
    free(ibuf);
    fclose(fp);
}

static void d_fill(struct vec3 *fragColor,
                   struct vec2 fragCoord,
                   image_data *id)
{

    struct vec3 *col;

    col = id->ud;
    *fragColor = *col;
}

static void fill(struct canvas *ctx, struct vec3 clr)
{
    draw(ctx->buf, ctx->res, 
         svec4(0, 0, ctx->res.x, ctx->res.y), 
         d_fill, &clr);
}

int main(int argc, char *argv[])
{
    struct vec3 *buf;
    int width, height;
    struct vec2 res;
    struct canvas ctx;

    width = 640;
    height = 480;

    res = svec2(width, height);

    buf = malloc(width * height * sizeof(struct vec3));

    ctx.res = res;
    ctx.buf = buf;

    fill(&ctx, svec3(1., 1.0, 1.0));

    heart(&ctx, 0, 0, width, height, rgb2color(255, 192, 203));

    write_ppm(buf, res, "demo.ppm");

    free(buf);
    return 0;
}
