// pumakins — PUMA robot kinematics (cmod version)
// Derived from work by Fred Proctor. License: GPL Version 2

#include <math.h>
#include <string.h>
#include "gomc_env.h"
#include "switchkins_cmod.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define REQUIRED_COORDINATES "XYZABC"

// ─── Constants ───

#define DEFAULT_PUMA560_A2 300.0
#define DEFAULT_PUMA560_A3  50.0
#define DEFAULT_PUMA560_D3  70.0
#define DEFAULT_PUMA560_D4 400.0
#define DEFAULT_PUMA560_D6  70.0

#define SINGULAR_FUZZ 0.000001
#define FLAG_FUZZ     0.000001

#define PUMA_SHOULDER_RIGHT 0x01
#define PUMA_ELBOW_DOWN     0x02
#define PUMA_WRIST_FLIP     0x04
#define PUMA_SINGULAR       0x08
#define PUMA_REACH          0x01

// ─── Inline rotation matrix helpers ───
// Convention: R = Rz(yaw) * Ry(pitch) * Rx(roll) (ZYX Euler)
// RPY: r=roll→a, p=pitch→b, y=yaw→c

typedef struct { double xx,xy,xz, yx,yy,yz, zx,zy,zz; } rot3_t;

static void rpy_to_rot(double r, double p, double y, rot3_t *R) {
    double sr = sin(r), cr = cos(r);
    double sp = sin(p), cp = cos(p);
    double sy = sin(y), cy = cos(y);
    R->xx = cr*cp;          R->xy = cr*sp*sy - sr*cy; R->xz = cr*sp*cy + sr*sy;
    R->yx = sr*cp;          R->yy = sr*sp*sy + cr*cy; R->yz = sr*sp*cy - cr*sy;
    R->zx = -sp;            R->zy = cp*sy;            R->zz = cp*cy;
}

static void rot_to_rpy(const rot3_t *R, double *roll, double *pitch, double *yaw) {
    *pitch = atan2(-R->zx, sqrt(R->xx * R->xx + R->yx * R->yx));
    *roll  = atan2(R->yx, R->xx);
    *yaw   = atan2(R->zy, R->zz);
}

// ─── Module state ───

static const gomc_hal_t  *g_hal;
static int                g_comp_id;
static sk_map_t           g_map;
static sk_switch_t        g_sw;

struct haldata {
    gomc_hal_float_t *a2, *a3, *d3, *d4, *d6;
};
static struct haldata *haldata;

#define PUMA_A2 (*(haldata->a2))
#define PUMA_A3 (*(haldata->a3))
#define PUMA_D3 (*(haldata->d3))
#define PUMA_D4 (*(haldata->d4))
#define PUMA_D6 (*(haldata->d6))

// ─── PUMA forward ───

