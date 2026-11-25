/* quat.h, euler.h and transform.h against small independent references written out
 * below: Rodrigues rotation matrices, plain row-by-column products, and Eigen's inverse.
 */
#include "litestl/math/euler.h"
#include "litestl/math/quat.h"
#include "litestl/math/transform.h"
#include "splitmix64.h"
#include "test_harness.h"

#include <cmath>
#include <limits>

test_init;

using namespace litestl;
using namespace litestl::math;

namespace {

/* m[r][c], rows first, unlike Matrix. */
template <typename F> struct Ref3 {
  F m[3][3] = {};
};

struct Rand {
  test::SplitMix64 gen;
  explicit Rand(uint64_t seed) : gen(seed)
  {
  }
  double unit()
  {
    return double(gen.next() >> 11) * 0x1p-53;
  }
  double range(double a, double b)
  {
    return a + (b - a) * unit();
  }
  int below(int n)
  {
    return int(gen.next() % uint64_t(n));
  }
};

template <typename F> Vec<F, 3> randVec(Rand &r, double size = 1.0)
{
  return Vec<F, 3>(
      F(r.range(-size, size)), F(r.range(-size, size)), F(r.range(-size, size)));
}

template <typename F> Quat<F> randQuat(Rand &r)
{
  Quat<F> q(F(r.range(-1, 1)), F(r.range(-1, 1)), F(r.range(-1, 1)), F(r.range(-1, 1)));
  return q.normalize();
}

template <typename F> Ref3<F> rodrigues(const Vec<F, 3> &axis, F angle)
{
  const F len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
  const F x = axis[0] / len, y = axis[1] / len, z = axis[2] / len;
  const F c = std::cos(angle), s = std::sin(angle), t = F(1) - c;
  Ref3<F> r;
  r.m[0][0] = t * x * x + c;
  r.m[0][1] = t * x * y - s * z;
  r.m[0][2] = t * x * z + s * y;
  r.m[1][0] = t * x * y + s * z;
  r.m[1][1] = t * y * y + c;
  r.m[1][2] = t * y * z - s * x;
  r.m[2][0] = t * x * z - s * y;
  r.m[2][1] = t * y * z + s * x;
  r.m[2][2] = t * z * z + c;
  return r;
}

template <typename F> Ref3<F> mul(const Ref3<F> &a, const Ref3<F> &b)
{
  Ref3<F> r;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      for (int k = 0; k < 3; k++) {
        r.m[i][j] += a.m[i][k] * b.m[k][j];
      }
    }
  }
  return r;
}

template <typename F, int N> F diff(const Matrix<F, N> &m, const Ref3<F> &r)
{
  const F *d = m;
  F worst = F(0);
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      worst = (std::max)(worst, std::fabs(d[j * N + i] - r.m[i][j]));
    }
  }
  return worst;
}

template <typename F> F diff(const Vec<F, 3> &a, const Vec<F, 3> &b)
{
  return (std::max)((std::max)(std::fabs(a[0] - b[0]), std::fabs(a[1] - b[1])),
                    std::fabs(a[2] - b[2]));
}

template <typename F, int N> F diff(const Matrix<F, N> &a, const Matrix<F, N> &b)
{
  const F *p = a, *q = b;
  F worst = F(0);
  for (int i = 0; i < N * N; i++) {
    const F e = std::fabs(p[i] - q[i]);
    worst = e <= worst ? worst : e;
  }
  return worst;
}

template <typename F> Vec<F, 3> apply(const Ref3<F> &m, const Vec<F, 3> &v)
{
  Vec<F, 3> r;
  for (int i = 0; i < 3; i++) {
    r[i] = m.m[i][0] * v[0] + m.m[i][1] * v[1] + m.m[i][2] * v[2];
  }
  return r;
}

template <typename F> bool isIdentity(const Quat<F> &q)
{
  return q[0] == F(0) && q[1] == F(0) && q[2] == F(0) && q[3] == F(1);
}

/** How far apart two rotations are, whatever the signs. */
template <typename F> F rotationDiff(const Quat<F> &a, const Quat<F> &b)
{
  F plus = F(0), minus = F(0);
  for (int i = 0; i < 4; i++) {
    plus = (std::max)(plus, std::fabs(a[i] - b[i]));
    minus = (std::max)(minus, std::fabs(a[i] + b[i]));
  }
  return (std::min)(plus, minus);
}

