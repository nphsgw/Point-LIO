# 数学と状態の表現: 手順6の検証 (2026-11-01)

対象は `30e5d51` の C (BL-011 後)。数値実装・L1 の入力や期待値・ゴールデン・照合器・登録済み許容誤差は変更していない。比較用の変更はリポジトリ外のコピーにだけ適用した。

## 条件と再実行

- イメージ: `point-lio-rust`、ID `sha256:1aec8ce20e45bf85982dc8fd4cbd90e45ee8c58f65d61b6a07f5b6813d79ddb5`。Rust 1.99.0 (`b940084d7`)、glibc `2.31-0ubuntu9.18`、コンテナの binutils `2.34-6ubuntu1.11`。
- CPU: AMD Ryzen 7 7700X、x86-64。`OMP_NUM_THREADS=1`、`--network none`、依存取得なし、`--locked --offline`。`RUSTFLAGS`・`target-cpu` は未設定。
- `golden/math/SHA256SUMS` を検証してから実行。Python は既存の共通照合器とバイト列の検査にだけ使い、Rust の数学はホストで実行していない。
- 記録先: `~/data/slamrun-runs/c-step6-30e5d51/rust`。ソースのコピーと SHA256、比較版のパッチ、コンパイル・L1・clippy・fmt・照合ログ、実バイナリ、動的シンボル、再配置、逆アセンブル、`LD_DEBUG=bindings` を保存した。B 側は同じディレクトリの `b-targets` と `b-full-build`。実行時は `/tmp/pointlio-rust-step6-20261101-final`・`/tmp/pointlio-rust-step6-B-targets-20261101`・`/tmp/pointlio-rust-step6-B-20261101` に出力し、2026-11-01 にコピーして `diff -r` で一致を確かめた。ログやパッチの中のパスは実行時の `/tmp` のまま。全ファイルの SHA256 は `~/data/slamrun-runs/c-step6-30e5d51/SHA256SUMS`。

リポジトリ直下で、新しいリポジトリ外の出力先を指定する:

```bash
bash rust/check_math_step6.sh /tmp/pointlio-math-step6-new
```

このスクリプトは記録の採取と数値検査を行う。逆アセンブルの関数ごとのレビューは別途必要であり、スクリプトの終了0だけで libm の検査を通過扱いにしない。ホストには Rust v0 の名前を復元できる binutils が必要 (今回は 2.42)。ホストの `objdump`・`addr2line` は固定環境で作った ELF を読むだけ。出所として、HEAD のコミット・`git diff`・未追跡を含む `git status --porcelain` (`source-status.txt`)・コピーした全ファイルの SHA256 を残す。`source-status.txt` は記録後に追加したもので、上記の保存済みの記録には含まれない。

## debug と release

既定の debug (opt-level 0) と release (opt-level 3) を、それぞれビルドして同じ入力を与えた。

- 共通照合器: 両方とも終了0、全13部品・1,205記録が通過。
- 13ファイルを丸ごとバイト比較: 全件一致。ヘッダ・入力・出力を含む。
- 今回の記録では、両方とも B の13ファイルともバイト一致。これは登録済み許容誤差の変更や一般の入力についての保証ではない。
- 既定版の release L1 は15件通過・無視0件。clippy (`--release --locked --offline --all-targets -- -D warnings`) と fmt (`--all -- --check`) も通過。

## C の libm 呼び出し

`nm -D` と `objdump -T` で、debug/release とも `acos`・`atan2`・`sincos` が未定義の動的シンボルであることを確認した。シンボルの版は全て `GLIBC_2.2.5`。コンテナの旧 `nm` では版が表示されないため、`objdump -T` も保存した。両バイナリの `LD_DEBUG=bindings` は、この3つが `/lib/x86_64-linux-gnu/libm.so.6` に解決されることを示した。

debug の経路 (アドレスは実行前の ELF の仮想アドレス):

