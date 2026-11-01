// 数学と状態の表現の L1 テスト(計画書 §2、docs/state-definition.md)。
// MTK の結果を、MTK を使わない解析解(Eigen::AngleAxisd、ロドリゲスの別の形)・性質・数値微分と比べる。
// 許容誤差は、倍精度の丸めと中心差分の打ち切り誤差から決めた値で、結果を見て広げない(agents/rules/testing.md)。
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>

#include "Estimator.h"
#include "point_order.h"

namespace {

using SO3d = MTK::SO3<double>;
typedef esekfom::esekf<state_output, 30, input_ikfom> KfOutput;

Eigen::Matrix3d skew(const Eigen::Vector3d &v)
{
    Eigen::Matrix3d m;
    m << 0, -v(2), v(1), v(2), 0, -v(0), -v(1), v(0), 0;
    return m;
}

// MTK とは別の形のロドリゲスの公式: cos·I + sin·[a]× + (1 − cos)·a·aᵀ
Eigen::Matrix3d rodrigues(const Eigen::Vector3d &v)
{
    double t = v.norm();
    if (t == 0) return Eigen::Matrix3d::Identity();
    Eigen::Vector3d a = v / t;
    return std::cos(t) * Eigen::Matrix3d::Identity() + std::sin(t) * skew(a) + (1 - std::cos(t)) * a * a.transpose();
}

// MTK とは別の対数写像(Eigen の AngleAxis)
Eigen::Vector3d log_ref(const Eigen::Matrix3d &r)
{
    Eigen::AngleAxisd aa(r);
    return aa.angle() * aa.axis();
}

Eigen::Vector3d random_vec(std::mt19937_64 &g, double lo, double hi)
{
    std::normal_distribution<double> n(0, 1);
    std::uniform_real_distribution<double> u(lo, hi);
    Eigen::Vector3d a(n(g), n(g), n(g));
    return a.normalized() * u(g);
}

double max_abs(const Eigen::MatrixXd &m) { return m.cwiseAbs().maxCoeff(); }

state_output random_state(std::mt19937_64 &g)
{
    state_output s;
    s.pos = random_vec(g, 0, 10);
    s.rot = rodrigues(random_vec(g, 0, 3));
    s.offset_R_L_I = rodrigues(random_vec(g, 0, 0.5));
    s.offset_T_L_I = random_vec(g, 0, 0.5);
    s.vel = random_vec(g, 0, 3);
    s.omg = random_vec(g, 0.1, 3);
    s.acc = random_vec(g, 0.1, 15);
    s.gravity = random_vec(g, 9.7, 9.9);
    s.bg = random_vec(g, 0, 0.05);
    s.ba = random_vec(g, 0, 0.3);
    return s;
}

Eigen::Matrix<double, 30, 1> unit(int i)
{
    Eigen::Matrix<double, 30, 1> e = Eigen::Matrix<double, 30, 1>::Zero();
    e(i) = 1;
    return e;
}

state_output boxplus(const state_output &x, const Eigen::Matrix<double, 30, 1> &d)
{
    state_output y = x;
    y.boxplus(MTK::vectview<const double, 30>(d.data()));
    return y;
}

Eigen::Matrix<double, 30, 1> boxminus(const state_output &a, const state_output &b)
{
    Eigen::Matrix<double, 30, 1> r;
    a.boxminus(MTK::vectview<double, 30>(r.data()), b);
    return r;
}

// 予測の状態の伝播(esekf::predict の predict_state の経路): x ⊕ f(x)·dt
state_output propagate(const state_output &x, double dt)
{
    state_output y = x;
    state_output tmp = x;
    Eigen::Matrix<double, 30, 1> f = get_f_output(tmp, input_ikfom());
    y.oplus(MTK::vectview<const double, 30>(f.data()), dt);
    return y;
}

// --- SO3 の指数写像 ---

TEST(So3Exp, MatchesAxisAngle)
{
    std::mt19937_64 g(1);
    int n = 0;
    for (double t : {2e-7, 1e-3, 0.5, 1.5, 3.0, 3.14})
        for (int i = 0; i < 20; i++, n++) {
            Eigen::Vector3d v = random_vec(g, t, t);
            EXPECT_LE(max_abs(SO3d::exp(v) - rodrigues(v)), 1e-15 * 8) << "angle " << t;
        }
    EXPECT_EQ(n, 120);
}

TEST(So3Exp, BelowThresholdReturnsIdentityWithinAngle)
{
    // ‖θ‖ ≤ 1e-7 では MTK はちょうど I を返す(state-definition.md 5.1)。真値との差は ‖θ‖ 程度になる
    std::mt19937_64 g(2);
    for (int i = 0; i < 20; i++) {
        Eigen::Vector3d v = random_vec(g, 1e-9, 1e-7);
        Eigen::Matrix3d r = SO3d::exp(v);
        EXPECT_EQ(r, Eigen::Matrix3d::Identity());
        EXPECT_LE(max_abs(r - rodrigues(v)), v.norm() * 1.0001);
    }
}

TEST(So3Exp, IsRotation)
{
    std::mt19937_64 g(3);
    for (int i = 0; i < 200; i++) {
        Eigen::Matrix3d r = SO3d::exp(random_vec(g, 1e-6, M_PI));
        EXPECT_LE(max_abs(r.transpose() * r - Eigen::Matrix3d::Identity()), 1e-15 * 8);
        EXPECT_NEAR(r.determinant(), 1.0, 1e-15 * 8);
    }
}

// --- SO3 の対数写像 ---

TEST(So3Log, InvertsExp)
{
    std::mt19937_64 g(4);
    for (int i = 0; i < 200; i++) {
        Eigen::Vector3d v = random_vec(g, 1e-3, M_PI - 1e-3);
        Eigen::Vector3d w = SO3d::log(SO3d(rodrigues(v)));
        EXPECT_LE((w - v).norm(), 1e-12) << "angle " << v.norm();
    }
}

// BL-011: rounded input R amplified trace error in the acos-based normal path.
// Fixed bits from docs/math-log-l1-reproduction.md; retain the original 1e-12.
TEST(So3Log, InvertsRoundedRotationBL011)
{
    auto from_bits = [](uint64_t bits) {
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };
    Eigen::Vector3d v(from_bits(0xc0052889165ec0c2ULL), from_bits(0x3fe97bf35e541585ULL),
                      from_bits(0xbff75a8559c5151bULL));
    const uint64_t bits[] = {
        0x3fdbbe5539650384ULL, 0xbfdb18dce9eb1e75ULL, 0x3fe974c0a7d7e313ULL,
        0xbfdc25aeb5f0ee9fULL, 0xbfebd61caea14362ULL, 0xbfcc95d7b3ad77e5ULL,
        0x3fe92b6a9e24735fULL, 0xbfd0320598cac0f6ULL, 0xbfe206874f7eb430ULL};
    Eigen::Matrix3d r;
    int count = 0;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col, ++count) r(row, col) = from_bits(bits[3 * row + col]);
    ASSERT_EQ(count, 9);
    const Eigen::Vector3d w = SO3d::log(SO3d(r));
    ASSERT_TRUE(w.allFinite());
    EXPECT_LE((w - v).norm(), 1e-12);
}