template <typename F> void testQuat(const char *name)
{
  const F tol = std::numeric_limits<F>::epsilon() * F(32);
  Rand rnd(0x1234);
  test::FailureLog log(name);
  for (int64_t n = test::cases(20000); n > 0; n--) {
    log.count_case();
    const Vec<F, 3> axis = randVec<F>(rnd), axis2 = randVec<F>(rnd), v = randVec<F>(rnd);
    const F angle = F(rnd.range(-7, 7)), angle2 = F(rnd.range(-7, 7));
    const Ref3<F> ra = rodrigues(axis, angle), rb = rodrigues(axis2, angle2);
    const Quat<F> a = Quat<F>::fromAxisAngle(axis, angle);
    const Quat<F> b = Quat<F>::fromAxisAngle(axis2, angle2);
    log.check(diff(a.toMatrix3(), ra) < tol, [] { fprintf(stderr, "toMatrix3"); });
    log.check(diff(a.toMatrix4(), ra) < tol, [] { fprintf(stderr, "toMatrix4"); });
    log.check(diff((a * b).toMatrix3(), mul(ra, rb)) < tol,
              [] { fprintf(stderr, "product"); });
    log.check(diff(Matrix<F, 3>(a.toMatrix3() * b.toMatrix3()), mul(ra, rb)) < tol,
              [] { fprintf(stderr, "the matrix product's convention"); });
    log.check(diff(a.rotate(v), apply(ra, v)) < tol, [] { fprintf(stderr, "rotate"); });
    log.check(diff(a.conjugated().rotate(a.rotate(v)), v) < tol,
              [] { fprintf(stderr, "conjugated"); });
    log.check(rotationDiff(Quat<F>::fromMatrix(a.toMatrix3()), a) < tol &&
                  rotationDiff(Quat<F>::fromMatrix(a.toMatrix4()), a) < tol,
              [] { fprintf(stderr, "fromMatrix"); });

    Vec<F, 3> axisOut;
    F angleOut;
    a.toAxisAngle(axisOut, angleOut);
    log.check(angleOut >= F(0) && angleOut <= F(3.1415927) + tol &&
                  rotationDiff(Quat<F>::fromAxisAngle(axisOut, angleOut), a) < tol,
              [] { fprintf(stderr, "toAxisAngle"); });

    /* slerp turns at a constant rate around one axis. */
    const F t = F(rnd.unit());
    const F small = F(rnd.range(0, 3));
    const Quat<F> to = Quat<F>::fromAxisAngle(axis, small) * b;
    const Quat<F> want = Quat<F>::fromAxisAngle(axis, small * t) * b;
    log.check(rotationDiff(slerp(b, to, t), want) < tol * F(4),
              [] { fprintf(stderr, "slerp"); });
    log.check(rotationDiff(slerp(b, -to, t), want) < tol * F(4),
              [] { fprintf(stderr, "slerp takes the shorter arc"); });
    const F tiny = F(1e-4) * F(rnd.unit());
    log.check(rotationDiff(slerp(b, Quat<F>::fromAxisAngle(axis, tiny) * b, t),
                           Quat<F>::fromAxisAngle(axis, tiny * t) * b) < tol,
              [] { fprintf(stderr, "slerp of a small angle"); });
  }
  log.finish();

  const F inf = std::numeric_limits<F>::infinity(),
          nan = std::numeric_limits<F>::quiet_NaN();
  const F big = (std::numeric_limits<F>::max)(),
          den = std::numeric_limits<F>::denorm_min();
  test_assert(isIdentity(Quat<F>()));
  test_assert(isIdentity(Quat<F>(0, 0, 0, 0).normalized()));
  test_assert(isIdentity(Quat<F>(inf, 0, 0, 1).normalized()));
  test_assert(isIdentity(Quat<F>(0, nan, 0, 1).normalized()));
  test_assert(isIdentity(Quat<F>::fromAxisAngle(Vec<F, 3>(), F(1))));
  test_assert(isIdentity(Quat<F>::fromAxisAngle(Vec<F, 3>(nan, 0, 0), F(1))));
  const F third = F(1) / std::sqrt(F(3));
  test_assert(std::fabs(Quat<F>(big, -big, big, 0).normalized()[1] + third) < tol);
  test_assert(Quat<F>(0, 0, -den, 0).normalized()[2] == F(-1));
  Vec<F, 3> axis;
  F angle;
  Quat<F>().toAxisAngle(axis, angle);
  test_assert(angle == F(0) && axis[0] == F(1));
  Quat<F>(0, 0, 0, -1).toAxisAngle(axis, angle);
  test_assert(angle == F(0) && axis[0] == F(1));
}

