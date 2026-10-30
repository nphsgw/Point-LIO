// 数学と状態の表現の L2 ゴールデン出力の生成と照合(形式は golden/math/README.md)。
// 使い方: math_golden write <ディレクトリ>  ゴールデン出力を書く(更新は人の承認と専用コミット)
//         math_golden check <ディレクトリ>  同じ入力で計算し直し、既存のファイルとバイト単位で比べる
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "Estimator.h"

namespace {

using SO3d = MTK::SO3<double>;
typedef std::vector<double> Vec;
typedef esekfom::esekf<state_output, 30, input_ikfom> KfOutput;

struct Case {
    std::string name;
    uint32_t in_len, out_len;
    std::vector<Vec> inputs;
    std::function<Vec(const Vec &)> run;
};

std::mt19937_64 rng(20261026);

double uniform(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); }

Eigen::Vector3d direction()
{
    std::normal_distribution<double> n(0, 1);
    return Eigen::Vector3d(n(rng), n(rng), n(rng)).normalized();
}

Eigen::Vector3d vec_with_norm(double t) { return direction() * t; }

double log_uniform(double lo, double hi) { return std::exp(uniform(std::log(lo), std::log(hi))); }

void put(Vec &v, const Eigen::Vector3d &x) { v.insert(v.end(), x.data(), x.data() + 3); }

void put_rowmajor(Vec &v, const Eigen::MatrixXd &m)
{
    for (int r = 0; r < m.rows(); r++)
        for (int c = 0; c < m.cols(); c++) v.push_back(m(r, c));
}

Eigen::Vector3d get3(const Vec &v, int o) { return Eigen::Vector3d(v[o], v[o + 1], v[o + 2]); }

Eigen::Matrix3d get33(const Vec &v, int o)
{
    Eigen::Matrix3d m;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) m(r, c) = v[o + 3 * r + c];
    return m;
}

Eigen::Matrix<double, 30, 30> get3030(const Vec &v, int o)
{
    Eigen::Matrix<double, 30, 30> m;
    for (int r = 0; r < 30; r++)
        for (int c = 0; c < 30; c++) m(r, c) = v[o + 30 * r + c];
    return m;
}

// 状態の 42 値: pos 3、rot 9(行優先)、offset_R_L_I 9、offset_T_L_I 3、vel 3、omg 3、acc 3、gravity 3、bg 3、ba 3
void put_state(Vec &v, const state_output &s)
{
    put(v, s.pos);
    put_rowmajor(v, s.rot);
    put_rowmajor(v, s.offset_R_L_I);
    put(v, s.offset_T_L_I);
    put(v, s.vel);
    put(v, s.omg);
    put(v, s.acc);
    put(v, s.gravity);
    put(v, s.bg);
    put(v, s.ba);
}

state_output get_state(const Vec &v, int o)
{
    state_output s;
    s.pos = get3(v, o);
    s.rot = get33(v, o + 3);
    s.offset_R_L_I = get33(v, o + 12);
    s.offset_T_L_I = get3(v, o + 21);
    s.vel = get3(v, o + 24);
    s.omg = get3(v, o + 27);
    s.acc = get3(v, o + 30);
    s.gravity = get3(v, o + 33);
    s.bg = get3(v, o + 36);
    s.ba = get3(v, o + 39);
    return s;
}

state_output random_state()
{
    state_output s;
    s.pos = vec_with_norm(uniform(0, 50));
    s.rot = SO3d::exp(vec_with_norm(uniform(0, 3)));
    s.offset_R_L_I = SO3d::exp(vec_with_norm(uniform(0, 0.5)));
    s.offset_T_L_I = vec_with_norm(uniform(0, 0.5));
    s.vel = vec_with_norm(uniform(0, 5));
    s.omg = vec_with_norm(uniform(0, 5));
    s.acc = vec_with_norm(uniform(0, 20));
    s.gravity = vec_with_norm(uniform(9.7, 9.9));
    s.bg = vec_with_norm(uniform(0, 0.05));
    s.ba = vec_with_norm(uniform(0, 0.5));
    return s;
}

