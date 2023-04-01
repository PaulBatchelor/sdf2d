#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "mathc/mathc.h"

#include "sdf.h"

/* global feathering amount for hacky anti-aliasing */
#define FEATHER_AMT 0.03

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

struct vec3 rgb2color(int r, int g, int b)
{
    float scale = 1.0 / 255;
    return svec3(r * scale, g * scale, b * scale);
}

static int mkcolor(float x)
{
    return floor(x * 255);
}

static float feather(float d, float amt)
{
    float alpha;
    alpha = 0;
    alpha = sdf_sign(d) > 0;
    alpha += smoothstep(amt, 0.0, fabs(d));
    alpha = clampf(alpha, 0, 1);
    return alpha;
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

void draw_gridlines(struct canvas *ctx)
{
    int x, y;
    int w, h;
    int size;

    w = ctx->res.x;
    h = ctx->res.y;

    size = w / 4;

    for (y = 0; y < h; y += size) {
        for (x = 0; x < w; x++) {
            int pos;
            pos = y*w + x;
            ctx->buf[pos] = svec3_zero();
        }
    }

    for (x = 0; x < w; x += size) {
        for (y = 0; y < h; y++) {
            int pos;
            pos = y*w + x;
            ctx->buf[pos] = svec3_zero();
        }
    }

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

    alpha = feather(d, FEATHER_AMT);

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

static void d_circ(struct vec3 *fragColor,
                   struct vec2 st,
                   image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_circle(p, 0.9);

    alpha = feather(d, FEATHER_AMT);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void circle(struct canvas *ctx,
            float cx, float cy, float r,
            struct vec3 clr)
{
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = r * 2;
    h = w;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_circ, &clr);
}

struct rounded_box_data {
    struct vec2 b;
    struct vec4 r;
    struct vec3 clr;
};

static void d_rounded_box(struct vec3 *fragColor,
                          struct vec2 st,
                          image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    float alpha;
    struct vec2 res;
    struct rounded_box_data *rb;

    res = svec2(id->region->z, id->region->w);
    rb = (struct rounded_box_data *)id->ud;

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_rounded_box(p, rb->b, rb->r);

    alpha = feather(d, FEATHER_AMT);

    col = svec3_lerp(*fragColor, rb->clr, alpha);
    *fragColor = col;
}

void rounded_box(struct canvas *ctx,
                 float x, float y, float w, float h, float r,
                 struct vec3 clr)
{
    struct rounded_box_data rb;
    /* setting to be <1 yields better roundedness. probably
     * has to do with truncation? 
     */
    rb.b = svec2(0.9, 0.9);
    rb.clr = clr;
    rb.r = svec4(r, r, r, r);
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_rounded_box, &rb);
}

struct box_data {
    struct vec2 b;
    struct vec3 clr;
};

static void d_box(struct vec3 *fragColor,
                  struct vec2 st,
                  image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    float alpha;
    struct vec2 res;
    struct box_data *bb;

    res = svec2(id->region->z, id->region->w);
    bb = (struct box_data *)id->ud;

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_box(p, bb->b);

    alpha = feather(d, FEATHER_AMT);

    col = svec3_lerp(*fragColor, bb->clr, alpha);
    *fragColor = col;
}

void box(struct canvas *ctx,
         float x, float y, float w, float h,
         struct vec3 clr)
{
    struct box_data bb;
    /* setting to be <1 yields better roundedness. probably
     * has to do with truncation? 
     */
    bb.b = svec2(0.9, 0.9);
    bb.clr = clr;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_box, &bb);
}

struct rhombus_data {
    struct vec2 b;
    struct vec3 clr;
};

static void d_rhombus(struct vec3 *fragColor,
                  struct vec2 st,
                  image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    float alpha;
    struct vec2 res;
    struct rhombus_data *rh;

    res = svec2(id->region->z, id->region->w);
    rh = (struct rhombus_data *)id->ud;

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_rhombus(p, rh->b);

    alpha = feather(d, FEATHER_AMT);

    col = svec3_lerp(*fragColor, rh->clr, alpha);
    *fragColor = col;
}

void rhombus(struct canvas *ctx,
         float cx, float cy, float r,
         struct vec3 clr)
{
    struct rhombus_data rh;
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = 2 * r;
    h = w;
    rh.b = svec2(0.9, 0.9);
    rh.clr = clr;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_rhombus, &rh);
}

struct triangle_equilateral_data {
    struct vec3 clr;
};

