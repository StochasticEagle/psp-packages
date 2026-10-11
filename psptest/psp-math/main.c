#include <pspkernel.h>
#include <pspmath.h>
#include <psptest.h>
#include <math.h>
#include <stdint.h>

PSP_MODULE_INFO("PSPTEST PSP-Math", 0, 1, 0);

#define PI 3.14159265358979323846f
#define Q14 16384

PSPTEST_COVERS(vfpu_sinf);
PSPTEST_COVERS(vfpu_cosf);
PSPTEST_COVERS(vfpu_sinf_scaled);
PSPTEST_COVERS(vfpu_cosf_scaled);
PSPTEST_COVERS(vfpu_sind_scaled);
PSPTEST_COVERS(vfpu_cosd_scaled);
PSPTEST_COVERS(vfpu_sind_int16);
PSPTEST_COVERS(vfpu_cosd_int16);
PSPTEST_COVERS(vfpu_tan_int16);
PSPTEST_COVERS(vfpu_sinf_int16);
PSPTEST_COVERS(vfpu_cosf_int16);
PSPTEST_COVERS(vfpu_sind_scaled_int16);
PSPTEST_COVERS(vfpu_cosd_scaled_int16);
PSPTEST_COVERS(vfpu_sincos);
PSPTEST_COVERS(vfpu_QuatIdentity_U);
PSPTEST_COVERS(vfpu_Mat4TransformVec4);
PSPTEST_COVERS(vfpu_Vec4Zero);
PSPTEST_COVERS(vfpu_Vec3Zero_U);

static int close_enough(float actual, float expected, float abs_tol) {
    return isfinite(actual) && fabsf(actual - expected) <= abs_tol;
}

#define CHECK_FLOAT(actual, expected, tol) PSPTEST_ASSERT_TRUE(test, close_enough((actual), (expected), (tol)))
#define CHECK_INT(actual, expected) PSPTEST_ASSERT_EQ_INT(test, (expected), (actual))
#define CHECK_Q14(actual, expected) do { int delta = (int)(actual) - (int)(expected); PSPTEST_ASSERT_TRUE(test, delta >= -1 && delta <= 1); } while (0)

PSPTEST_TEST(radian_trigonometry) {
    static const float angles[] = { -PI, -PI * 0.5f, -0.25f, 0.0f, 0.25f, PI * 0.5f, PI };
    unsigned i;
    for (i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i) {
        float s, c;
        float a = angles[i];
        CHECK_FLOAT(vfpu_sinf(a), sinf(a), 0.003f);
        CHECK_FLOAT(vfpu_cosf(a), cosf(a), 0.003f);
        CHECK_FLOAT(vfpu_sinf_scaled(a, 2.5f), 2.5f * sinf(a), 0.008f);
        CHECK_FLOAT(vfpu_cosf_scaled(a, -1.5f), -1.5f * cosf(a), 0.005f);
        vfpu_sincos(a, &s, &c);
        CHECK_FLOAT(s, sinf(a), 0.003f);
        CHECK_FLOAT(c, cosf(a), 0.003f);
    }
}

PSPTEST_TEST(degree_trigonometry) {
    static const int angles[] = { -1080, -450, -360, -270, -180, -90, -1, 0, 1, 45, 90, 180, 270, 359, 360, 450, 720, 1080 };
    unsigned i;
    for (i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i) {
        float rad = (float)angles[i] * (PI / 180.0f);
        CHECK_FLOAT(vfpu_sind_scaled((float)angles[i], 2.0f), 2.0f * sinf(rad), 0.008f);
        CHECK_FLOAT(vfpu_cosd_scaled((float)angles[i], 2.0f), 2.0f * cosf(rad), 0.008f);
        CHECK_Q14(vfpu_sind_int16(angles[i]), (int)(sinf(rad) * Q14 + (sinf(rad) >= 0 ? 0.5f : -0.5f)));
        CHECK_Q14(vfpu_cosd_int16(angles[i]), (int)(cosf(rad) * Q14 + (cosf(rad) >= 0 ? 0.5f : -0.5f)));
    }
    CHECK_INT(vfpu_tan_int16(90), 32767);
    CHECK_INT(vfpu_tan_int16(270), 32767);
    CHECK_INT(vfpu_tan_int16(0), 0);
    CHECK_INT(vfpu_tan_int16(180), 0);
}

PSPTEST_TEST(q14_fixed_point) {
    CHECK_INT(vfpu_sinf_int16(0), 0);
    CHECK_INT(vfpu_cosf_int16(0), Q14);
    CHECK_INT(vfpu_sind_scaled_int16(90, Q14), Q14);
    CHECK_INT(vfpu_cosd_scaled_int16(0, Q14), Q14);
    CHECK_INT(vfpu_sind_scaled_int16(-90, Q14), -Q14);
    CHECK_INT(vfpu_cosd_scaled_int16(180, Q14), -Q14);
    CHECK_INT(vfpu_sind_scaled_int16(90, 32767), 32767);
}

PSPTEST_TEST(vector_matrix_quaternion) {
    ScePspFVector4 in PSPMATH_ALIGN = { 1.0f, 2.0f, 3.0f, 1.0f };
    ScePspFVector4 out PSPMATH_ALIGN = { 0 };
    ScePspFVector4 zero PSPMATH_ALIGN = { 1, 1, 1, 1 };
    ScePspFVector3 v3 PSPMATH_ALIGN = { 1, 2, 3 };
    ScePspFMatrix4 identity PSPMATH_ALIGN = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };
    float q[4] PSPMATH_ALIGN = { 1, 2, 3, 4 };
    float *a = (float *)&out;
    vfpu_Mat4TransformVec4(&identity, &in, &out);
    CHECK_FLOAT(a[0], 1.0f, 0.0001f);
    CHECK_FLOAT(a[1], 2.0f, 0.0001f);
    CHECK_FLOAT(a[2], 3.0f, 0.0001f);
    CHECK_FLOAT(a[3], 1.0f, 0.0001f);
    vfpu_Vec4Zero(&zero);
    for (int i = 0; i < 4; ++i) CHECK_FLOAT(((float *)&zero)[i], 0.0f, 0.0001f);
    vfpu_Vec3Zero_U(&v3);
    for (int i = 0; i < 3; ++i) CHECK_FLOAT(((float *)&v3)[i], 0.0f, 0.0001f);
    vfpu_QuatIdentity_U(q);
    CHECK_FLOAT(q[0], 0.0f, 0.0001f);
    CHECK_FLOAT(q[1], 0.0f, 0.0001f);
    CHECK_FLOAT(q[2], 0.0f, 0.0001f);
    CHECK_FLOAT(q[3], 1.0f, 0.0001f);
}

static const PspTestCase cases[] = {
    PSPTEST_CASE(radian_trigonometry),
    PSPTEST_CASE(degree_trigonometry),
    PSPTEST_CASE(q14_fixed_point),
    PSPTEST_CASE(vector_matrix_quaternion)
};

PSPTEST_MODULE("packages/psp-math", cases, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL, NULL)