TEST(So3Log, SmallAngleBranchErrorBound)
{
    // |θ| < 0.001 では MTK は 0.5·k(= sin θ·軸)を返すので、真値との差は θ − sin θ ≈ θ³/6 まである
    std::mt19937_64 g(5);
    for (int i = 0; i < 100; i++) {
        Eigen::Vector3d v = random_vec(g, 1e-6, 1e-3 * 0.999);
        Eigen::Vector3d w = SO3d::log(SO3d(rodrigues(v)));
        double t = v.norm();
        EXPECT_LE((w - v).norm(), t * t * t / 6 * 1.01 + 1e-18) << "angle " << t;
    }
}

// 台帳 BL-008: A-v1 の MTK の Log は、π − 1e-7 の付近で誤った値を返し、π − 1e-10 で NaN になる。B で直した
TEST(So3Log, FiniteNearPi)
{
    std::mt19937_64 g(6);
    for (double d : {1e-4, 1e-7, 1e-10, 0.0})
        for (int i = 0; i < 20; i++) {
            Eigen::Vector3d v = random_vec(g, M_PI - d, M_PI - d);
            Eigen::Vector3d w = SO3d::log(SO3d(rodrigues(v)));
            ASSERT_TRUE(w.allFinite()) << "pi - " << d;
            EXPECT_LE(max_abs(rodrigues(w) - rodrigues(v)), 1e-6) << "pi - " << d;
        }
}

