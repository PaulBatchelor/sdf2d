#include <math.h>
#include "mathc/mathc.h"

float sdf_sign(float x)
{
    if (x == 0) return 0;
    return x < 0 ? -1 : 1;
}

float sdf_min(float x, float y)
{
    return x < y ? x : y;
}

float sdf_max(float x, float y)
{
    return x > y ? x : y;
}

float sdf_circle(struct vec2 p, float r)
{
    return svec2_length(p) - r;
}

static float dot2(struct vec2 p)
{
    return svec2_dot(p, p);
}

float sdf_heart(struct vec2 p)
{
    p.x = fabs(p.x);
    /* p.y = 1 - p.y; */
    /* p.y = p.y - 0.5; */

    if (p.y + p.x > 1.0) {
        return sqrt(dot2(svec2_subtract(p, svec2(0.25, 0.75)))) -
            sqrt(2.0)/4.0;
    }
    return sqrt(sdf_min(dot2(svec2_subtract(p, svec2(0.0, 1.00))),
                    dot2(svec2_subtract_f(p, 0.5*sdf_max(p.x+p.y, 0.0))))) *
                    sdf_sign(p.x - p.y);
}

struct vec2 sdf_heart_center(struct vec2 pos, struct vec2 res)
{
    struct vec2 p;
    p.x = (2.0 * pos.x - res.x) / res.y;
    p.y = (2.0 * (res.y - pos.y) - res.y) / res.y;
    p.y += 0.5;
    return p;
}

float sdf_smoothstep(float e0, float e1, float x)
{
    float t;
    t = clampf((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct vec2 sdf_normalize(struct vec2 pos, struct vec2 res)
{
    struct vec2 p;
    p = svec2_multiply_f(pos, 2.0);
    p = svec2_subtract(p, res);
    p = svec2_divide_f(p, res.y);
    return p;
}

float sdf_rounded_box(struct vec2 pos, struct vec2 b, struct vec4 r)
{
    struct vec2 q;
    float out;

    /* r.xy = (p.x>0.0)?r.xy : r.zw; */

    if (pos.x <= 0.0) {
        r.x = r.z;
        r.y = r.w;
    }

    /* r.x  = (p.y>0.0)?r.x  : r.y; */

    if (pos.y <= 0.0) {
        r.x = r.y;
    }
    /* vec2 q = abs(p)-b+r.x; */

    /* abs(p) */
    q = pos;
    q.x = fabs(q.x);
    q.y = fabs(q.y);

    /* q - b */
    q = svec2_subtract(q, b);

    /* q + r.x */
    q = svec2_add_f(q, r.x);

    /* return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - r.x; */

    /* min(max(q.x, q.y), 0.0) */

    out = sdf_min(sdf_max(q.x, q.y), 0.0);

    /* + length(max(q, 0.0)) */

    out += svec2_length(svec2_max(q, svec2_zero()));

    /* - r.x */
    out -= r.x;

    return out;
}

float sdf_box(struct vec2 p, struct vec2 b)
{
    struct vec2 d;
    float out;

    /* vec2 d = abs(p)-b; */
    d = svec2_abs(p);
    d = svec2_subtract(d, b);

    /* return length(max(d,0.0)) + min(max(d.x,d.y),0.0); */
    out = svec2_length(svec2_max(d, svec2_zero()));
    out += sdf_min(sdf_max(d.x, d.y), 0.0);
    return out;
}

static float ndot(struct vec2 a, struct vec2 b ) {
    return a.x*b.x - a.y*b.y; 
}

float sdf_rhombus(struct vec2 p, struct vec2 b)
{
    float h;
    float d;
    struct vec2 tmp;
    /* p = abs(p) */
    p = svec2_abs(p);
    /* h = clamp(ndot(b-2.0*p,b)/dot(b,b), -1.0, 1.0); */
    tmp = svec2_multiply_f(p, 2.0);
    tmp = svec2_subtract(b, tmp);
    h = ndot(tmp, b) / svec2_dot(b, b);
    h = clampf(h, -1.0, 1.0);
    /* d = length( p-0.5*b*vec2(1.0-h,1.0+h) ); */
    tmp = svec2_multiply_f(b, 0.5);
    tmp = svec2_multiply(tmp, svec2(1.0-h, 1.0+h));
    tmp = svec2_subtract(p, tmp);
    d = svec2_length(tmp);

    /* return d * sign( p.x*b.y + p.y*b.x - b.x*b.y );  */

    return d * sdf_sign(p.x*b.y + p.y*b.x - b.x*b.y);
}

float sdf_equilateral_triangle(struct vec2 p)
{
    const float k = sqrt(3.0);
    p.x = fabs(p.x) - 1.0;
    p.y = p.y + 1.0/k;
    if (p.x + k*p.y > 0.0) {
        p = svec2_multiply_f(svec2(p.x - k*p.y, -k*p.x - p.y), 0.5);
    }

    p.x -= clampf(p.x, -2.0, 0.0);

    return -svec2_length(p) * sdf_sign(p.y);
}

float sdf_pentagon(struct vec2 p, float r)
{
    const struct vec3 k = svec3(0.809016994,0.587785252,0.726542528);
    float tmpf;
    struct vec2 tmp;

    p.x = fabs(p.x);
    /* p -= 2.0*min(dot(vec2(-k.x,k.y),p),0.0)*vec2(-k.x,k.y); */

    tmpf = svec2_dot(svec2(-k.x, k.y), p);
    tmpf = 2.0*sdf_min(tmpf, 0.0);
    tmp = svec2(-k.x, k.y);
    tmp = svec2_multiply_f(tmp, tmpf);
    p = svec2_subtract(p, tmp);

    /* p -= 2.0*min(dot(vec2(+k.x,k.y),p),0.0)*vec2(+k.x,k.y);  */
    tmpf = svec2_dot(svec2(+k.x, k.y), p);
    tmpf = 2.0*sdf_min(tmpf, 0.0);
    tmp = svec2(+k.x, k.y);
    tmp = svec2_multiply_f(tmp, tmpf);
    p = svec2_subtract(p, tmp);

    /* p -= vec2(clamp(p.x,-r*k.z,r*k.z),r); */

    tmp = svec2(clampf(p.x, -r*k.z, r*k.z), r);
    p = svec2_subtract(p, tmp);

    /* return length(p)*sign(p.y); */

    return svec2_length(p) * sdf_sign(p.y);
}
