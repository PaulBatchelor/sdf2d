#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "mathc/mathc.h"

#include "sdf.h"

#define SDF2D_SDFVM_PRIV
#include "sdfvm.h"

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

typedef struct {
    sdfvm vm;
    uint8_t *program;
    size_t sz;
    sdfvm_stacklet registers[16];
} user_params;

#define US_MAXTHREADS 8

typedef struct thread_userdata thread_userdata;

typedef struct {
    struct vec3 *buf;
    image_data *data;
    int off;
    void (*draw)(struct vec3 *, struct vec2, thread_userdata *);
    int stride;
    sdfvm vm;
} thread_data;

struct thread_userdata {
    thread_data *th;
    image_data *data;
};

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
    thread_userdata thud;

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

    thud.th = td;
    thud.data = data;
    for (y = ystart; y < yend; y+=nthreads) {
        for (x = xstart; x < xend; x++) {
            int pos;
            struct vec3 *c;
            pos = y*stride + x;

            if (pos > maxpos || pos < 0) continue;
            c = &buf[pos];
            td->draw(c, svec2(x - reg->x, y - reg->y), &thud);
        }
    }

    return NULL;
}

void draw_with_stride(struct vec3 *buf,
                      struct vec2 res,
                      struct vec4 region,
                      void (*drawfunc)(struct vec3 *, struct vec2, thread_userdata *),
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
        sdfvm_init(&td[t].vm);
        pthread_create(&thread[t], NULL, draw_thread, &td[t]);
    }

    for (t = 0; t < US_MAXTHREADS; t++) {
        pthread_join(thread[t], NULL);
    }
}

void draw(struct vec3 *buf,
          struct vec2 res,
          struct vec4 region,
          void (*drawfunc)(struct vec3 *, struct vec2, thread_userdata *),
          void *ud)
{
    draw_with_stride(buf, res, region, drawfunc, ud, res.x);
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

static void d_fill(struct vec3 *fragColor,
                   struct vec2 fragCoord,
                   thread_userdata *thud)
{
    image_data *id;
    struct vec3 *col;
    id = thud->data;

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

static void draw_color(sdfvm *vm,
                       struct vec2 p,
                       struct vec3 *fragColor,
                       uint8_t *program,
                       size_t sz,
                       sdfvm_stacklet *registers,
                       int nregisters)
{
    struct vec3 col;

    sdfvm_point_set(vm, p);
    sdfvm_color_set(vm, *fragColor);
    sdfvm_registers(vm, registers, nregisters);
    sdfvm_execute(vm, program, sz);

#if 0
    points[0] = svec2(-0.5, 0.5);
    points[1] = svec2(-0.1, -0.5);
    points[2] = svec2(0.1, -0.5);
    points[3] = svec2(0.5, 0.5);

    sdfvm_push_vec2(vm, p);
    for (i = 0; i < 4; i++) {
        sdfvm_push_vec2(vm, points[i]);
    }
    sdfvm_poly4(vm);
    sdfvm_push_scalar(vm, 0.1);
    sdfvm_roundness(vm);

    sdfvm_push_vec2(vm, p);
    sdfvm_push_scalar(vm, 0.7);
    sdfvm_circle(vm);

    sdfvm_push_scalar(vm, 0.1);
    sdfvm_lerp(vm);

    sdfvm_push_scalar(vm, -1.0);
    sdfvm_mul(vm);
    sdfvm_gtz(vm);
    sdfvm_push_vec3(vm, *fragColor);
    sdfvm_push_vec3(vm, svec3_zero());
    sdfvm_lerp3(vm);
#endif

    sdfvm_pop_vec3(vm, &col);

    *fragColor = col;
}

static void d_polygon(struct vec3 *fragColor,
                      struct vec2 st,
                      thread_userdata *thud)
{
    struct vec2 p;
    image_data *id;
    struct vec2 res;
    sdfvm *vm;
    user_params *params;

    id = thud->data;
    vm = &thud->th->vm;
    params = id->ud;

    res = svec2(id->region->z, id->region->w);
    sdfvm_push_vec2(vm, svec2(st.x, st.y));
    sdfvm_push_vec2(vm, res);
    sdfvm_normalize(vm);
    sdfvm_pop_vec2(vm, &p);
    p.y = p.y*-1;

    draw_color(vm, p, fragColor,
            params->program, params->sz,
            params->registers, 16);
}

void polygon(struct canvas *ctx,
           float x, float y,
           float w, float h,
           user_params *p)
{
    draw(ctx->buf, ctx->res, svec4(x, y, w, h), d_polygon, p);
}

static int add_float(uint8_t *prog, size_t *ppos, size_t maxsz, float val)
{
    uint8_t fdata[4];
    float *fptr;
    size_t pos;

    pos = *ppos;
    if ((pos + 4) > maxsz) {
        fprintf(stderr, "Warning: out of memory\n");
        return 1;
    }

    fptr = (float *)fdata;
    *fptr = val;
    prog[pos++] = fdata[0];
    prog[pos++] = fdata[1];
    prog[pos++] = fdata[2];
    prog[pos++] = fdata[3];

    *ppos = pos;

    return 0;
}

void generate_program(uint8_t *prog, size_t *sz, size_t maxsz)
{
    size_t pos;
    int i;
    pos = 0;

    prog[pos++] = SDF_OP_POINT;
    for (i = 0; i < 4; i++) {
        prog[pos++] = SDF_OP_SCALAR;
        add_float(prog, &pos, maxsz, i);
        prog[pos++] = SDF_OP_REGISTER;
    }
    prog[pos++] = SDF_OP_POLY4;
   
    /* r5: rounded edge amount */
    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 5);
    prog[pos++] = SDF_OP_REGISTER;

    prog[pos++] = SDF_OP_ROUNDNESS;
    prog[pos++] = SDF_OP_POINT;

    /* r6: circle radius */
    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 6);
    prog[pos++] = SDF_OP_REGISTER;

    prog[pos++] = SDF_OP_CIRCLE;

    /* r4: circleness amount */
    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 4);
    prog[pos++] = SDF_OP_REGISTER;

    prog[pos++] = SDF_OP_LERP;

    prog[pos++] = SDF_OP_POINT;
    prog[pos++] = SDF_OP_VEC2;
    add_float(prog, &pos, maxsz, 0.0);
    add_float(prog, &pos, maxsz, -0.6);
    prog[pos++] = SDF_OP_ADD2;
    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 0.3);
    prog[pos++] = SDF_OP_CIRCLE;

    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 0.1);

    prog[pos++] = SDF_OP_UNION_SMOOTH;


    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 0.01);
    prog[pos++] = SDF_OP_ONION;

    prog[pos++] = SDF_OP_POINT;
    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 0.1);
    prog[pos++] = SDF_OP_CIRCLE;

    prog[pos++] = SDF_OP_POINT;
    prog[pos++] = SDF_OP_VEC2;
    add_float(prog, &pos, maxsz, 0.0);
    add_float(prog, &pos, maxsz, 0.13);
    prog[pos++] = SDF_OP_ADD2;

    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, 0.12);
    prog[pos++] = SDF_OP_CIRCLE;

    prog[pos++] = SDF_OP_UNION;


    prog[pos++] = SDF_OP_ADD;

    prog[pos++] = SDF_OP_SCALAR;
    add_float(prog, &pos, maxsz, -1.0);
    prog[pos++] = SDF_OP_MUL;

    prog[pos++] = SDF_OP_GTZ;

    prog[pos++] = SDF_OP_COLOR;
    prog[pos++] = SDF_OP_VEC3;
    add_float(prog, &pos, maxsz, 0.0);
    add_float(prog, &pos, maxsz, 0.0);
    add_float(prog, &pos, maxsz, 0.0);
    prog[pos++] = SDF_OP_LERP3;

    *sz = pos;
}