static int puma_forward(const double joints[KINS_MAX_JOINTS],
                        kins_pose_t *world)
{
    double s1, s2, s3, s4, s5, s6;
    double c1, c2, c3, c4, c5, c6;
    double s23, c23;
    double t1, t2, t3, t4;
    rot3_t R;
    double tx, ty, tz;

    s1 = sin(joints[0]*M_PI/180); c1 = cos(joints[0]*M_PI/180);
    s2 = sin(joints[1]*M_PI/180); c2 = cos(joints[1]*M_PI/180);
    s3 = sin(joints[2]*M_PI/180); c3 = cos(joints[2]*M_PI/180);
    s4 = sin(joints[3]*M_PI/180); c4 = cos(joints[3]*M_PI/180);
    s5 = sin(joints[4]*M_PI/180); c5 = cos(joints[4]*M_PI/180);
    s6 = sin(joints[5]*M_PI/180); c6 = cos(joints[5]*M_PI/180);

    s23 = c2*s3 + s2*c3;
    c23 = c2*c3 - s2*s3;

    // First column of rotation matrix
    t1 = c4*c5*c6 - s4*s6;
    t2 = s23*s5*c6;
    t3 = s4*c5*c6 + c4*s6;
    t4 = c23*t1 - t2;
    R.xx = c1*t4 + s1*t3;
    R.xy = s1*t4 - c1*t3; // actually row x col y
    R.xz = -s23*t1 - c23*s5*c6;

    // Second column
    t1 = -c4*c5*s6 - s4*c6;
    t2 = s23*s5*s6;
    t3 = c4*c6 - s4*c5*s6;
    t4 = c23*t1 + t2;
    R.yx = c1*t4 + s1*t3;
    R.yy = s1*t4 - c1*t3;
    R.yz = -s23*t1 + c23*s5*s6;

    // Third column
    t1 = c23*c4*s5 + s23*c5;
    R.zx = -c1*t1 - s1*s4*s5;
    R.zy = -s1*t1 + c1*s4*s5;
    R.zz = s23*c4*s5 - c23*c5;

    // Position
    t1 = PUMA_A2*c2 + PUMA_A3*c23 - PUMA_D4*s23;
    tx = c1*t1 - PUMA_D3*s1;
    ty = s1*t1 + PUMA_D3*c1;
    tz = -PUMA_A3*s23 - PUMA_A2*s2 - PUMA_D4*c23;

    // D6 effect
    tx += R.zx * PUMA_D6;
    ty += R.zy * PUMA_D6;
    tz += R.zz * PUMA_D6;

    world->x = tx;
    world->y = ty;
    world->z = tz;

    // NOTE: pumakins stores the rotation matrix as:
    //   Row 0: (R.xx, R.yx, R.zx) — but wait, the legacy code builds
    //   hom.rot.x.x = first col first row, etc.
    // The legacy Layout is hom.rot.{x,y,z} = rows, .{x,y,z} = columns
    // Here R.{xx,xy,xz} = row 0  (matches hom.rot.x.{x,y,z})
    //      R.{yx,yy,yz} = row 1  (matches hom.rot.y.{x,y,z})
    //      R.{zx,zy,zz} = row 2  (matches hom.rot.z.{x,y,z})
    // The rot_to_rpy function expects this layout.
    double roll, pitch, yaw;
    rot_to_rpy(&R, &roll, &pitch, &yaw);
    world->a = roll  * 180.0 / M_PI;
    world->b = pitch * 180.0 / M_PI;
    world->c = yaw   * 180.0 / M_PI;

    world->u = 0; world->v = 0; world->w = 0;
    return 0;
}

// ─── PUMA inverse ───

