#!/usr/bin/env python3
"""Make a temporary instrumented copy of B's generator; never edit B or golden/."""
from pathlib import Path
import sys


INJECTION = r'''
// Test-only injection. The serialized inputs remain identical to B.
Vec injected_run(const Case &c, const Vec &in, size_t index)
{
    const char *env = std::getenv("PL_MATH_INJECTION");
    std::string mode = env ? env : "baseline";
    if (mode != "baseline" && mode != "perturb" && mode != "f32_round" &&
        mode != "left_boxplus" && mode != "quat_order" && mode != "jacobian" && mode != "dt_shift")
        throw std::runtime_error("unknown injection " + mode);
    Vec changed = in;
    if (mode == "dt_shift" && (c.name == "predict_state" || c.name == "predict_cov"))
        changed.back() = c.inputs[(index + 1) % c.inputs.size()].back();
    Vec out = c.run(changed);
    if (mode == "perturb") {
        auto at = std::max_element(out.begin(), out.end(), [](double a, double b) { return std::abs(a) < std::abs(b); });
        // An all-zero record has no nonzero relative perturbation; report it in the results.
        *at += std::abs(*at) * 1e-9;
    } else if (mode == "f32_round") {
        for (double &x : out) x = static_cast<double>(static_cast<float>(x));
    } else if (mode == "left_boxplus" && c.name == "so3_boxplus") {
        out.clear();
        put_rowmajor(out, SO3d::exp(get3(in, 9)) * get33(in, 0));
    } else if (mode == "left_boxplus" && c.name == "state_boxplus") {
        state_output s = get_state(in, 0);
        s.boxplus(MTK::vectview<const double, 30>(in.data() + 42));
        s.rot = SO3d::exp(get3(in, 45)) * get33(in, 3);
        s.offset_R_L_I = SO3d::exp(get3(in, 48)) * get33(in, 12);
        out.clear();
        put_state(out, s);
    } else if (mode == "quat_order" && c.name == "quat_from_matrix") {
        out = Vec{out[3], out[0], out[1], out[2]}; // xyzw -> wxyz
    } else if (mode == "jacobian" && c.name == "df_dx_output") {
        out[12] = 0; // omit df[0]/dx[12] = 1 (position derivative / velocity)
    }
    return out;
}

'''


def main():
    source = Path(sys.argv[1]).read_text()
    marker = "std::string encode(const Case &c)"
    call = "Vec out = c.run(in);"
    if source.count(marker) != 1 or source.count(call) != 1:
        raise RuntimeError("B generator changed; review injection points")
    source = source.replace("#include <cmath>", "#include <cmath>\n#include <cstdlib>")
    source = source.replace(marker, INJECTION + marker)
    source = source.replace(call, "Vec out = injected_run(c, in, static_cast<size_t>(&in - c.inputs.data()));")
    Path(sys.argv[2]).write_text(source)


if __name__ == "__main__":
    main()