template <typename F> void testEuler(const char *name)
{
  const F eps = std::numeric_limits<F>::epsilon();
  const F pi = F(3.14159265358979323846);
  /* Axis of the first, second and third rotation, by EulerOrder. */
  const int orders[6][3] = {
      {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  Rand rnd(0x4321);
  test::FailureLog log(name);
  for (int64_t n = test::cases(20000); n > 0; n--) {
    log.count_case();
    const int o = rnd.below(6);
    const EulerOrder order = EulerOrder(o);
    const Vec<F, 3> a = randVec<F>(rnd, 12);
    Ref3<F> want;
    for (int i = 0; i < 3; i++) {
      want.m[i][i] = F(1);
    }
    for (int k = 0; k < 3; k++) {
      Vec<F, 3> axis;
      axis[orders[o][k]] = F(1);
      want = mul(rodrigues(axis, a[orders[o][k]]), want);
    }
    const Quat<F> q = eulerToQuat(a, order);
    log.check(diff(q.toMatrix3(), want) < eps * F(32),
              [&] { fprintf(stderr, "eulerToQuat order %d", o); });

    /* Any hint: the same rotation comes back, and the hint itself if it is an answer. */
    const Vec<F, 3> b = quatToEuler(q, order, randVec<F>(rnd, 12));
    log.check(rotationDiff(eulerToQuat(b, order), q) < eps * F(64),
              [&] { fprintf(stderr, "round trip order %d", o); });
    if (std::fabs(std::cos(a[orders[o][1]])) > F(0.05)) {
      log.check(diff(quatToEuler(q, order, a), a) < eps * F(2000),
                [&] { fprintf(stderr, "hint is kept, order %d", o); });
      const Vec<F, 3> nearHint = a + randVec<F>(rnd, 0.3);
      log.check(diff(quatToEuler(q, order, nearHint), a) < eps * F(2000) ||
                    std::fabs(std::cos(a[orders[o][1]])) < F(0.5),
                [&] { fprintf(stderr, "near hint, order %d", o); });
    }

    /* At and near gimbal lock the rotation is still right, and the last angle is the
     * hint's at the lock itself. */
    Vec<F, 3> g = randVec<F>(rnd, 3);
    const F off[8] = {
        F(0), eps * F(4), eps * F(100), eps * F(1e4), F(1e-7), F(1e-5), F(1e-3), F(1e-2)};
    g[orders[o][1]] =
        (rnd.below(2) ? pi : -pi) / F(2) + off[rnd.below(8)] * F(rnd.range(-1, 1));
    const Quat<F> gq = eulerToQuat(g, order);
    const Vec<F, 3> gb = quatToEuler(gq, order, g);
    log.check(rotationDiff(eulerToQuat(gb, order), gq) < eps * F(64),
              [&] { fprintf(stderr, "gimbal lock, order %d", o); });
    const Vec<F, 3> gc = quatToEuler(gq, order, randVec<F>(rnd, 12));
    log.check(rotationDiff(eulerToQuat(gc, order), gq) < eps * F(64),
              [&] { fprintf(stderr, "gimbal lock, any hint, order %d", o); });
  }
  log.finish();

  /* An exact lock: y is a quarter turn, built from exact components. */
  const F h = std::sqrt(F(0.5));
  const Quat<F> lock(F(0), h, F(0), h);
  const Vec<F, 3> a = quatToEuler(lock, EulerOrder::XYZ, Vec<F, 3>(F(0), F(0), F(0.7)));
  test_assert(std::fabs(a[2] - F(0.7)) < eps * F(64));
  test_assert(rotationDiff(eulerToQuat(a, EulerOrder::XYZ), lock) < eps * F(64));

  /* A drag through three turns stays continuous. */
  Vec<F, 3> angles;
  for (int i = 1; i <= 1080; i++) {
    const Quat<F> q = Quat<F>::fromAxisAngle(Vec<F, 3>(0, 0, 1), F(i) * pi / F(180));
    angles = quatToEuler(q, EulerOrder::XYZ, angles);
  }
  test_assert(diff(angles, Vec<F, 3>(F(0), F(0), F(6) * pi)) < F(1e-4));
}

template <typename F> void testTransforms(const char *name)
{
  const F eps = std::numeric_limits<F>::epsilon();
  Rand rnd(0x9876);
  test::FailureLog log(name);
  for (int64_t n = test::cases(20000); n > 0; n--) {
    log.count_case();
    Matrix<F, 4> m;
    for (int c = 0; c < 4; c++) {
      for (int r = 0; r < 4; r++) {
        m[c][r] = F(rnd.range(-2, 2));
      }
    }
    const Vec<F, 4> v(
        F(rnd.range(-1, 1)), F(rnd.range(-1, 1)), F(rnd.range(-1, 1)), F(1));
    Vec<F, 4> want;
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        want[r] += m[c][r] * v[c];
      }
    }
    const Vec<F, 4> got = transformHomogeneous(m, v);
    const Vec<F, 3> p(v[0], v[1], v[2]);
    const Vec<F, 3> gp = transformPoint(m, p), gv = transformVector(m, p);
    bool ok = true;
    for (int r = 0; r < 4; r++) {
      ok &= std::fabs(got[r] - want[r]) < eps * F(64);
      if (r < 3) {
        ok &= std::fabs(gp[r] - want[r]) < eps * F(64);
        ok &= std::fabs(gv[r] + m[3][r] - want[r]) < eps * F(64);
      }
    }
    log.check(ok, [] { fprintf(stderr, "transform*"); });

    /* A column of the product is the product with a column. */
    Matrix<F, 4> m2;
    for (int c = 0; c < 4; c++) {
      for (int r = 0; r < 4; r++) {
        m2[c][r] = F(rnd.range(-2, 2));
      }
    }
    Matrix<F, 4> prod = m * m2;
    const Vec<F, 4> viaProduct = transformHomogeneous(prod, v);
    const Vec<F, 4> viaSteps = transformHomogeneous(m, transformHomogeneous(m2, v));
    ok = true;
    for (int r = 0; r < 4; r++) {
      ok &= std::fabs(viaProduct[r] - viaSteps[r]) < eps * F(512);
    }
    log.check(ok, [] { fprintf(stderr, "composition order"); });

    const Vec<F, 4> plane(F(rnd.range(-1, 1)),
                          F(rnd.range(-1, 1)),
                          F(rnd.range(-1, 1)),
                          F(rnd.range(-1, 1)));
    log.check(std::fabs(planeToLocal(m, plane).dot(v) - plane.dot(want)) < eps * F(256),
              [] { fprintf(stderr, "planeToLocal"); });

    /* TRS, with signs: at most one reflection can come back, on x. */
    const Vec<F, 3> t = randVec<F>(rnd, 5);
    const Quat<F> q = randQuat<F>(rnd);
    Vec<F, 3> s;
    int negative = 0;
    for (int i = 0; i < 3; i++) {
      s[i] = F(std::exp(rnd.range(-3, 3)));
      if (rnd.below(3) == 0) {
        s[i] = -s[i];
        negative++;
      }
    }
    const Matrix<F, 4> trs = composeTrs(t, q, s);
    const Vec<F, 3> viaTrs = transformPoint(trs, p);
    log.check(diff(viaTrs, q.rotate(p * s) + t) < eps * F(512),
              [] { fprintf(stderr, "composeTrs"); });
    Vec<F, 3> t2, s2;
    Quat<F> q2;
    F shear = decomposeTrs(trs, t2, q2, s2);
    log.check(shear < eps * F(32) && s2[1] > F(0) && s2[2] > F(0) &&
                  (s2[0] < F(0)) == (negative % 2 == 1) &&
                  diff(composeTrs(t2, q2, s2), trs) < eps * F(512),
              [] { fprintf(stderr, "decomposeTrs"); });

    /* Zero scales: the matrix comes back, and the rotation is a rotation. */
    const int mask = 1 + rnd.below(7);
    for (int i = 0; i < 3; i++) {
      if (mask >> i & 1) {
        s[i] = F(0);
      }
    }
    const Matrix<F, 4> flat = composeTrs(t, q, s);
    decomposeTrs(flat, t2, q2, s2);
    log.check(std::fabs(q2.dot(q2) - F(1)) < eps * F(32) &&
                  diff(composeTrs(t2, q2, s2), flat) < eps * F(512),
              [&] { fprintf(stderr, "decomposeTrs of zero scales, mask %d", mask); });

    const F amount = F(rnd.range(0.01, 1));
    Matrix<F, 4> sheared;
    sheared.identity();
    sheared[1][0] = amount;
    shear = decomposeTrs(sheared, t2, q2, s2);
    log.check(std::fabs(shear - amount / std::sqrt(F(1) + amount * amount)) < eps * F(32),
              [] { fprintf(stderr, "shear"); });
  }
  log.finish();

  /* Two equal columns, and a non-finite one, still give a rotation. */
  Matrix<F, 4> m;
  m.identity();
  m[1] = m[0];
  Vec<F, 3> t, s;
  Quat<F> q;
  test_assert(std::fabs(decomposeTrs(m, t, q, s) - F(1)) < eps * F(8));
  test_assert(std::fabs(q.dot(q) - F(1)) < eps * F(8));
  m[2][1] = std::numeric_limits<F>::quiet_NaN();
  decomposeTrs(m, t, q, s);
  test_assert(isIdentity(q));
  Matrix<F, 4> tiny;
  tiny.identity();
  for (int i = 0; i < 3; i++) {
    tiny[i][i] = std::numeric_limits<F>::denorm_min() * F(4);
  }
  test_assert(decomposeTrs(tiny, t, q, s) == F(0) && isIdentity(q));
}