| 元の関数 | 逆アセンブルで追った経路 |
| --- | --- |
| `so3::exp` | `0x40568` → `rodrigues` (`0x40dc0`) → `0x40e28` → `libm::sin_cos` (`0x45950`) |
| `so3::exp3` | `0x40b82` → 同じ `rodrigues` → 同じラッパー |
| `so3::a_matrix` | `0x40c7e` → 同じラッパー |
| 共通ラッパー | `0x4596b` で GOT `0x8b200` (`sincos`) をロードし、`0x4597a` の `call *%rax` で呼ぶ。行情報は `libm.rs:16` |
| `so3::log` | `0x405de` → `<f64>::acos` → `acos`。`0x40727`・`0x4093d` → `<f64>::atan2` → `atan2` |

release の全三角関数参照を列挙して、次のように対応づけた:

| 所属する関数 | 呼び出し位置 | 呼び出し先とソース上の経路 |
| --- | --- | --- |
| `so3::exp` | `0x2147c` | `sincos`、Exp → Rodrigues → `libm.rs:16` |
| `so3::exp3` | `0x21a87` | `sincos`、Exp3 → Rodrigues → `libm.rs:16` |
| `so3::boxplus` | `0x21cb6` | `sincos`、boxplus → Exp → Rodrigues → `libm.rs:16` (inline) |
| `so3::a_matrix` | `0x21f57` | `sincos`、A行列 → `libm.rs:16` |
| `so3::rodrigues` | `0x22151` | `sincos`、Rodrigues → `libm.rs:16` |
| `so3::log` | `0x2161b` | `acos`、`so3.rs:30` |
| `so3::log` | `0x21805`・`0x21966` | `atan2`、`so3.rs:59`・`:67` |

通常の release には行情報がないため、診断用に `CARGO_PROFILE_RELEASE_DEBUG=1` を指定した版も作り、`addr2line -afiC` で inline の呼び出し列を確認した。これは数値判定用の release と区別する。リンク後の配置が変わり、`.text` 全体のバイト一致はしない。SO3 の全8関数について、リンク先アドレス・RIP 相対の変位を除いた命令とシンボルによる呼び出し先の列が通常版と同じことを確認した。診断版の対応する位置は、表の位置に `0x30` を加えたもの。Exp・Exp3・boxplus・A行列・Rodrigues の全5箇所の行情報に `libm.rs:16` が現れた。

debug は3つの動的参照、release は上表の8箇所で全て。`sin`・`cos` の単独の参照はなく、Log に `sincos` はない。表以外に直接三角関数を呼ぶ箇所、出所を対応づけられない参照は残らなかった。boxplus に含まれるのは Exp の inline 展開である。

## BL-011 後の B との対応

同じイメージ・CPU・`OMP_NUM_THREADS=1` で、現行 B の `math_golden` と `pointlio_offline` もビルドした。ソースはリポジトリの読み取り専用 mount。記録先は `~/data/slamrun-runs/c-step6-30e5d51/b-targets`。

全ターゲットの `catkin_make` は、無関係な Livox ドライバの SDK ヘッダ (`livox_def.h`・`livox_sdk.h`) 不足で失敗した (`~/data/slamrun-runs/c-step6-30e5d51/b-full-build/build.log`)。その後、[以前の B 検証](bl011-b-fix-validation.md)に記載済みの対象限定コマンドでビルドに成功した:

```bash
# 固定イメージ内で ROS と /ws/devel/setup.sh を source した後、/ws で実行
catkin_make math_tests math_golden pointlio_offline --cmake-args -DCATKIN_WHITELIST_PACKAGES="" -DCMAKE_VERBOSE_MAKEFILE=ON
```

コンパイルログと `flags.make` で、実際のフラグは `-O3 -g -fopenmp`、`-march`・`-ffast-math` なしを確認。`math_golden check` は全13部品・1,205記録で成功した。今回は B の `math_tests` はビルドしたが実行していない。実データでの offline 実行も行っていない。

両バイナリの `nm -D`・再配置・`objdump -d -C -l` を調べた:

| B の対象関数 | `math_golden` の呼び出し位置 | `pointlio_offline` の呼び出し位置 | 解決先 |
| --- | --- | --- | --- |
| `MTK::SO3<double>::exp` | `0x2f03f` | `0x5d42b` | `sincos@GLIBC_2.2.5` |
| `MTK::A_matrix` | `0x30762` | `0x60732` | 同上 |
| `Exp<double>(v1,v2,v3)` | `0x2eb2e` | `0xa3823` | 同上 |
| `MTK::SO3<double>::log` | `0x2e27c`、`0x2e04a`・`0x2e243` | 関数本体なし | `acos`、`atan2`・`atan2` |