static int puma_inverse(const kins_pose_t *world,
                        double joints[KINS_MAX_JOINTS])
{
    rot3_t R;
    double t1, t2, t3, k, sumSq;
    double th1, th2, th3, th23, th4, th5, th6;
    double s1, c1, s3, c3, s23, c23, s4, c4, s5, c5, s6, c6;
    double px, py, pz;

    // RPY → rotation matrix
    double roll  = world->a * M_PI / 180.0;
    double pitch = world->b * M_PI / 180.0;
    double yaw   = world->c * M_PI / 180.0;
    rpy_to_rot(roll, pitch, yaw, &R);

    // Remove D6 effect
    px = world->x - PUMA_D6 * R.zx;
    py = world->y - PUMA_D6 * R.zy;
    pz = world->z - PUMA_D6 * R.zz;

    // Joint 1
    sumSq = px*px + py*py - PUMA_D3*PUMA_D3;
    // Default: shoulder left
    th1 = atan2(py, px) - atan2(PUMA_D3, sqrt(fabs(sumSq)));

    s1 = sin(th1); c1 = cos(th1);

    // Joint 3
    k = (sumSq + pz*pz - PUMA_A2*PUMA_A2 - PUMA_A3*PUMA_A3 -
         PUMA_D4*PUMA_D4) / (2.0 * PUMA_A2);
    double d34sq = PUMA_A3*PUMA_A3 + PUMA_D4*PUMA_D4 - k*k;
    if (d34sq < 0) d34sq = 0;
    // Default: elbow up
    th3 = atan2(PUMA_A3, PUMA_D4) - atan2(k, sqrt(d34sq));

    s3 = sin(th3); c3 = cos(th3);

    // Joint 2
    t1 = (-PUMA_A3 - PUMA_A2*c3)*pz +
         (c1*px + s1*py)*(PUMA_A2*s3 - PUMA_D4);
    t2 = (PUMA_A2*s3 - PUMA_D4)*pz +
         (PUMA_A3 + PUMA_A2*c3)*(c1*px + s1*py);
    t3 = pz*pz + (c1*px + s1*py)*(c1*px + s1*py);

    th23 = atan2(t1, t2);
    th2 = th23 - th3;
    s23 = t1/t3; c23 = t2/t3;

    // Joint 4
    t1 = -R.zx*s1 + R.zy*c1;
    t2 = -R.zx*c1*c23 - R.zy*s1*c23 + R.zz*s23;
    if (fabs(t1) < SINGULAR_FUZZ && fabs(t2) < SINGULAR_FUZZ)
        th4 = joints[3]*M_PI/180; // singular: keep current
    else
        th4 = atan2(t1, t2);

    s4 = sin(th4); c4 = cos(th4);

    // Joint 5
    s5 = R.zz*(s23*c4) - R.zx*(c1*c23*c4 + s1*s4)
                        - R.zy*(s1*c23*c4 - c1*s4);
    c5 = -R.zx*(c1*s23) - R.zy*(s1*s23) - R.zz*c23;
    th5 = atan2(s5, c5);

    // Joint 6
    s6 = R.xx*(s23*s4) - R.xx*(c1*c23*s4 - s1*c4)
                        - R.xy*(s1*c23*s4 + c1*c4);
    // Wait, legacy code uses hom.rot.x.z for s6 and hom.rot.x.x for c6
    // Let me recopy exactly from legacy:

    // Actually: in legacy code, hom.rot.{x,y,z} are the ROWS of the rotation matrix
    // hom.rot.x = first row  →  R.xx, R.xy, R.xz  (our naming)
    // But wait, legacy builds the rotation matrix differently. Let me re-examine.
    //
    // Legacy builds: hom.rot.x.x, hom.rot.x.y, hom.rot.x.z = first ROW
    //                hom.rot.y.x, hom.rot.y.y, hom.rot.y.z = second ROW
    //                hom.rot.z.x, hom.rot.z.y, hom.rot.z.z = third ROW
    //
    // Joint 6 in legacy:
    //   s6 = hom.rot.x.z*(s23*s4) - hom.rot.x.x*(c1*c23*s4 - s1*c4)
    //                               - hom.rot.x.y*(s1*c23*s4 + c1*c4)
    // where hom.rot.x = first row of R
    //   hom.rot.x.x = R[0][0] = our R.xx
    //   hom.rot.x.y = R[0][1] = our R.xy
    //   hom.rot.x.z = R[0][2] = our R.xz

    s6 = R.xz*(s23*s4) - R.xx*(c1*c23*s4 - s1*c4)
                        - R.xy*(s1*c23*s4 + c1*c4);
    c6 = R.xx*((c1*c23*c4 + s1*s4)*c5 - c1*s23*s5) +
         R.xy*((s1*c23*c4 - c1*s4)*c5 - s1*s23*s5) -
         R.xz*(s23*c4*c5 + c23*s5);
    th6 = atan2(s6, c6);

    joints[0] = th1*180.0/M_PI;
    joints[1] = th2*180.0/M_PI;
    joints[2] = th3*180.0/M_PI;
    joints[3] = th4*180.0/M_PI;
    joints[4] = th5*180.0/M_PI;
    joints[5] = th6*180.0/M_PI;
    return 0;
}

// ─── Dispatch ───

static int dispatch_forward(
    const double joints[KINS_MAX_JOINTS], kins_pose_t *world,
    uint64_t fflags, uint64_t *iflags, int32_t *out)
{
    (void)fflags; (void)iflags;
    switch (g_sw.current_type) {
        case 0:  *out = puma_forward(joints, world); return 0;
        default: sk_identity_forward(&g_map, joints, world);
                 *out = 0; return 0;
    }
}