// BL-008 の修正の分岐(回転角 π − d、d < 3.16e-4)の前後で、Log が真の回転ベクトルに戻る。
// 分岐の中: atan2 と対称な部分から求めた軸は丸め程度の誤差なので、上限 1e-12。
// 分岐の外(A と同じ式): acos の誤差 ε/d が θ/sin θ の sin θ ≈ d で割られ、相対誤差 ε/d² になる。上限は 10·ε/d²
TEST(So3Log, AccurateAroundNearPiBranch)
{
    const double eps = std::numeric_limits<double>::epsilon();
    std::mt19937_64 g(14);
    for (double d : {1e-3, 4e-4, 3.2e-4, 3.1e-4, 2e-4, 1e-5, 1e-8, 1e-12})
        for (int i = 0; i < 20; i++) {
            Eigen::Vector3d v = random_vec(g, M_PI - d, M_PI - d);
            Eigen::Vector3d w = SO3d::log(SO3d(rodrigues(v)));
            double bound = d < 3.16e-4 ? 1e-12 : 10 * eps / (d * d);
            EXPECT_LE((w - v).norm(), bound) << "pi - " << d;
        }
}

// --- boxplus / boxminus ---

TEST(So3Boxplus, IsRightPerturbation)
{
    std::mt19937_64 g(7);
    for (int i = 0; i < 50; i++) {
        Eigen::Matrix3d r0 = rodrigues(random_vec(g, 0, 3));
        Eigen::Vector3d d = random_vec(g, 1e-3, 1.0);
        SO3d r(r0);
        r.boxplus(MTK::vectview<const double, 3>(d.data()));
        EXPECT_LE(max_abs(r - r0 * rodrigues(d)), 1e-15 * 8);
        EXPECT_GT(max_abs(r - rodrigues(d) * r0), 1e-6);  // 左から掛けたものとは異なる
    }
}

TEST(So3Boxminus, InvertsBoxplus)
{
    std::mt19937_64 g(8);
    for (int i = 0; i < 100; i++) {
        SO3d r0(rodrigues(random_vec(g, 0, 3)));
        Eigen::Vector3d d = random_vec(g, 1e-3, 3.0);
        SO3d r = r0;
        r.boxplus(MTK::vectview<const double, 3>(d.data()));
        Eigen::Vector3d e;
        r.boxminus(MTK::vectview<double, 3>(e.data()), r0);
        EXPECT_LE((e - d).norm(), 1e-12);
    }
}

TEST(StateBoxminus, InvertsBoxplusForAllComponents)
{
    std::mt19937_64 g(9);
    std::normal_distribution<double> n(0, 0.3);
    for (int i = 0; i < 50; i++) {
        state_output x = random_state(g);
        Eigen::Matrix<double, 30, 1> d;
        for (int j = 0; j < 30; j++) d(j) = n(g);
        EXPECT_LE((boxminus(boxplus(x, d), x) - d).norm(), 1e-12);
    }
}

// --- A 行列(共分散の伝播) ---

TEST(AMatrix, IsLeftJacobianOfExp)
{
    // Exp(v + ε) ≈ Exp(A(v)·ε)·Exp(v)
    std::mt19937_64 g(10);
    const double h = 1e-6;
    for (int i = 0; i < 50; i++) {
        Eigen::Vector3d v = random_vec(g, 1e-3, 2.5);
        Eigen::Matrix3d num;
        for (int j = 0; j < 3; j++) {
            Eigen::Vector3d e = Eigen::Vector3d::Unit(j) * h;
            num.col(j) = (log_ref(rodrigues(v + e) * rodrigues(v).transpose()) -
                          log_ref(rodrigues(v - e) * rodrigues(v).transpose())) / (2 * h);
        }
        vect3 vv(v);
        EXPECT_LE(max_abs(MTK::A_matrix(vv) - num), 1e-8) << "angle " << v.norm();
    }
}