static void d_triangle_equilateral(struct vec3 *fragColor,
                  struct vec2 st,
                  image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    float alpha;
    struct vec2 res;
    struct triangle_equilateral_data *rh;

    res = svec2(id->region->z, id->region->w);
    rh = (struct triangle_equilateral_data *)id->ud;

    p = sdf_normalize(svec2(st.x, st.y), res);
    /* horizontal flip */
    p.y = 1 - p.y;
    d = -sdf_equilateral_triangle(p);

    alpha = feather(d, FEATHER_AMT);

    col = svec3_lerp(*fragColor, rh->clr, alpha);
    *fragColor = col;
}

void triangle_equilateral(struct canvas *ctx,
         float cx, float cy, float r,
         struct vec3 clr)
{
    struct triangle_equilateral_data tri;
    float x, y, w, h;
    float rad;

    /* bust out some trig to convert 
     * barycentric radial coordinates to rectangular bounds
     */

    rad = (2 * M_PI) / 360.0;

    w = 2.0 * r * cos(30.0 * rad);
    h = w * sin(60.0 * rad);
    x = cx - w*0.5;
    y = cy - r;

    tri.clr = clr;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_triangle_equilateral, &tri);
}

static void d_pentagon(struct vec3 *fragColor,
                       struct vec2 st,
                       image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_pentagon(p, 0.8);

    alpha = feather(d, FEATHER_AMT);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void pentagon(struct canvas *ctx,
              float cx, float cy, float r,
              struct vec3 clr)
{
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = r * 2;
    h = w;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_pentagon, &clr);
}

static void d_hexagon(struct vec3 *fragColor,
                       struct vec2 st,
                       image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_hexagon(p, 0.8);

    alpha = feather(d, FEATHER_AMT);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void hexagon(struct canvas *ctx,
              float cx, float cy, float r,
              struct vec3 clr)
{
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = r * 2;
    h = w;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_hexagon, &clr);
}

static void d_octogon(struct vec3 *fragColor,
                      struct vec2 st,
                      image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_octogon(p, 0.8);

    alpha = feather(d, FEATHER_AMT);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void octogon(struct canvas *ctx,
             float cx, float cy, float r,
             struct vec3 clr)
{
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = r * 2;
    h = w;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_octogon, &clr);
}

static void d_hexagram(struct vec3 *fragColor,
                       struct vec2 st,
                       image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = sdf_hexagram(p, 0.5);

    alpha = feather(d, FEATHER_AMT);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void hexagram(struct canvas *ctx,
              float cx, float cy, float r,
              struct vec3 clr)
{
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = r * 2;
    h = w;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_hexagram, &clr);
}
struct star5_data {
    struct vec3 clr;
    float rf;
};

static void d_star5(struct vec3 *fragColor,
                  struct vec2 st,
                  image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    float alpha;
    struct vec2 res;
    struct star5_data *star;

    res = svec2(id->region->z, id->region->w);
    star = (struct star5_data *)id->ud;

    /* flip so the start is pointing upwards */
    st.y = res.y - st.y;
    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_star5(p, 0.9, star->rf);

    alpha = feather(d, FEATHER_AMT);

    col = svec3_lerp(*fragColor, star->clr, alpha);
    *fragColor = col;
}

void star5(struct canvas *ctx,
           float cx, float cy, float r, float rf,
           struct vec3 clr)
{
    struct star5_data star;
    float x, y, w, h;


    w = 2.0 * r;
    h = w;
    x = cx - r;
    y = cy - r;

    star.clr = clr;
    star.rf = rf;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_star5, &star);
}

struct rounded_x_data {
    struct vec3 clr;
    float r;
};

static void d_rounded_x(struct vec3 *fragColor,
                  struct vec2 st,
                  image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    float alpha;
    struct vec2 res;
    struct rounded_x_data *rx;

    res = svec2(id->region->z, id->region->w);
    rx = (struct rounded_x_data *)id->ud;

    /* flip so the start is pointing upwards */
    st.y = res.y - st.y;
    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_rounded_x(p, 0.9, rx->r);

    alpha = feather(d, FEATHER_AMT);

    col = svec3_lerp(*fragColor, rx->clr, alpha);
    *fragColor = col;
}

void rounded_x(struct canvas *ctx,
           float cx, float cy, float r, float thickness,
           struct vec3 clr)
{
    struct rounded_x_data rx;
    float x, y, w, h;

    w = 2.0 * r;
    h = w;
    x = cx - r;
    y = cy - r;

    rx.clr = clr;
    rx.r = thickness;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_rounded_x, &rx);
}