template <typename F, int N> void testInverse(const char *name)
{
  const F eps = std::numeric_limits<F>::epsilon();
  Rand rnd(0x5555 + N);
  test::FailureLog log(name);
  for (int64_t n = test::cases(20000); n > 0; n--) {
    log.count_case();
    Matrix<F, N> m, inv;
    for (int c = 0; c < N; c++) {
      for (int r = 0; r < N; r++) {
        m[c][r] = F(rnd.range(-1, 1));
      }
    }
    log.check(tryInverse(m, inv), [] { fprintf(stderr, "refused"); });
    Matrix<F, N> eigen = m;
    eigen.invert();
    const F *e = eigen;
    F size = F(0);
    for (int i = 0; i < N * N; i++) {
      size = (std::max)(size, std::fabs(e[i]));
    }
    log.check(diff(inv, eigen) < eps * F(64) * size * (F(1) + size),
              [&] { fprintf(stderr, "differs from Eigen's, size %g", double(size)); });

    /* Rows and columns scaled by anything: still invertible, and the answer is the
     * rescaled one. Powers of two keep it exact. */
    int re[N], ce[N];
    Matrix<F, N> scaled = m;
    for (int i = 0; i < N; i++) {
      re[i] = rnd.below(40) - 20;
      ce[i] = rnd.below(40) - 20;
    }
    for (int c = 0; c < N; c++) {
      for (int r = 0; r < N; r++) {
        scaled[c][r] = std::ldexp(m[c][r], re[r] + ce[c]);
      }
    }
    Matrix<F, N> sinv;
    if (log.check(tryInverse(scaled, sinv), [] { fprintf(stderr, "scaled: refused"); })) {
      for (int c = 0; c < N; c++) {
        for (int r = 0; r < N; r++) {
          sinv[c][r] = std::ldexp(sinv[c][r], ce[r] + re[c]);
        }
      }
      log.check(diff(sinv, inv) < eps * F(1024) * size * (F(1) + size),
                [] { fprintf(stderr, "scaled: wrong"); });
    }

    /* Singular: a column that is a combination of two others. */
    Matrix<F, N> sing = m, out = m;
    const F ka = F(rnd.range(-2, 2)), kb = F(rnd.range(-2, 2));
    for (int r = 0; r < N; r++) {
      sing[2][r] = sing[0][r] * ka + sing[1][r] * kb;
    }
    const bool singular = !tryInverse(sing, out);
    log.check(!singular || diff(out, m) == F(0), [] { fprintf(stderr, "out changed"); });
    /* Rounding leaves some of them invertible; those have huge inverses. */
    if (!singular) {
      F worst = F(0);
      const F *o = out;
      for (int i = 0; i < N * N; i++) {
        worst = (std::max)(worst, std::fabs(o[i]));
      }
      log.check(worst > F(0.001) / eps, [] { fprintf(stderr, "singular accepted"); });
    }
  }
  log.finish();

  Matrix<F, N> m, out;
  test_assert(!tryInverse(m, out)); /* zero */
  m.identity();
  m[1] = m[0];
  test_assert(!tryInverse(m, out));
  m.identity();
  m[0][1] = std::numeric_limits<F>::infinity();
  test_assert(!tryInverse(m, out));
  m.identity();
  m[0][0] = std::numeric_limits<F>::denorm_min();
  test_assert(!tryInverse(m, out)); /* the inverse overflows */
  m.identity();
  m[0][0] = F(1e-30);
  test_assert(tryInverse(m, m) && std::fabs(m[0][0] * F(1e-30) - F(1)) < F(1e-5));
}