// --- 過程モデル ---

TEST(ProcessModel, JacobianMatchesNumeric)
{
    std::mt19937_64 g(11);
    const double h = 1e-6;
    for (int i = 0; i < 20; i++) {
        state_output x = random_state(g);
        Eigen::Matrix<double, 30, 30> num;
        for (int j = 0; j < 30; j++) {
            state_output xp = boxplus(x, unit(j) * h), xm = boxplus(x, unit(j) * -h);
            num.col(j) = (get_f_output(xp, input_ikfom()) - get_f_output(xm, input_ikfom())) / (2 * h);
        }
        EXPECT_LE(max_abs(df_dx_output(x, input_ikfom()) - num), 1e-7);
    }
}

TEST(Predict, CovarianceMatchesNumericJacobian)
{
    // P' = F·P·Fᵀ + Q·dt²。F は状態の伝播 x ⊕ f(x)·dt の、誤差状態についての数値微分
    std::mt19937_64 g(12);
    std::normal_distribution<double> n(0, 1);
    const double h = 1e-6;
    for (double dt : {0.005, 0.1})
        for (int i = 0; i < 10; i++) {
            state_output x = random_state(g);
            Eigen::Matrix<double, 30, 30> a, q = Eigen::Matrix<double, 30, 30>::Zero();
            for (int r = 0; r < 30; r++)
                for (int c = 0; c < 30; c++) a(r, c) = n(g);
            Eigen::Matrix<double, 30, 30> p = a * a.transpose() / 30 + Eigen::Matrix<double, 30, 30>::Identity() * 1e-3;
            for (int j = 0; j < 30; j++) q(j, j) = std::abs(n(g));

            Eigen::Matrix<double, 30, 30> f;
            state_output y = propagate(x, dt);
            for (int j = 0; j < 30; j++)
                f.col(j) = (boxminus(propagate(boxplus(x, unit(j) * h), dt), y) -
                            boxminus(propagate(boxplus(x, unit(j) * -h), dt), y)) / (2 * h);
            Eigen::Matrix<double, 30, 30> expected = f * p * f.transpose() + q * dt * dt;

            KfOutput kf;
            kf.init_dyn_share_modified_3h(get_f_output, df_dx_output, h_model_output, h_model_IMU_output);
            kf.change_x(x);
            kf.change_P(p);
            input_ikfom in;
            double d = dt;
            kf.predict(d, q, in, false, true);
            EXPECT_LE(max_abs(kf.get_P() - expected), 1e-7 * max_abs(expected)) << "dt " << dt;
            EXPECT_EQ(max_abs(boxminus(kf.get_x(), x)), 0.0);  // 共分散だけの呼び出しは状態を変えない
        }
}

TEST(Predict, CovarianceStaysSymmetricPositive)
{
    std::mt19937_64 g(13);
    std::normal_distribution<double> n(0, 1);
    for (int i = 0; i < 20; i++) {
        Eigen::Matrix<double, 30, 30> a;
        for (int r = 0; r < 30; r++)
            for (int c = 0; c < 30; c++) a(r, c) = n(g);
        KfOutput kf;
        kf.init_dyn_share_modified_3h(get_f_output, df_dx_output, h_model_output, h_model_IMU_output);
        state_output x = random_state(g);
        kf.change_x(x);
        Eigen::Matrix<double, 30, 30> p = a * a.transpose() / 30;
        kf.change_P(p);
        Eigen::Matrix<double, 30, 30> q = Eigen::Matrix<double, 30, 30>::Identity() * 1e-4;
        input_ikfom in;
        double dt = 0.01;
        kf.predict(dt, q, in, false, true);
        const Eigen::Matrix<double, 30, 30> &pp = kf.get_P();
        double m = max_abs(pp);
        EXPECT_LE(max_abs(pp - pp.transpose()), 1e-13 * m);
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 30, 30>> es((pp + pp.transpose()) / 2);
        EXPECT_GE(es.eigenvalues().minCoeff(), -1e-12 * m);
    }
}

// --- 初期姿勢(ImuProcess::Set_init) ---

