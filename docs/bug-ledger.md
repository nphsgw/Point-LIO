# バグ台帳

疑いを「仮説 → 再現済み → 分類済み」で管理する([agents/README.md](../agents/README.md))。テスト化してよいのは再現済みのものだけ。

- 状態: **仮説**(コードを読んだだけ、または観測が間接的)/ **再現済み**(実行で再現した)/ **分類済み**(分類と決定が人の判断で確定した)
- 分類: (a) メモリ/UB系 / (b) アルゴリズム/仕様系 / (c) 未確認。「分類案」は起票者の案で、確定ではない
- 決定((b) は 直す/仕様として固定/保留、(a) は B で直すか): 人が決める。B に (a) を含める方針は承認済み(2026-10-26)だが、1件ごとの修正は台帳での承認を要する
- 1件の修正は1コミット。決定を記録してから B を変える

| ID | 状態 | 分類案 | 内容 | 証拠 | 決定 |
| --- | --- | --- | --- | --- | --- |
| BL-001 | 再現済み | (a) | `/cloud_registered` の点群メッセージで、`point_step` 48 バイトのうち fields に載らない16バイトが実行ごとに変わる。未初期化メモリがそのまま送られているとみられる。点の値には影響しない | 手順0・0b の44回で、fields のバイトは一致し、生バイトは毎回異なった([ros-free-harness.md](../agents/plans/ros-free-harness.md) §2)。未初期化であることはサニタイザ(MSan)で確かめていない | 未決定 |
| BL-002 | 仮説 | (a) | 購読コールバック(`AsyncSpinner(0)` のスレッド)とメインループが、`lidar_buffer`・`time_buffer`・`imu_deque` を排他制御なしで読み書きする。`mtx_buffer` の lock がコメントアウトされている | `src/li_initialization.cpp` 27・35・100・106 行。ROS 経路の44回では結果に表れていない。TSan は未実行 | 未決定 |
| BL-003 | 再現済み | (b)(仕様として知っておく挙動の候補) | PCL 1.10 の `pcl::fromROSMsg` は、フィールドの配置が構造体と同じなら隣り合うフィールドを1回の `memcpy` にまとめ、名前の対応しないフィールドのバイトも写す。Ouster で時刻のフィールド名を変えても、前処理の出力が変わらなかった | T6 の注入による確認([ros-free-harness.md](../agents/plans/ros-free-harness.md) §10)。Point-LIO ではなく PCL の挙動で、バグとは限らない。Rust の前処理はフィールド名で解釈するので、C++ と挙動が分かれうる | 未決定 |
| BL-004 | 仮説 | (b) | `/aft_mapped_to_init`(と `odom.bin`)の姿勢の共分散36値に代入がなく、常に0になる。出力から状態共分散の発散は検出できない | `src/` に `pose.covariance` への代入がない(書き出しは `src/offline_output.cpp` 60 行だけ)。`publish_odometry`(`src/laserMapping.cpp` 270〜290 行付近)。実行の出力で0であることは確かめていない | 未決定 |
| BL-005 | 仮説 | (b) | `pcd_save_interval = -1` のとき `scan_wait_num >= pcd_save_interval` が常に真になり、毎フレーム PCD を書く。config のコメント(-1 は1ファイル)と異なる | `src/laserMapping.cpp` 213 行。実行では未確認。正準設定は `pcd_save_en: false` なので評価には影響しない | 未決定 |
| BL-006 | 仮説 | (a) | `Log/pos_log.txt` の `fopen` の戻り値を検査しない。書けない環境で `runtime_pos_log` が真だと、NULL の `FILE*` に書いて落ちうる | `src/laserMapping.cpp` 359 行と、680・873・990 行の `runtime_pos_log` の分岐。実行では未確認。`pointlio_offline` は `runtime_pos_log_enable` が真なら実行を拒否する | 未決定 |
