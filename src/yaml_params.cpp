#include "yaml_params.h"

#include <cmath>
#include <cstdlib>
#include <regex>
#include <stdexcept>

namespace
{
enum class Type { NONE, BOOL, INT, DOUBLE, STRING };

// PyYAML の Resolver(YAML 1.1)の bool・int・float の正規表現。2進・8進・16進・60進は正準設定に出ないので扱わない。
// 指数だけで小数点のない形(1e-5)は、PyYAML では文字列になる
const std::regex RE_BOOL("yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF");
const std::regex RE_INT("[-+]?(0|[1-9][0-9_]*)");
const std::regex RE_FLOAT("[-+]?[0-9][0-9_]*\\.[0-9_]*([eE][-+][0-9]+)?|\\.[0-9_]+([eE][-+][0-9]+)?"
                          "|[-+]?\\.(inf|Inf|INF)|\\.(nan|NaN|NAN)");

Type type_of(const YAML::Node &n)
{
    if (!n || !n.IsScalar())
        return Type::NONE;
    const std::string &s = n.Scalar();
    if (n.Tag() == "!") // 引用符つきは文字列
        return Type::STRING;
    if (std::regex_match(s, RE_BOOL))
        return Type::BOOL;
    if (std::regex_match(s, RE_INT))
        return Type::INT;
    if (std::regex_match(s, RE_FLOAT))
        return Type::DOUBLE;
    return Type::STRING;
}

std::string digits(const std::string &s)
{
    std::string out;
    for (char c : s)
        if (c != '_')
            out += c;
    return out;
}

bool as_double(const YAML::Node &n, double &v)
{
    Type t = type_of(n);
    if (t != Type::INT && t != Type::DOUBLE)
        return false;
    std::string s = digits(n.Scalar());
    char *end = nullptr;
    v = t == Type::INT ? static_cast<double>(std::strtoll(s.c_str(), &end, 10)) : std::strtod(s.c_str(), &end);
    if (*end != '\0')
        throw std::runtime_error("cannot parse number: " + s);
    return true;
}
}

YamlParamSource::YamlParamSource(const std::string &path) : root_(YAML::LoadFile(path))
{
    if (!root_.IsMap())
        throw std::runtime_error("config is not a map: " + path);
}

YAML::Node YamlParamSource::find(const std::string &key) const
{
    // Node への = は値の上書きになるので、たどるときは reset で付け替える
    YAML::Node n;
    n.reset(root_);
    size_t start = 0;
    while (true)
    {
        size_t slash = key.find('/', start);
        std::string part = key.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!n.IsMap())
            return YAML::Node();
        const YAML::Node &cn = n;
        YAML::Node child = cn[part];
        if (!child)
            return YAML::Node();
        n.reset(child);
        if (slash == std::string::npos)
            return n;
        start = slash + 1;
    }
}

bool YamlParamSource::get(const std::string &key, bool &v) const
{
    YAML::Node n = find(key);
    if (type_of(n) != Type::BOOL)
        return false;
    const char c = n.Scalar()[0];
    v = c == 'y' || c == 'Y' || c == 't' || c == 'T' || n.Scalar() == "on" || n.Scalar() == "On" || n.Scalar() == "ON";
    return true;
}

bool YamlParamSource::get(const std::string &key, int &v) const
{
    YAML::Node n = find(key);
    Type t = type_of(n);
    double d;
    if (t == Type::INT)
    {
        v = static_cast<int>(std::strtoll(digits(n.Scalar()).c_str(), nullptr, 10));
        return true;
    }
    if (t == Type::DOUBLE && as_double(n, d))
    {
        v = static_cast<int>(std::fmod(d, 1.0) < 0.5 ? std::floor(d) : std::ceil(d));
        return true;
    }
    return false;
}

bool YamlParamSource::get(const std::string &key, float &v) const
{
    double d;
    if (!as_double(find(key), d))
        return false;
    v = static_cast<float>(d);
    return true;
}

bool YamlParamSource::get(const std::string &key, double &v) const
{
    return as_double(find(key), v);
}

bool YamlParamSource::get(const std::string &key, std::string &v) const
{
    YAML::Node n = find(key);
    if (type_of(n) != Type::STRING)
        return false;
    v = n.Scalar();
    return true;
}

bool YamlParamSource::get(const std::string &key, std::vector<double> &v) const
{
    YAML::Node n = find(key);
    if (!n || !n.IsSequence())
        return false;
    std::vector<double> out;
    for (const YAML::Node &e : n)
    {
        double d;
        if (!as_double(e, d))
            return false;
        out.push_back(d);
    }
    v = out;
    return true;
}
