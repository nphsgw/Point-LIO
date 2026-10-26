#pragma once
// 正準設定(rosparam dump の YAML)をパラメータの読み出し元にする。pointlio_offline 用。
#include <string>
#include <yaml-cpp/yaml.h>
#include "parameters.h"

// 値の型は、rosparam load(PyYAML、YAML 1.1)が付ける型と同じ規則で決め、型変換は roscpp の getParam に合わせる。
// - bool: bool 型のみ
// - int: int 型、または double 型を四捨五入(roscpp と同じく fmod(d, 1) < 0.5 なら切り捨て)
// - double・float: int 型または double 型
// - string: 文字列型のみ
// - vector<double>: 要素がすべて int 型か double 型の列
class YamlParamSource : public ParamSource
{
public:
    explicit YamlParamSource(const std::string &path); // 読めなければ例外
    bool get(const std::string &key, bool &v) const override;
    bool get(const std::string &key, int &v) const override;
    bool get(const std::string &key, float &v) const override;
    bool get(const std::string &key, double &v) const override;
    bool get(const std::string &key, std::string &v) const override;
    bool get(const std::string &key, std::vector<double> &v) const override;

private:
    YAML::Node find(const std::string &key) const; // "mapping/satu_acc" のように '/' で入れ子をたどる
    YAML::Node root_;
};