void update_registers(sdfvm_stacklet *r)
{
    int i;
    struct vec2 points[4];

    /* initialize registers */
    for (i = 0; i < 16; i++) {
        r[i].type = SDFVM_SCALAR;
        r[i].data.s = 0.0;
    }

    /* set 0-3 to be quad points */
    points[0] = svec2(-0.5, 0.5);
    points[1] = svec2(-0.1, -0.5);
    points[2] = svec2(0.1, -0.5);
    points[3] = svec2(0.5, 0.5);

    for (i = 0; i <= 3; i++) {
        r[i].type = SDFVM_VEC2;
        r[i].data.v2 = points[i];
    }

    /* register 4: "circleness" */
    r[4].type = SDFVM_SCALAR;
    r[4].data.s = 0.1;

    /* register 5: "roundedge" */
    r[5].type = SDFVM_SCALAR;
    r[5].data.s = 0.1;

    /* register 6: "circrad" */
    r[6].type = SDFVM_SCALAR;
    r[6].data.s = 0.7;
}

int main(int argc, char *argv[])
{
    struct vec3 *buf;
    int width, height;
    struct vec2 res;
    struct canvas ctx;
    int sz;
    int clrpos;
    user_params params;

    /* rainbow colors:
     * Red: 255, 179, 186
     * Orange: 255, 223, 186
     * Yellow: 255, 255, 186
     * Green: 186, 255, 201
     * Blue: 186, 225, 255
     */

    width = 512;
    height = 512;
    clrpos = 0;

    sz = width / 1;

    res = svec2(width, height);

    buf = malloc(width * height * sizeof(struct vec3));

    ctx.res = res;
    ctx.buf = buf;

    sdfvm_init(&params.vm);
    params.program = calloc(1, 128);
    params.sz = 0;
    generate_program(params.program, &params.sz, 128);
    update_registers(params.registers);

    fill(&ctx, svec3(1., 1.0, 1.0));
    polygon(&ctx, 0, 0, sz, sz, &params);
    clrpos = (clrpos + 1) % 5;

    write_ppm(buf, res, "vmdemo.ppm");

    /* sdfvm_print_lookup_table(NULL); */

    free(buf);
    free(params.program);
    return 0;
}