static void d_vesica(struct vec3 *fragColor,
                      struct vec2 st,
                      image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);

    p = sdf_normalize(svec2(st.x, st.y), res);
    d = -sdf_vesica(p, 0.9, 0.5);

    alpha = feather(d, FEATHER_AMT);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void vesica(struct canvas *ctx,
             float cx, float cy, float r,
             struct vec3 clr)
{
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = r * 2;
    h = w;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_vesica, &clr);
}

static void d_egg(struct vec3 *fragColor,
                      struct vec2 st,
                      image_data *id)
{
    struct vec2 p;
    float d;
    struct vec3 col;
    struct vec3 *fg;
    float alpha;
    struct vec2 res;

    res = svec2(id->region->z, id->region->w);

    st.y = res.y - st.y;
    p = sdf_normalize(svec2(st.x, st.y), res);
    p = svec2_add(p, svec2(0, 0.2));
    d = -sdf_egg(p, 0.6, 0.3);

    alpha = feather(d, FEATHER_AMT);

    fg = (struct vec3 *)id->ud;
    col = svec3_lerp(*fragColor, *fg, alpha);
    *fragColor = col;
}

void egg(struct canvas *ctx,
             float cx, float cy, float r,
             struct vec3 clr)
{
    float x, y, w, h;
    x = cx - r;
    y = cy - r;
    w = r * 2;
    h = w;
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_egg, &clr);
}

int main(int argc, char *argv[])
{
    struct vec3 *buf;
    int width, height;
    struct vec2 res;
    struct canvas ctx;
    int sz;
    float cscale, padding;
    int sz_scaled;
    struct vec3 rainbow[5];
    int clrpos;

    /* rainbow colors:
     * Red: 255, 179, 186
     * Orange: 255, 223, 186
     * Yellow: 255, 255, 186
     * Green: 186, 255, 201
     * Blue: 186, 225, 255
     */

    /* red */
    rainbow[0] = rgb2color(255, 179, 186);
    /* orange */
    rainbow[1] = rgb2color(255, 223, 186);
    /* yellow */
    rainbow[2] = rgb2color(255, 255, 186);
    /* green */
    rainbow[3] = rgb2color(186, 255, 201);
    /* blue */
    rainbow[4] = rgb2color(186, 255, 255);

    width = 512;
    height = 512;
    clrpos = 0;

    sz = width / 4;
    cscale = 0.75;
    padding = (1 - cscale) * sz * 0.5;
    sz_scaled = sz * cscale;


    res = svec2(width, height);

    buf = malloc(width * height * sizeof(struct vec3));

    ctx.res = res;
    ctx.buf = buf;

    fill(&ctx, svec3(1., 1.0, 1.0));
    heart(&ctx, 0, 0, sz, sz, rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;
    circle(&ctx, 1*sz + sz*0.5, sz*0.5, (sz*0.5)*0.75, rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;
    rounded_box(&ctx, 
                2*sz + sz*0.125, 
                0 + sz * 0.125, sz*0.75, sz*0.75, 0.5, rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    box(&ctx, 
        3*sz + padding, 
        0 + padding, sz*0.75, sz*0.75, rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    rhombus(&ctx, 
        0*sz + sz*0.5, 
        1*sz + sz*0.5, sz_scaled*0.5, rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    triangle_equilateral(&ctx, 
                         1*sz + sz*0.5,
                         1*sz + sz*0.5,
                         sz_scaled * 0.7, rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    pentagon(&ctx,
             2*sz + sz*0.5,
             1*sz + sz*0.5,
             sz_scaled*0.5,
             rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    hexagon(&ctx,
            3*sz + sz*0.5,
            1*sz + sz*0.5,
            sz_scaled*0.5,
            rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    octogon(&ctx,
            0*sz + sz*0.5,
            2*sz + sz*0.5,
            sz_scaled*0.5,
            rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    hexagram(&ctx,
             1*sz + sz*0.5,
             2*sz + sz*0.5,
             sz_scaled*0.5,
             rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    star5(&ctx,
          2*sz + sz*0.5,
          2*sz + sz*0.5,
          sz_scaled*0.5,
          0.5,
          rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    rounded_x(&ctx,
              3*sz + sz*0.5,
              2*sz + sz*0.5,
              sz_scaled*0.5,
              0.1,
              rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    vesica(&ctx,
           0*sz + sz*0.5,
           3*sz + sz*0.5,
           sz_scaled*0.5,
           rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

    egg(&ctx,
        1*sz + sz*0.5,
        3*sz + sz*0.5,
        sz_scaled*0.5,
        rainbow[clrpos]);
    clrpos = (clrpos + 1) % 5;

#ifdef DRAW_GRIDLINES
    draw_gridlines(&ctx);
#endif

    write_ppm(buf, res, "demo.ppm");

    free(buf);
    return 0;
}