static int dispatch_inverse(
    const kins_pose_t *world, double joints[KINS_MAX_JOINTS],
    uint64_t iflags, uint64_t *fflags, int32_t *out)
{
    (void)iflags; (void)fflags;
    switch (g_sw.current_type) {
        case 0:  *out = puma_inverse(world, joints); return 0;
        default: sk_identity_inverse(&g_map, world, joints);
                 *out = 0; return 0;
    }
}

static int dispatch_type(kins_kinematics_type_t *out)
    { *out = KINS_BOTH; return 0; }
static int dispatch_switchable(int32_t *out) { *out = 1; return 0; }
static int dispatch_switch(int32_t t, int32_t *out)
    { *out = sk_switch_to(&g_sw, t); return 0; }

static kins_callbacks_t puma_callbacks = {
    .forward    = dispatch_forward,
    .inverse    = dispatch_inverse,
    .type       = dispatch_type,
    .switchable = dispatch_switchable,
    .switch_    = dispatch_switch,
};

// ─── cmod lifecycle ───

static cmod_t puma_cmod;

static void puma_destroy(cmod_t *self) {
    (void)self;
    if (g_hal && g_comp_id > 0) g_hal->exit(g_hal->ctx, g_comp_id);
}

int New(const cmod_env_t *env, const char *name,
        int argc, const char **argv, cmod_t **out)
{
    if (!env->hal) {
        gomc_log_errorf(env->log, name, "HAL API not available");
        return -1;
    }
    g_hal = env->hal;

    const char *coordinates = REQUIRED_COORDINATES;
    for (int i = 0; i < argc; i++)
        if (strncmp(argv[i], "coordinates=", 12) == 0)
            coordinates = argv[i] + 12;
    if (env->ini) {
        const char *v = env->ini->get(env->ini->ctx, "KINS", "COORDINATES");
        if (v) coordinates = v;
    }

    if (sk_map_coordinates(&g_map, coordinates, 0) < 0) {
        gomc_log_errorf(env->log, name, "bad coordinates: %s", coordinates);
        return -1;
    }

    g_comp_id = env->hal->init(env->hal->ctx, name, env->dl_handle,
                               GOMC_HAL_COMP_REALTIME);
    if (g_comp_id < 0) return g_comp_id;

    haldata = env->hal->malloc(env->hal->ctx, sizeof(struct haldata));
    if (!haldata) { g_hal->exit(g_hal->ctx, g_comp_id); return -1; }

    int rc = 0;
    rc |= gomc_hal_pin_float_newf(env->hal, GOMC_HAL_IN, &haldata->a2,
                                  g_comp_id, "%s.A2", name);
    rc |= gomc_hal_pin_float_newf(env->hal, GOMC_HAL_IN, &haldata->a3,
                                  g_comp_id, "%s.A3", name);
    rc |= gomc_hal_pin_float_newf(env->hal, GOMC_HAL_IN, &haldata->d3,
                                  g_comp_id, "%s.D3", name);
    rc |= gomc_hal_pin_float_newf(env->hal, GOMC_HAL_IN, &haldata->d4,
                                  g_comp_id, "%s.D4", name);
    rc |= gomc_hal_pin_float_newf(env->hal, GOMC_HAL_IN, &haldata->d6,
                                  g_comp_id, "%s.D6", name);
    if (rc < 0) goto fail;

    PUMA_A2 = DEFAULT_PUMA560_A2;
    PUMA_A3 = DEFAULT_PUMA560_A3;
    PUMA_D3 = DEFAULT_PUMA560_D3;
    PUMA_D4 = DEFAULT_PUMA560_D4;
    PUMA_D6 = DEFAULT_PUMA560_D6;

    rc = sk_create_switch_pins(env->hal, g_comp_id, &g_sw);
    if (rc < 0) goto fail;

    env->hal->ready(env->hal->ctx, g_comp_id);

    rc = kins_api_register(env->api, "kinematics", &puma_callbacks);
    if (rc != 0) {
        gomc_log_errorf(env->log, name, "kins_api_register failed: %d", rc);
        goto fail;
    }

    puma_cmod.Destroy = puma_destroy;
    *out = &puma_cmod;
    return 0;

fail:
    g_hal->exit(g_hal->ctx, g_comp_id);
    return rc;
}