各バイナリの `sincos` 呼び出しは表の3箇所だけ。対象の3関数に単独の `sin`・`cos` はなく、Log の本体がある `math_golden` では `acos`・`atan2` だけを呼ぶ。`math_golden` の実行時の binding でも glibc の `sincos`・`acos`・`atan2` を確認した。B の両バイナリには範囲外の `SO3ToEuler` の `atan2` と `Set_init` の `acos` もあり、今回 C へ移した関数と混同しない。

## `sincos` を外した比較版

コピーの `libm.rs` だけを、次の安全な実装に置き換えて release でビルドした:

```rust
pub(crate) fn sin_cos(x: f64) -> (f64, f64) {
    let sin = x.sin();
    let cos = std::hint::black_box(x).cos();
    (sin, cos)
}
```

動的シンボルに `sin`・`cos` があり、`sincos` はない。上表の5つの経路は全て別々の `sin`・`cos` 呼び出しになり、実行時にも glibc の同名の関数へ解決された。例えば Exp は `0x2149c` の `sin` と `0x214be` の `cos`、A行列は `0x21f8d` と `0x21faf`。Log の `acos`・`atan2` は維持された。

この呼び分けを確認後、保存した出力で共通照合器を再実行し、終了0を確認した (`comparison-after-disassembly-review.log`)。全13部品の不一致件数は0、合計0/1,205記録。登録された比較だけでなく、生の出力バイトの不一致も0件だった。特にビット一致を求める `a_matrix` も0/128件。今回の入力集合では差が出なかったという記録であり、別々の呼び出しが常に `sincos` と同じになる根拠にはしない。プランどおり、この比較版の失敗を必須にはしない。

## BL-008 を戻した検出試験

BL-011 後の通常分岐をそのまま使うと、BL-008 の修正前の式を再現できない。そのため、コピーの `c < -1 + 5e-8` の領域だけを A の `(0.5 * theta / sin(theta)) * K` に戻し、他の領域の BL-011 の式と小角の判定は維持した。テスト・入力・比較基準は変更していない。

- コンパイル成功後、release L1 は終了101。15件中13件通過・2件失敗・無視0件。
- `so3_log_finite_near_pi`: `math_l1.rs:125` の Exp(Log(R)) の数値アサーションが `π − 1e-7` で失敗。
- `so3_log_accurate_around_near_pi_branch`: `math_l1.rs:149` の Log と真値の差の数値アサーションが `π − 0.00031` で失敗。
- BL-011 の固定入力の回帰テストは通過しており、通常分岐の修正をまとめて戻した試験ではない。
- 共通照合器は終了2。`so3_log.bin` の record 108 (0始まり) に NaN/Inf を検出した。入力は B と同じ。照合器がここで停止するため、残り全ての部品を判定したとは扱わない。

既定版で同じ L1・L2 が通過し、修正前の数値式で対応する2件と照合が落ちた。単なるビルド失敗を Red としていない。比較版は記録先のソースコピーとパッチとしてのみ保存し、製品の Rust コードには適用していない。

## unsafe と未完了の範囲

`rust/**/*.rs` 全体を検索し、実コードの unsafe は `pointlio-math/src/libm.rs` の2箇所だけだった:

| 箇所 | 行数 (開始行から閉じ括弧を含む) |
| --- | --- |
| `unsafe extern "C"`、`libm.rs:6` | 3行 |
| `unsafe` 呼び出しブロック、`libm.rs:15` | 3行 |
| 合計 | **2箇所・6行** |

`deny(unsafe_code)`・`allow(unsafe_code)` の属性やコメントはブロックの行数に含めない。生ポインタは上記の extern 宣言の出力引数2本。別の unsafe 関数・unsafe impl はない。安全性の前提は呼び出し直前のコメントにあり、crate 全体の deny と `libm` だけの allow を維持している。

この数え上げとバイナリ検査は、別セッションの差分・FFI メモリ安全レビューの代わりにはしない。手順7の確定、そのレビュー、全体の移植や実データでの C の評価は今回の対象外。既知の θ=1e-3 の近似分岐の制約も変更していない。
