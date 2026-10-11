# ime2nd

Windows 11 のタスクトレイ常駐アプリ。IME が ON のとき、開始キーの Shift（初期値は左 Shift。トレイメニューで右 Shift に変更可）を押しながらの文字キー（英字・数字・記号・Space）を横取りし（もう一方の Shift は通常の Shift のままです）、
キャレット位置の小窓に**小文字で**溜め（もう一方の Shift も押しながら打つと大文字）、開始キーの Shift を離した瞬間に一括でアプリへ入力します。

- 管理者権限は不要です（`asInvoker`）。自動起動は `HKCU\...\Run` のみ使用します。
- 管理者権限で動いているアプリ（UIPI）には介入できず、そのアプリでは何もしません。
- JIS / US 配列どちらでも動作します（仮想キーで判定し、記号は現在の配列に応じた文字に変換します）。

## ビルド / 実行

```
build.bat            -> build\ime2nd.exe  (CMake + Ninja + MSVC。VS 同梱の cmake を自動で使用)
build\ime2nd.exe     -> 常駐開始（トレイアイコンの右クリックで 有効/無効・開始キー(左/右 Shift)・自動起動・終了）
build\ime2nd.exe --selftest   -> IME状態・キャレット取得・小窓・フック遅延を確認し build\selftest.txt に出力
```

開始キー+英字の入力中: 開始キー+Space で半角スペースを追加、Backspace で1文字削除、Esc で破棄。

## 構成

- `KeyboardHook` — `WH_KEYBOARD_LL`。判定のみで重い処理はしない（約300ms超でフックが外されるため）
- `ImeState` — `ImmGetDefaultIMEWnd` + `WM_IME_CONTROL(IMC_GETOPENSTATUS)`、ワーカースレッドでキャッシュ
- `CaretLocator` — UI Automation(TextPattern) → MSAA(OBJID_CARET) → `GetGUIThreadInfo` → マウス位置
- `PopupWindow` — `WS_EX_NOACTIVATE|TOPMOST` の小窓（フォーカスを奪わない）
- `TextInjector` — `SendInput(KEYEVENTF_UNICODE)` で一括送信

## 設定

トレイアイコンの右クリック →「英字入力の開始キー」で、**左 Shift / 右 Shift / CapsLock** のうち 1 つを選べます（初期値は左 Shift）。いずれも、押している間だけ有効で、離した瞬間に一括入力します。

- **Shift を選んだとき**: もう一方の Shift は通常の Shift として働きます（開始キーを押している間にもう一方も押すと、英字が大文字になります）。
- **CapsLock を選んだとき**: CapsLock を押している間だけ有効で、Shift（左右どちらでも）を一緒に押すと大文字・Shift 付きの記号になります。
  CapsLock の押下は横取りして OS に渡さないので、**Caps Lock の切り替えは使えなくなります**（Shift を選び直すと元に戻ります）。
  メニューで CapsLock を選んだ時点で Caps Lock が ON なら、自動で OFF にします。
  JIS 配列キーボードの「英数」キーは CapsLock とは別のキーコードなので、対象外です（Shift+英数 = CapsLock のときだけ反応します）。

選択は `HKCU\Software\ime2nd` の `TriggerShift`（左 Shift = 160、右 Shift = 161、CapsLock = 20）に保存され、次回起動時も有効です。

同じメニューの「スペース・記号のあとの待ち時間」で、スペースや記号のあとに入れる待ち時間を **0 / 50 / 100 / 150（標準）/ 250 / 400 ms** から選べます。
メモ帳のスペルチェックによる文字化けの対策で、文書が長い・PC が遅いなどで崩れるときは長く、崩れないなら短く（速く）できます。`0` は待ちません。
保存先は同じキーの `BreakDelayMs`（ミリ秒、0〜1000）です。

## 横取りするキー

開始キーの Shift を押している間、次のキーを横取りして小窓に溜めます。

- 英字 a–z、数字キー（上段の 0–9）、すべての記号キー（JIS・US 配列のもの）、Space
- 開始キーの Shift は「Shift 扱い」ではないので、各キーは **Shift なしの文字**になります（`a`、`1`、`-`、`;` など）。
  もう一方の Shift も押すと **Shift 付きの文字**になります（`A`、`!`、`_`、`+` など。配列によって変わります）。
- テンキーと、変換・無変換・Enter・Tab・矢印などの非文字キーは横取りしません。
- 開始キーの Shift で全角記号（例: `！`）を打っていた場合も横取りされるので、そのときは、もう一方の Shift を使ってください。

## 既知の制限

- メモ帳のスペルチェックが有効なとき、記号やスペースのあとで文字が崩れることがあるため、それらの文字のあとに 150 ms（標準）待ってから次を送ります。長い文書や遅い PC では、トレイメニューの「スペース・記号のあとの待ち時間」で伸ばしてください。
- Chromium 系アプリ（Chrome、Electron など）では、未確定の日本語が残っていると、そのまま残ります。先に Enter で確定してください。