// 台帳 BL-009: 測った重力が設定の重力とちょうど逆向きのとき、A は回転でない -I(行列式 −1)を返す
TEST(SetInit, AntiParallelGravityGivesRotation)
{
    for (const Eigen::Vector3d &g : {Eigen::Vector3d(0, 0, -9.81), Eigen::Vector3d(0, 0, 9.81),
                                     Eigen::Vector3d(Eigen::Vector3d(1, -2, 3).normalized() * 9.81)}) {
        ImuProcess p;
        p.gravity_ = g;
        Eigen::Vector3d t = -g;
        Eigen::Matrix3d r;
        p.Set_init(t, r);
        EXPECT_LE(max_abs(r.transpose() * r - Eigen::Matrix3d::Identity()), 1e-15) << g.transpose();
        EXPECT_NEAR(r.determinant(), 1.0, 1e-15) << g.transpose();
        EXPECT_LE((r * g - t).norm(), 1e-14 * g.norm()) << g.transpose();
    }
}

// --- iVox の近傍探索 ---

// 台帳 BL-010: 候補が0件のとき、A は出力の入れ物を空にせず、前の中身が残る
TEST(IVoxClosest, NoCandidateClearsOutput)
{
    IVoxType::Options opt;
    IVoxType ivox(opt);
    PointType far;
    far.x = 100;
    far.y = 0;
    far.z = 0;
    ivox.AddPoints(PointVector(1, far));

    PointType q;
    q.x = 0;
    q.y = 0;
    q.z = 0;
    PointVector out(5, far);
    EXPECT_FALSE(ivox.GetClosestPoint(q, out, 5));
    EXPECT_TRUE(out.empty()) << out.size();

    // 候補があるときは、見つけた点で埋める
    out.assign(5, q);
    EXPECT_TRUE(ivox.GetClosestPoint(far, out, 5));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].x, far.x);
}

// 格子の上の点(座標が 0.05 の倍数)。距離の等しい点と重なる点が多い。intensity に挿入の順の添字を入れる
PointVector lattice_points(int n, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> c(-8, 8);
    PointVector pts;
    for (int i = 0; i < n; ++i)
    {
        PointType p;
        p.x = 0.05f * c(rng);
        p.y = 0.05f * c(rng);
        p.z = 0.05f * c(rng);
        p.intensity = static_cast<float>(i);
        pts.push_back(p);
    }
    return pts;
}

// 期待値: 距離が範囲内の全点を (距離, 挿入の順) で並べたもの。距離は iVox と同じ distance2
std::vector<std::pair<double, int>> brute_force_neighbours(const PointVector &pts, const PointType &q, double max_range)
{
    std::vector<std::pair<double, int>> all;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        double d = faster_lio::distance2(pts[i], q);
        if (d < max_range * max_range)
            all.emplace_back(d, static_cast<int>(i));
    }
    std::sort(all.begin(), all.end());
    return all;
}

// 範囲を格子の幅と同じにすると、範囲内の点はすべて 26 近傍のセルに入るので、総当たりと比べられる
IVoxType::Options lattice_options()
{
    IVoxType::Options opt;
    opt.resolution_ = 0.2;
    opt.nearby_type_ = IVoxType::NearbyType::NEARBY26;
    return opt;
}

// 台帳 BL-012: A は nth_element で選ぶので、2番目以降の並びと、距離の等しい候補のどれを選ぶかが実装で決まる
TEST(IVoxClosest, NearestByDistanceThenInsertionOrder)
{
    const double max_range = 0.2;
    int checked = 0, with_ties = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        PointVector pts = lattice_points(400, seed);
        IVoxType ivox(lattice_options());
        ivox.AddPoints(pts);
        PointVector queries = lattice_points(50, seed + 1000);
        for (const PointType &q : queries)
        {
            auto expected = brute_force_neighbours(pts, q, max_range);
            PointVector out;
            bool found = ivox.GetClosestPoint(q, out, 5, max_range);
            ASSERT_EQ(found, !expected.empty()) << seed;
            ASSERT_EQ(out.size(), std::min<size_t>(5, expected.size())) << seed;
            for (size_t j = 0; j < out.size(); ++j)
                EXPECT_EQ(out[j].intensity, static_cast<float>(expected[j].second)) << seed << " " << j;
            if (expected.size() > 5 && expected[4].first == expected[5].first)
                ++with_ties;
            ++checked;
        }
    }
    EXPECT_EQ(checked, 20 * 50);
    // 境目に距離の等しい候補がある問い合わせを十分に含む(入力が試したい場合を含むことの確認)
    EXPECT_GT(with_ties, 100) << with_ties;
}

