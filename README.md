# M5Core2_HeartBIT
M5Stack Core2 と BITalino HeartBIT（Bluetooth Classic / SPPモデル）を直接 Bluetooth 接続し、PC を介さずにリアルタイムで心電図（ECG）波形と心拍数（BPM）を表示する PlatformIO プロジェクトです。

## 生成AIの利用について
本プロジェクトのソースコードの実装、信号処理アルゴリズムの設計、およびドキュメント（README等）の作成には、生成AI（Gemini4 Neon）を活用しています。<br>
生成されたコードについては、実機（M5Stack Core2 + BITalino HeartBIT）での動作確認および検証を行っています。

## 主な機能

- **スタンドアロン動作**: M5Core2（Bluetooth マスター）から HeartBIT へ直接 SPP 接続し、100 Hz で ECG データを取得
- **フレーム同期・誤り検出**: BITalino の 3 バイトバイナリフレームを CRC4 チェック付きでデコードし、通信ズレを自動補正
- **オンデバイス信号処理**:
  - ハイパスフィルタ（約 0.84 Hz）によるベースライン変動除去
  - 2 次バターワース・ローパスフィルタ（18 Hz）による 50/60 Hz 商用電源ノイズ低減
  - 簡易 Pan-Tompkins 法（微分 → 2 乗 → 80 ms 移動積分）による極性非依存の R 波検出
  - 直近 5 拍の中央値（Median）フィルタによる安定した BPM 算出
- **ちらつきのないリアルタイム描画**: PSRAM 上の `M5Canvas` を用いた約 20 fps のライブ ECG 波形（直近 3.2 秒分）・R 波検出マーカー・BPM 表示

## 動作環境・ハードウェア構成

| 項目 | 内容 |
| :--- | :--- |
| マイコン | M5Stack Core2（ESP32 / Bluetooth Classic 対応） |
| センサ | BITalino HeartBIT（Bluetooth Classic / SPP モデル） |
| 開発環境 | PlatformIO（VS Code） |
| フレームワーク | Arduino (`espressif32`) |
| 依存ライブラリ | `m5stack/M5Unified` |

> **注意**: M5Stack CoreS3 や AtomS3 など ESP32-S3 搭載機は BLE 専用のため、Bluetooth Classic（SPP）モデルの HeartBIT とは直接接続できません。

## ディレクトリ構成
Platformioが動作する最小構成です。
```text
├── src/
│   └── main.cpp            # Bluetooth通信・信号処理・画面描画
├── LICENSE
├── platformio.ini
└── README.md
```
## セットアップ手順
1. リポジトリのクローン
``` bash
git clone　https://github.com/YN1222124/M5Core2_HeartBIT.git
```
2. HeartBIT の MAC アドレスとチャンネルを設定
`src/main.cpp` の先頭を、お手元の HeartBIT の値に合わせて変更します。
```C++
// HeartBIT の Bluetooth MAC アドレス
uint8_t bitalinoMac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

// ECG センサのチャンネル番号 (0 = A1, 1 = A2)
const uint8_t ECG_CHANNEL = 1;
const char* BT_PIN = "0000";
```

3. ビルドと書き込み
PlatformIO でビルド・書き込みを実行します。`platformio.ini` では `Bluetooth Classic` と `M5Unified` の併用に対応するため `board_build.partitions = huge_app.csv` を指定しています。

## 操作方法
|ボタン|表示ラベル|機能|
|:--:|:--:|:--|
|BtnA（左）|	[Flip View]	|画面に表示される ECG 波形の上下極性を反転（R 波検出には影響しません）|
|BtnB（中央）|	[Reset]	| R 波検出の適応しきい値および心拍数バッファをリセット|
|BtnC（右）|	[Reconnect]	|HeartBIT への Bluetooth 再接続を実行|

## デバッグ・チューニング
シリアルモニタ（`115200 bps`）を開くと、R 波を検出するたびに以下の形式でログが出力されます。
``` text
BPM: 66.7 (RR: 900 ms, E: 142.5, Th: 58.2)
```
`E`: 現在の R 波エネルギー値 <br>
`Th`: 現在の検出しきい値 

波形上の赤い丸（R 波検出マーカー）がノイズに誤反応する、または R 波を取りこぼす場合は、`src/main.cpp` 内の `threshold = max(30.0f, peakEnergy * 0.40f);` の係数（`0.40f`）を調整してください。

## トラブルシューティング
* 接続に失敗する (Connect Failed!)
Bluetooth Classic（SPP）は 1 対 1 通信です。PC やスマートフォンが HeartBIT に接続されたままだと M5Core2 から接続できないため、HeartBIT の電源を入れ直してから BtnC を押してください。
* 波形に細かいノイズが多い
書き込み完了後に USB ケーブルを抜き、M5Core2 を内蔵バッテリーで駆動すると PC 経由の商用電源ノイズが低減します。<br>
電極が肌に密着しているか、乾式電極の場合は指先が乾燥しすぎていないか確認してください。

## 免責事項
本プロジェクトは学習・研究およびプロトタイピングを目的としたものです。医療機器ではなく、医療上の診断・監視・治療等の用途には使用できません。

## License
This project is licensed under the MIT License - see the LICENSE file for details.
