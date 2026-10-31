// Plan step 2: inspect Eigen 3.3.7's expression-specific evaluation order.
// Standalone, not part of B's build. Run in point-lio-noetic:pinned:
// g++ -std=c++14 -O3 -g -pthread -fexceptions -fopenmp -I/usr/include/eigen3
//     tests/math/eigen_order_probe.cpp -o /tmp/eigen_order_probe (one command)
// /tmp/eigen_order_probe
// noinline keeps each measured expression observable at this boundary. This
// does not establish the order after inlining into B; L2 in step 5 checks that.
#include <Eigen/Core>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

using Mat = Eigen::Matrix3d;
using Vec = Eigen::Vector3d;
using Mat30 = Eigen::Matrix<double, 30, 30>;

// Deliberately use the original expression shapes, not a pre-evaluated product.
__attribute__((noinline)) Mat transpose_product(const Mat &a, const Mat &b) { return a.transpose() * b; }
__attribute__((noinline)) Mat product(const Mat &a, const Mat &b) { return a * b; }
__attribute__((noinline)) Mat scaled_product(double s, const Mat &a, const Mat &b) { return s * a * b; }
__attribute__((noinline)) Mat negative_product(const Mat &a, const Mat &b) { return -a * b; }
__attribute__((noinline)) Vec matvec(const Mat &a, const Vec &v) { return a * v; }
__attribute__((noinline)) double dot(const Vec &a, const Vec &b) { return a.dot(b); }
__attribute__((noinline)) double trace(const Mat &a) { return a.trace(); }
__attribute__((noinline)) double norm(const Vec &a) { return a.norm(); }
__attribute__((noinline)) Vec normalized_col(const Mat &s, int i) { return s.col(i).normalized(); }
__attribute__((noinline)) Mat exp_sum(double s1, double s2, const Mat &k)
{
    return Mat::Identity() + s1 * k + s2 * k * k;
}
__attribute__((noinline)) Mat symmetric_part(const Mat &r, double c)
{
    return 0.5 * (r + r.transpose()) - c * Mat::Identity();
}
__attribute__((noinline)) Mat30 covariance_sum(Mat30 f, const Mat30 &j, double dt)
{
    f += j * dt;
    return f;
}
__attribute__((noinline)) int maximum_diagonal(const Mat &s)
{
    int i;
    s.diagonal().maxCoeff(&i);
    return i;
}

bool same(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }

// Each named candidate is compared by raw bits, including signed zero.
struct Count {
    std::string name;
    uint64_t checked = 0, different = 0;
    void add(double actual, double candidate)
    {
        ++checked;
        different += !same(actual, candidate);
    }
    bool report(bool expect_match) const
    {
        std::cout << name << " checked=" << checked << " different=" << different << '\n';
        return checked > 0 && (expect_match ? different == 0 : different > 0);
    }
};

double left(double x, double y, double z) { return (x + y) + z; }
double right(double x, double y, double z) { return x + (y + z); }
double mixed(int r, double x, double y, double z) { return r == 2 ? right(x, y, z) : left(x, y, z); }