Eigen::Matrix<double, 30, 30> random_spd()
{
    std::normal_distribution<double> n(0, 1);
    Eigen::Matrix<double, 30, 30> a;
    for (int r = 0; r < 30; r++)
        for (int c = 0; c < 30; c++) a(r, c) = n(rng);
    return a * a.transpose() / 30 + Eigen::Matrix<double, 30, 30>::Identity() * 1e-4;
}

// 回転角の代表値: 閾値の境界(Exp 1e-7、Log 0.001 と tr > 3 − 1e-6)と、π − 1e-3 までの一様な範囲。
// π の近くは台帳 BL-008 の決定まで含めない
std::vector<double> angles()
{
    std::vector<double> t = {0.0, 1e-9, 0.99e-7, 1e-7, 1.01e-7, 0.999e-3, 1e-3, 1.001e-3, 0.5, 1.0, 2.0, 3.0, M_PI - 1e-3};
    for (int i = 0; i < 40; i++) t.push_back(log_uniform(1e-9, 1e-2));
    for (int i = 0; i < 40; i++) t.push_back(uniform(1e-2, M_PI - 1e-3));
    return t;
}

// 回転角が π の近くの入力(BL-008 の修正の後に追加、2026-10-30)。回転角を引数に取る部品の末尾に足す。
// 既存の記録を変えないよう、既存の入力をすべて作った後に、別のシードの乱数列で作る。
// 回転角は π − d。d は修正の分岐(d < 3.16e-4)の境界の前後、π ちょうど、1e-12〜1e-3 の対数一様
void add_near_pi(std::vector<Case> &cs)
{
    rng.seed(20261030);
    std::vector<double> t;
    for (double d : {1e-3, 4e-4, 3.2e-4, 3.1e-4, 2e-4, 1e-4, 1e-5, 1e-7, 1e-10, 1e-12, 0.0}) t.push_back(M_PI - d);
    for (int i = 0; i < 20; i++) t.push_back(M_PI - log_uniform(1e-12, 1e-3));

    auto vec_in = [](double x) {
        Vec in;
        put(in, vec_with_norm(x));
        return in;
    };
    auto rot_in = [](double x) {
        Vec in;
        put_rowmajor(in, SO3d::exp(vec_with_norm(x)));
        return in;
    };
    auto boxplus_in = [](double x) {
        Vec in;
        put_rowmajor(in, SO3d::exp(vec_with_norm(uniform(0, 3))));
        put(in, vec_with_norm(x));
        return in;
    };
    auto boxminus_in = [](double x) {
        Eigen::Matrix3d b = SO3d::exp(vec_with_norm(uniform(0, 3)));
        Vec in;
        put_rowmajor(in, b * SO3d::exp(vec_with_norm(x)));
        put_rowmajor(in, b);
        return in;
    };
    std::vector<std::pair<std::string, std::function<Vec(double)>>> make = {
        {"so3_exp", vec_in},     {"so3_log", rot_in},  {"so3_boxplus", boxplus_in},     {"so3_boxminus", boxminus_in},
        {"a_matrix", vec_in},    {"so3math_exp3", vec_in}, {"quat_from_matrix", rot_in}};
    for (const auto &m : make) {
        auto c = std::find_if(cs.begin(), cs.end(), [&](const Case &x) { return x.name == m.first; });
        if (c == cs.end()) throw std::runtime_error("no case " + m.first);
        for (double x : t) c->inputs.push_back(m.second(x));
    }
}

