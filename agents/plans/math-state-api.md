# 数学と状態の表現: 手順4の公開 API 案

状態: 承認済み(2026-10-31、人の判断。テスト作成前に型とシグネチャの案を提示して承認を得た)。承認済みの [モジュールプラン](math-state.md) 3.4・5節に対応する。数値の実装は手順5で行う。

## 型

`linalg` で固定長配列の型エイリアスだけを公開する。行列は行優先で `m[row][column]`。単位は [状態の定義](../../docs/state-definition.md) と同じ。

```rust
pub type Vec3 = [f64; 3];
pub type Mat3 = [[f64; 3]; 3];
pub type Vec30 = [f64; 30];
pub type Mat30 = [[f64; 30]; 30];
```

要素の読み書きと独立したテスト参照の作成に、独自の演算子や変換処理を必要としない。Eigen の式ごとの演算順に対応する内部の行列積・縮約などは `pub(crate)` 以下とし、汎用の公開演算子にはしない。テスト側の参照計算は独立に書く。

## SO3

```rust
pub fn exp(v: &Vec3, scale: f64) -> Mat3;
pub fn log(rotation: &Mat3) -> Vec3;
pub fn boxplus(rotation: &mut Mat3, delta: &Vec3, scale: f64);
pub fn boxminus(rotation: &Mat3, reference: &Mat3) -> Vec3;
pub fn a_matrix(v: &Vec3) -> Mat3;
pub fn exp3(v1: f64, v2: f64, v3: f64) -> Mat3;
pub fn quat_from_matrix(rotation: &Mat3) -> [f64; 4];
```

`boxplus` は C++ と同じく対象を更新し、右から `Exp(scale·delta)` を掛ける。`boxminus(rotation, reference)` は `Log(referenceᵀ·rotation)`。`scale` は既定引数の代わりに明示する。`exp3` は C++ の3スカラーの形を保つ。クォータニオンの返値は xyzw で、正規化しない。

## 状態

```rust
#[derive(Clone, Debug, PartialEq)]
pub struct StateOutput {
    pub pos: Vec3,
    pub rot: Mat3,
    pub offset_r_l_i: Mat3,
    pub offset_t_l_i: Vec3,
    pub vel: Vec3,
    pub omg: Vec3,
    pub acc: Vec3,
    pub gravity: Vec3,
    pub bg: Vec3,
    pub ba: Vec3,
}

impl StateOutput {
    pub fn boxplus(&mut self, delta: &Vec30, scale: f64);
    pub fn boxminus(&self, reference: &Self) -> Vec30;
    pub fn oplus(&mut self, delta: &Vec30, scale: f64);
}
```

フィールドは C++ の宣言順と同じ。外部パラメータ名は Rust の snake_case にする。構築は全フィールドを指定するリテラルとし、この段階で `Default` や値を補正するコンストラクタは追加しない。回転も直接設定でき、構築時の正規化・直交化はしない。30次元の添字と、42値の PLGOLD01 の並びは既存の定義から変えない。メモリをそのままシリアライズせず、各フィールドを明示的に読み書きする。

## 過程モデルと予測

```rust
// process
pub fn f_output(x: &StateOutput) -> Vec30;
pub fn df_dx_output(x: &StateOutput) -> Mat30;

// esekf
pub fn predict(
    x: &mut StateOutput,
    p: &mut Mat30,
    q: &Mat30,
    dt: f64,
    predict_state: bool,
    prop_cov: bool,
);
```

過程モデルは状態を変更しないので共有参照で受ける。参照されない `input_ikfom` は承認済みプランどおり受けない。`predict` は状態と共分散を借用して更新し、Q と dt は変更しない。C++ と同じ2つのフラグを保つ。両方 true のときは状態を更新した後で f と df/dx を再計算し、両方 false のときは何もしない。範囲内での数値検証は既定の片方ずつの呼び出しで行う。

## PLGOLD01 の入出力 (承認後の追記)

承認した API 案に載っていなかったが、手順3で `golden` モジュールを公開した。これはプラン 3.1 の構成にある golden.rs で、`math_golden` の bin と入出力のテストから使う。この2つは別の crate として扱われるので、公開が要る。数学の API ではなく、照合用の道具である。2026-11-01 の手順8の差分レビューで指摘があり、範囲内の逸脱として記録した。

```rust
pub struct Component { pub name: &'static str, pub input_len: usize, pub output_len: usize }
pub const COMPONENTS: [Component; 13];
pub struct Record { pub input: Vec<f64>, pub output: Vec<f64> }
pub fn decode(bytes: &[u8], component: Component) -> io::Result<Vec<Record>>;
pub fn encode(records: &[Record], component: Component) -> io::Result<Vec<u8>>;
```

## Red の作り方

型・フィールド・シグネチャを定義し、数値関数の本体は `todo!` にする。テストはコンパイルでき、14件が未実装関数に到達して失敗することを確認する。これは数値アサーションの検出力の確認とは区別する。実装後の14件の通過と、BL-008 の修正を戻した場合の失敗は後続手順で確認する。

L1 の別の形の Rodrigues、クォータニオン経由の Log、Jacobi 法と行列・ベクトルの参照計算はテスト側に置く。数値微分の摂動と差はプランどおり実装側の状態の演算を使う。固定シード、検査件数、C++ と同じ許容誤差を維持する。手順3の仮出力バイナリはこの段階では変更しない。