int main()
{
    constexpr int samples = 200000;
    std::mt19937_64 rng(20261031);
    // Avoid std::uniform_real_distribution's implementation-dependent mapping.
    auto random = [&]() {
        const double magnitude = static_cast<double>(rng() >> 11) / 9007199254740992.0;
        return (rng() & 1) ? -magnitude : magnitude;
    };
    Count atb{"AtB.left"}, atb_other{"AtB.right"};
    Count ab{"AB.mixed"}, ab_other{"AB.all_left"};
    Count sab{"sAB.scale_left_then_mixed"}, sab_other{"sAB.scale_after_product"};
    Count negab{"negative_AB.negate_left_then_mixed"};
    Count av{"Av.mixed"}, av_other{"Av.all_left"};
    Count dp{"dot.left"}, dp_other{"dot.right"};
    Count tr{"trace.right"}, tr_other{"trace.left"};
    Count nr{"norm.left"}, nr_other{"norm.right"};
    Count nc{"normalized_col.divide_sqrt_left"}, nc_other{"normalized_col.divide_sqrt_right"};
    Count nc_recip{"normalized_col.multiply_reciprocal"};
    Count es{"exp_sum.left"}, es_other{"exp_sum.right"};
    Count ss{"symmetric_part.subtract_cI_all_coefficients"};
    Count fs{"F_plus_equals_Jdt"}, mc{"maxCoeff.first_tie"};

    for (int n = 0; n < samples; ++n) {
        Mat a, b;
        Vec v, w;
        for (int r = 0; r < 3; ++r) {
            v[r] = random();
            w[r] = random();
            for (int c = 0; c < 3; ++c) {
                a(r, c) = random();
                b(r, c) = random();
            }
        }
        const double s1 = random(), s2 = random();
        const Mat t = transpose_product(a, b), p = product(a, b);
        const Mat sp = scaled_product(s1, a, b), np = negative_product(a, b);
        const Vec y = matvec(a, v);
        const Mat sum = exp_sum(s1, s2, a), sym = symmetric_part(a, s1);
        const int col = n % 3;
        const Vec column = a.col(col), normalized = normalized_col(a, col);
        const double square_l = left(column[0] * column[0], column[1] * column[1], column[2] * column[2]);
        const double square_r = right(column[0] * column[0], column[1] * column[1], column[2] * column[2]);
        for (int r = 0; r < 3; ++r) {
            nc.add(normalized[r], column[r] / std::sqrt(square_l));
            nc_other.add(normalized[r], column[r] / std::sqrt(square_r));
            nc_recip.add(normalized[r], column[r] * (1.0 / std::sqrt(square_l)));
            av.add(y[r], mixed(r, a(r, 0) * v[0], a(r, 1) * v[1], a(r, 2) * v[2]));
            av_other.add(y[r], left(a(r, 0) * v[0], a(r, 1) * v[1], a(r, 2) * v[2]));
            for (int c = 0; c < 3; ++c) {
                const double x0 = a(r, 0) * b(0, c), x1 = a(r, 1) * b(1, c), x2 = a(r, 2) * b(2, c);
                atb.add(t(r, c), left(a(0, r) * b(0, c), a(1, r) * b(1, c), a(2, r) * b(2, c)));
                atb_other.add(t(r, c), right(a(0, r) * b(0, c), a(1, r) * b(1, c), a(2, r) * b(2, c)));
                ab.add(p(r, c), mixed(r, x0, x1, x2));
                ab_other.add(p(r, c), left(x0, x1, x2));
                sab.add(sp(r, c), mixed(r, (s1 * a(r, 0)) * b(0, c), (s1 * a(r, 1)) * b(1, c), (s1 * a(r, 2)) * b(2, c)));
                sab_other.add(sp(r, c), s1 * mixed(r, x0, x1, x2));
                negab.add(np(r, c), mixed(r, (-a(r, 0)) * b(0, c), (-a(r, 1)) * b(1, c), (-a(r, 2)) * b(2, c)));
                const double x = mixed(r, (s2 * a(r, 0)) * a(0, c), (s2 * a(r, 1)) * a(1, c), (s2 * a(r, 2)) * a(2, c));
                const double identity = r == c ? 1.0 : 0.0;
                es.add(sum(r, c), (identity + s1 * a(r, c)) + x);
                es_other.add(sum(r, c), identity + (s1 * a(r, c) + x));
                ss.add(sym(r, c), 0.5 * (a(r, c) + a(c, r)) - s1 * identity);
            }
        }
        dp.add(dot(v, w), left(v[0] * w[0], v[1] * w[1], v[2] * w[2]));
        dp_other.add(dot(v, w), right(v[0] * w[0], v[1] * w[1], v[2] * w[2]));
        tr.add(trace(a), right(a(0, 0), a(1, 1), a(2, 2)));
        tr_other.add(trace(a), left(a(0, 0), a(1, 1), a(2, 2)));
        nr.add(norm(v), std::sqrt(left(v[0] * v[0], v[1] * v[1], v[2] * v[2])));
        nr_other.add(norm(v), std::sqrt(right(v[0] * v[0], v[1] * v[1], v[2] * v[2])));
        int expected_max = 0;
        for (int i = 1; i < 3; ++i) if (a(i, i) > a(expected_max, expected_max)) expected_max = i;
        mc.add(maximum_diagonal(a), expected_max);

        // Same 30x30 shape as predict's coefficient-wise addition (2000 cases).
        if (n < 2000) {
            Mat30 f, j;
            for (int r = 0; r < 30; ++r)
                for (int c = 0; c < 30; ++c) { f(r, c) = random(); j(r, c) = random(); }
            const Mat30 actual = covariance_sum(f, j, s1);
            for (int r = 0; r < 30; ++r)
                for (int c = 0; c < 30; ++c) fs.add(actual(r, c), f(r, c) + j(r, c) * s1);
        }
    }

    // Explicit signed-zero, negative scalar and tied maximum cases.
    Count zero{"normalized_col.zero"}, signed_zero{"symmetric_part.signed_zero"};
    const Vec normalized_zero = normalized_col(Mat::Zero(), 0);
    for (int i = 0; i < 3; ++i) zero.add(normalized_zero[i], 0.0);
    Mat r = Mat::Identity();
    r(1, 1) = r(2, 2) = -1.0;
    r(0, 1) = r(1, 0) = -0.0;
    const Mat s = symmetric_part(r, -1.0);
    signed_zero.add(s(0, 1), 0.0);
    signed_zero.add(s(1, 0), 0.0);
    int ties = 0;
    for (const Vec &d : std::array<Vec, 7>{{Vec(1, 1, 0), Vec(0, 1, 1), Vec(1, 0, 1), Vec(1, 1, 1),
                                          Vec(-2, -2, -3), Vec(-3, -2, -2), Vec(-0.0, 0.0, -1)}}) {
        Mat a = d.asDiagonal();
        int expected = 0;
        for (int i = 1; i < 3; ++i) if (d[i] > d[expected]) expected = i;
        mc.add(maximum_diagonal(a), expected);
        ++ties;
    }

    std::cout << "Eigen " << EIGEN_WORLD_VERSION << '.' << EIGEN_MAJOR_VERSION << '.' << EIGEN_MINOR_VERSION
              << " seed=20261031 samples=" << samples << " covariance_samples=2000 tie_cases=" << ties << '\n';
    bool ok = true;
    for (const Count *c : {&atb, &ab, &sab, &negab, &av, &dp, &tr, &nr, &nc, &es, &ss, &fs, &mc, &zero, &signed_zero})
        ok = c->report(true) && ok;
    // Alternative models must differ somewhere: otherwise this input set cannot
    // distinguish the proposed order from that alternative.
    for (const Count *c : {&atb_other, &ab_other, &sab_other, &av_other, &dp_other, &tr_other, &nr_other, &nc_other, &nc_recip, &es_other})
        ok = c->report(false) && ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": measured models and discriminating alternatives\n";
    return ok ? 0 : 1;
}