// 次の候補は、ノードごとの切り詰めの前の全体での 6 番目。1つのノードが近い 6 点をすべて持つ場合を含む
TEST(IVoxClosest, NextCandidateIsSixthOverall)
{
    // 1つのセルに 7 点、隣のセルに遠い 1 点
    IVoxType ivox(lattice_options());
    PointVector pts;
    for (int i = 0; i < 7; ++i)
    {
        PointType p;
        p.x = 0.01f * (i + 1);
        p.y = 0.01f;
        p.z = 0.01f;
        p.intensity = static_cast<float>(i);
        pts.push_back(p);
    }
    PointType far;
    far.x = 0.21f;
    far.y = 0.01f;
    far.z = 0.01f;
    far.intensity = 7;
    pts.push_back(far);
    ivox.AddPoints(pts);

    PointType q;
    q.x = 0;
    q.y = 0.01f;
    q.z = 0.01f;
    PointVector out;
    IVoxType::DistPoint next;
    ASSERT_TRUE(ivox.GetClosestPoint(q, out, 5, 0.3, &next));
    ASSERT_EQ(out.size(), 5u);
    ASSERT_NE(next.node, nullptr);
    EXPECT_EQ(next.pid, 5u);
    EXPECT_EQ(next.dist, faster_lio::distance2(pts[5], q));
    EXPECT_EQ(next.Get().intensity, 5);

    // 範囲内の候補が 5 点以下なら次の候補はない
    ASSERT_TRUE(ivox.GetClosestPoint(q, out, 5, 0.055, &next));
    EXPECT_EQ(out.size(), 5u);
    EXPECT_EQ(next.node, nullptr);

    // 総当たりとの比較: 次の候補の距離と pid(挿入の順と同じ)
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        PointVector lp = lattice_points(400, seed);
        IVoxType lv(lattice_options());
        lv.AddPoints(lp);
        for (const PointType &lq : lattice_points(50, seed + 1000))
        {
            auto expected = brute_force_neighbours(lp, lq, 0.2);
            PointVector lo;
            lv.GetClosestPoint(lq, lo, 5, 0.2, &next);
            if (expected.size() <= 5)
            {
                EXPECT_EQ(next.node, nullptr) << seed;
                continue;
            }
            ASSERT_NE(next.node, nullptr) << seed;
            EXPECT_EQ(next.dist, expected[5].first) << seed;
            EXPECT_EQ(next.pid, static_cast<uint32_t>(expected[5].second)) << seed;
            ++checked;
        }
    }
    EXPECT_GT(checked, 500) << checked;
}


// --- 点の並び ---

// 時刻の等しい点が多い点群。x に元の添字を入れる
PointCloudXYZI cloud_with_time_ties(int n, int groups, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> group(0, groups - 1);
    PointCloudXYZI cloud;
    for (int i = 0; i < n; ++i)
    {
        PointType p;
        p.x = static_cast<float>(i);
        p.y = 0;
        p.z = 0;
        p.curvature = static_cast<float>(group(rng));
        cloud.push_back(p);
    }
    return cloud;
}

// 台帳 BL-007: A の非安定なソートでは、時刻の等しい点の並びがソートの実装で決まる
TEST(PointOrder, TimeSortKeepsInputOrderOfTies)
{
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        PointCloudXYZI cloud = cloud_with_time_ties(200, 5, seed);
        sort_by_time(cloud);
        ASSERT_EQ(cloud.size(), 200u);
        for (size_t i = 1; i < cloud.size(); ++i)
        {
            const PointType &a = cloud.points[i - 1], &b = cloud.points[i];
            ASSERT_LE(a.curvature, b.curvature) << seed << " " << i;
            if (a.curvature == b.curvature)
            {
                EXPECT_LT(a.x, b.x) << seed << " " << i;
                ++checked;
            }
        }
    }
    EXPECT_EQ(checked, 20 * (200 - 5));
}