std::vector<Case> cases()
{
    std::vector<Case> cs;
    {
        Case c{"so3_exp", 3, 9, {}, [](const Vec &in) {
                   Vec o;
                   put_rowmajor(o, SO3d::exp(get3(in, 0)));
                   return o;
               }};
        for (double t : angles()) {
            Vec in;
            put(in, vec_with_norm(t));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"so3_log", 9, 3, {}, [](const Vec &in) {
                   Vec o;
                   put(o, SO3d::log(SO3d(get33(in, 0))));
                   return o;
               }};
        for (double t : angles()) {
            Vec in;
            put_rowmajor(in, SO3d::exp(vec_with_norm(t)));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"so3_boxplus", 12, 9, {}, [](const Vec &in) {
                   SO3d r(get33(in, 0));
                   Eigen::Vector3d d = get3(in, 9);
                   r.boxplus(MTK::vectview<const double, 3>(d.data()));
                   Vec o;
                   put_rowmajor(o, r);
                   return o;
               }};
        for (double t : angles()) {
            Vec in;
            put_rowmajor(in, SO3d::exp(vec_with_norm(uniform(0, 3))));
            put(in, vec_with_norm(t));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"so3_boxminus", 18, 3, {}, [](const Vec &in) {
                   SO3d a(get33(in, 0)), b(get33(in, 9));
                   Eigen::Vector3d r;
                   a.boxminus(MTK::vectview<double, 3>(r.data()), b);
                   Vec o;
                   put(o, r);
                   return o;
               }};
        for (double t : angles()) {
            Eigen::Matrix3d b = SO3d::exp(vec_with_norm(uniform(0, 3)));
            Vec in;
            put_rowmajor(in, b * SO3d::exp(vec_with_norm(t)));
            put_rowmajor(in, b);
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"a_matrix", 3, 9, {}, [](const Vec &in) {
                   vect3 v(get3(in, 0));
                   Vec o;
                   put_rowmajor(o, MTK::A_matrix(v));
                   return o;
               }};
        std::vector<double> t = angles();
        for (double x : {1e-12, 0.99e-11, 1e-11, 1.01e-11}) t.push_back(x);
        for (double x : t) {
            Vec in;
            put(in, vec_with_norm(x));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"so3math_exp3", 3, 9, {}, [](const Vec &in) {
                   Vec o;
                   put_rowmajor(o, Exp(in[0], in[1], in[2]));
                   return o;
               }};
        std::vector<double> t = angles();
        for (double x : {0.99e-5, 1e-5, 1.01e-5}) t.push_back(x);
        for (double x : t) {
            Vec in;
            put(in, vec_with_norm(x));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"quat_from_matrix", 9, 4, {}, [](const Vec &in) {
                   Eigen::Quaterniond q(get33(in, 0));
                   return Vec{q.coeffs()[0], q.coeffs()[1], q.coeffs()[2], q.coeffs()[3]};
               }};
        for (double t : angles()) {
            Vec in;
            put_rowmajor(in, SO3d::exp(vec_with_norm(t)));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"state_boxplus", 72, 42, {}, [](const Vec &in) {
                   state_output s = get_state(in, 0);
                   s.boxplus(MTK::vectview<const double, 30>(in.data() + 42));
                   Vec o;
                   put_state(o, s);
                   return o;
               }};
        std::normal_distribution<double> n(0, 0.3);
        for (int i = 0; i < 60; i++) {
            Vec in;
            put_state(in, random_state());
            for (int j = 0; j < 30; j++) in.push_back(n(rng));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"state_boxminus", 84, 30, {}, [](const Vec &in) {
                   state_output a = get_state(in, 0), b = get_state(in, 42);
                   Eigen::Matrix<double, 30, 1> r;
                   a.boxminus(MTK::vectview<double, 30>(r.data()), b);
                   return Vec(r.data(), r.data() + 30);
               }};
        std::normal_distribution<double> n(0, 0.3);
        for (int i = 0; i < 60; i++) {
            state_output b = random_state(), a = b;
            Eigen::Matrix<double, 30, 1> d;
            for (int j = 0; j < 30; j++) d(j) = n(rng);
            a.boxplus(MTK::vectview<const double, 30>(d.data()));
            Vec in;
            put_state(in, a);
            put_state(in, b);
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"f_output", 42, 30, {}, [](const Vec &in) {
                   state_output s = get_state(in, 0);
                   Eigen::Matrix<double, 30, 1> f = get_f_output(s, input_ikfom());
                   return Vec(f.data(), f.data() + 30);
               }};
        for (int i = 0; i < 60; i++) {
            Vec in;
            put_state(in, random_state());
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"df_dx_output", 42, 900, {}, [](const Vec &in) {
                   state_output s = get_state(in, 0);
                   Vec o;
                   put_rowmajor(o, df_dx_output(s, input_ikfom()));
                   return o;
               }};
        for (int i = 0; i < 60; i++) {
            Vec in;
            put_state(in, random_state());
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"predict_state", 43, 42, {}, [](const Vec &in) {
                   KfOutput kf;
                   kf.init_dyn_share_modified_3h(get_f_output, df_dx_output, h_model_output, h_model_IMU_output);
                   state_output s = get_state(in, 0);
                   kf.change_x(s);
                   Eigen::Matrix<double, 30, 30> q = Eigen::Matrix<double, 30, 30>::Zero();
                   input_ikfom u;
                   double dt = in[42];
                   kf.predict(dt, q, u, true, false);
                   Vec o;
                   put_state(o, kf.get_x());
                   return o;
               }};
        for (int i = 0; i < 60; i++) {
            Vec in;
            put_state(in, random_state());
            in.push_back(log_uniform(1e-5, 0.1));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    {
        Case c{"predict_cov", 1843, 900, {}, [](const Vec &in) {
                   KfOutput kf;
                   kf.init_dyn_share_modified_3h(get_f_output, df_dx_output, h_model_output, h_model_IMU_output);
                   state_output s = get_state(in, 0);
                   kf.change_x(s);
                   Eigen::Matrix<double, 30, 30> p = get3030(in, 42), q = get3030(in, 942);
                   kf.change_P(p);
                   input_ikfom u;
                   double dt = in[1842];
                   kf.predict(dt, q, u, false, true);
                   Vec o;
                   put_rowmajor(o, kf.get_P());
                   return o;
               }};
        for (int i = 0; i < 30; i++) {
            Vec in;
            put_state(in, random_state());
            put_rowmajor(in, random_spd());
            Eigen::Matrix<double, 30, 30> q = Eigen::Matrix<double, 30, 30>::Zero();
            for (int j = 0; j < 30; j++) q(j, j) = log_uniform(1e-8, 1e-1);
            put_rowmajor(in, q);
            in.push_back(log_uniform(1e-5, 0.1));
            c.inputs.push_back(in);
        }
        cs.push_back(c);
    }
    add_near_pi(cs);
    return cs;
}

std::string encode(const Case &c)
{
    std::string b("PLGOLD01", 8);
    auto u32 = [&](uint32_t x) { b.append(reinterpret_cast<const char *>(&x), 4); };
    u32(1);
    u32(static_cast<uint32_t>(c.inputs.size()));
    u32(c.in_len);
    u32(c.out_len);
    for (const Vec &in : c.inputs) {
        Vec out = c.run(in);
        if (in.size() != c.in_len || out.size() != c.out_len) throw std::runtime_error("length mismatch in " + c.name);
        b.append(reinterpret_cast<const char *>(in.data()), in.size() * 8);
        b.append(reinterpret_cast<const char *>(out.data()), out.size() * 8);
    }
    return b;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 3 || (std::string(argv[1]) != "write" && std::string(argv[1]) != "check")) {
        std::cerr << "usage: math_golden write|check <dir>\n";
        return 2;
    }
    bool write = std::string(argv[1]) == "write";
    int bad = 0, n = 0;
    for (const Case &c : cases()) {
        std::string path = std::string(argv[2]) + "/" + c.name + ".bin", b = encode(c);
        n++;
        if (write) {
            std::ofstream(path, std::ios::binary) << b;
            std::cout << "wrote " << path << " records=" << c.inputs.size() << "\n";
            continue;
        }
        std::ifstream f(path, std::ios::binary);
        std::string old((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        bool ok = f.good() || f.eof();
        ok = ok && old == b;
        std::cout << (ok ? "ok   " : "FAIL ") << c.name << " records=" << c.inputs.size() << "\n";
        bad += !ok;
    }
    if (n == 0) return 1;
    return bad ? 1 : 0;
}