template <typename F> void testProjections()
{
  const F eps = std::numeric_limits<F>::epsilon();
  const F n = F(0.5), f = F(200), inf = std::numeric_limits<F>::infinity();
  for (int depth = 0; depth < 2; depth++) {
    for (int rev = 0; rev < 2; rev++) {
      const F lo = depth ? F(-1) : F(0);
      const F atNear = rev ? F(1) : lo, atFar = rev ? lo : F(1);
      const auto ndc = [](const Matrix<F, 4> &m, F x, F y, F z) {
        const Vec<F, 4> c = transformHomogeneous(m, Vec<F, 4>(x, y, z, F(1)));
        return Vec<F, 3>(c[0] / c[3], c[1] / c[3], c[2] / c[3]);
      };
      const F fov = F(1.1), aspect = F(1.6), th = std::tan(fov / F(2));
      const Matrix<F, 4> p = perspective(fov, aspect, n, f, ClipDepth(depth), rev != 0);
      /* The corners of the near and far planes are the corners of clip space. */
      test_assert(diff(ndc(p, n * th * aspect, -n * th, -n), Vec<F, 3>(1, -1, atNear)) <
                  eps * F(16));
      test_assert(diff(ndc(p, -f * th * aspect, f * th, -f), Vec<F, 3>(-1, 1, atFar)) <
                  eps * F(1024));
      const Matrix<F, 4> i = perspective(fov, aspect, n, inf, ClipDepth(depth), rev != 0);
      test_assert(diff(ndc(i, F(0), F(0), -n), Vec<F, 3>(0, 0, atNear)) < eps * F(16));
      const Vec<F, 4> far4 = transformHomogeneous(i, Vec<F, 4>(0, 0, -1, 0));
      test_assert(std::fabs(far4[2] / far4[3] - atFar) < eps * F(16));
      const Matrix<F, 4> o =
          orthographic<F>(F(-3), F(5), F(1), F(2), F(-7), f, ClipDepth(depth), rev != 0);
      test_assert(diff(ndc(o, F(-3), F(1), F(7)), Vec<F, 3>(-1, -1, atNear)) <
                  eps * F(64));
      test_assert(diff(ndc(o, F(5), F(2), -f), Vec<F, 3>(1, 1, atFar)) < eps * F(64));
    }
  }
  Rand rnd(0x777);
  for (int k = 0; k < 1000; k++) {
    const Vec<F, 3> eye = randVec<F>(rnd, 10), target = randVec<F>(rnd, 10);
    const Vec<F, 3> up = k % 3 == 0   ? Vec<F, 3>()
                         : k % 3 == 1 ? (target - eye) * F(2)
                                      : randVec<F>(rnd);
    const Matrix<F, 4> v = lookAt(eye, target, up);
    const F dist = (target - eye).length();
    test_assert(diff(transformPoint(v, eye), Vec<F, 3>()) < eps * F(128));
    test_assert(diff(transformPoint(v, target), Vec<F, 3>(0, 0, -dist)) < eps * F(256));
    const Quat<F> q = Quat<F>::fromMatrix(v);
    test_assert(diff(q.toMatrix3(), [&] {
                  Ref3<F> r;
                  for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                      r.m[i][j] = static_cast<const F *>(v)[j * 4 + i];
                    }
                  }
                  return r;
                }()) < eps * F(64));
    if (k % 3 == 2) {
      const Vec<F, 3> u = transformVector(v, up);
      test_assert(std::fabs(u[0]) < eps * F(64) && u[1] > F(0));
    }
  }
  const Vec<F, 3> p(1, 2, 3);
  test_assert(diff(transformPoint(lookAt(p, p, Vec<F, 3>(0, 1, 0)), p), Vec<F, 3>()) <
              eps * F(64));
}

} // namespace

int main()
{
  testQuat<float>("quat float");
  testQuat<double>("quat double");
  testEuler<float>("euler float");
  testEuler<double>("euler double");
  testTransforms<float>("transforms float");
  testTransforms<double>("transforms double");
  testInverse<float, 3>("inverse float 3");
  testInverse<float, 4>("inverse float 4");
  testInverse<double, 3>("inverse double 3");
  testInverse<double, 4>("inverse double 4");
  testProjections<float>();
  testProjections<double>();
  return test_end();
}