// ボクセル内の点の値の大きさを散らし、足す順で float の和が変わるようにする
PointCloudXYZI cloud_in_few_voxels(int n, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> voxel(0, 3);
    std::uniform_real_distribution<float> frac(0.0f, 0.999f), mag(-3.0f, 3.0f);
    PointCloudXYZI cloud;
    for (int i = 0; i < n; ++i)
    {
        PointType p;
        p.x = static_cast<float>(voxel(rng)) + frac(rng);
        p.y = frac(rng);
        p.z = frac(rng);
        p.intensity = std::pow(10.0f, mag(rng));
        p.curvature = std::pow(10.0f, mag(rng));
        p.normal_x = std::pow(10.0f, mag(rng));
        p.normal_y = 0;
        p.normal_z = 0;
        cloud.push_back(p);
    }
    return cloud;
}

void expect_same_bits(const PointType &a, const PointType &b, int voxel)
{
    const float fa[] = {a.x, a.y, a.z, a.intensity, a.curvature, a.normal_x, a.normal_y, a.normal_z};
    const float fb[] = {b.x, b.y, b.z, b.intensity, b.curvature, b.normal_x, b.normal_y, b.normal_z};
    EXPECT_EQ(std::memcmp(fa, fb, sizeof fa), 0) << "voxel " << voxel;
}

// 台帳 BL-007: A はボクセル内の点を非安定なソートで並べ、その順に float で足す
TEST(PointOrder, VoxelCentroidSumsInInputOrder)
{
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        PointCloudXYZI::Ptr cloud(new PointCloudXYZI(cloud_in_few_voxels(400, seed)));
        StableVoxelGrid<PointType> filter;
        filter.setLeafSize(1.0f, 1.0f, 1.0f);
        filter.setInputCloud(cloud);
        PointCloudXYZI out;
        filter.filter(out);

        // 期待値: 入力の順に同じ累積器で足す。出力はボクセルの添字の昇順(x の整数部の昇順)
        std::vector<pcl::CentroidPoint<PointType>> sums(4);
        for (const PointType &p : cloud->points)
            sums[static_cast<int>(std::floor(p.x))].add(p);
        ASSERT_EQ(out.size(), 4u) << seed;
        for (int v = 0; v < 4; ++v)
        {
            PointType expected;
            sums[v].get(expected);
            expect_same_bits(out.points[v], expected, v);
            ++checked;
        }
    }
    EXPECT_EQ(checked, 20 * 4);
}

// 並びの修正のほかは pcl::VoxelGrid と同じ: ボクセルの数と順、重心(足す順の違いの丸めだけ)
TEST(PointOrder, VoxelGridMatchesPclExceptOrder)
{
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        PointCloudXYZI::Ptr cloud(new PointCloudXYZI(cloud_in_few_voxels(400, seed)));
        PointCloudXYZI a, b;
        pcl::VoxelGrid<PointType> pcl_filter;
        pcl_filter.setLeafSize(1.0f, 1.0f, 1.0f);
        pcl_filter.setInputCloud(cloud);
        pcl_filter.filter(a);
        StableVoxelGrid<PointType> filter;
        filter.setLeafSize(1.0f, 1.0f, 1.0f);
        filter.setInputCloud(cloud);
        filter.filter(b);
        ASSERT_EQ(a.size(), b.size()) << seed;
        for (size_t v = 0; v < a.size(); ++v)
        {
            const float fa[] = {a[v].x, a[v].y, a[v].z, a[v].intensity, a[v].curvature, a[v].normal_x};
            const float fb[] = {b[v].x, b[v].y, b[v].z, b[v].intensity, b[v].curvature, b[v].normal_x};
            for (int k = 0; k < 6; ++k)
                EXPECT_NEAR(fa[k], fb[k], 1e-5f * std::max(1.0f, std::fabs(fa[k]))) << seed << " " << v << " " << k;
            ++checked;
        }
    }
    EXPECT_EQ(checked, 20 * 4);
}

}  // namespace
