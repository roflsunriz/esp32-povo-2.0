# povo 1.70 メール認証・認証保持の静的解析

2026-09-05。解析根は `C:/Users/UserName/AppData/Local/Temp/povo-jadx-1.70/sources`。以下のパスと行はこの根に相対。コード転載ではなく挙動の要約。実通信で確認した契約ではない。固定秘密・ユーザー情報・認証値を含めない。

## メールログイン

1. `com/circles/selfcare/p592v2/login/C11365t0.java:696-702` のメール導線は `{email: 入力値}` を login/action に送る（device_id/phone/isd は null）。`p668g4/C16205a.java` がJSON名を定義。
2. `C11365t0.java:486-505`: captchaEnabled が真なら `X-Captcha` を付け v4、偽なら v3。値の構成は `{token: CAPTCHA結果, version: "v2", action: "LOGIN"}` JSON の UTF-8 を Base64 NO_WRAP。`p668g4/C16206b.java:16-25`。
3. `LoginActions.java` は `{actions: [{action, metadata: {message}}], message}` を返す。単一のメールOTPと決め打ちせず、actions の順序と種類を扱う。
4. `C11365t0.java:723-731` の通常メールログインOTP送信は device_id、email、otp_duration=15、auth_mode=`ENHANCED_EMAIL_OTP`、request_type=`LOGIN_EMAIL_OTP`。`p649f4/C16101a.java` コンストラクタの mask617 により activity/isd_code/phone_no/channel は null、旧API用path は auth。channel=email を無条件に追加しない。
5. OTP返答は `Token.C8685a`。JSON名は device_id/auth_id/otp_code/google_auth (`Token.java:75-88`)。`C11367u.java:28` が返却auth_idを既存の端末ID付き認証オブジェクトへコピーし、`C11365t0.java:939-955` が入力otp_codeをセット。
6. 全actions完了後 `C11365t0.java:553-594` が EMAIL_OTP のオブジェクトを email_auth に入れ、device とともにログインする。初期構築mask95により override は null（trueを常に送らない）。device は device_id/device_type=`Mobile`/app_type=`ecosystem`、他はnull（`DeviceMeta.java`末尾の既定値）。Gsonのnull出力設定は実通信で照合する。
7. token応答は auth_token/pin_token/first_login (`Token.java:27-38`)。`C11301X.java:31-38` は pin_token があればPINチャレンジへ遷移。成功扱いしてはいけない。PIN検証 API は users/pin/validate、X-Captcha と x-pin-token (`InterfaceC17302c.java:74-77`)。
8. auth_token が得られると `C11308a0.java:29-54` が永続化を呼び、ユーザープロファイルを取得する。

## APIパスとフラグ

`p724j4/InterfaceC17302c.java`:

| 操作 | メソッド/相対パス | VLI-Version | VLI-Prefix | 行 |
|---|---|---|---|---|
| ログイン方法・旧 | POST users/login/action | v3 | /api/v3/user-service | 55-59 |
| ログイン方法・captcha | POST users/login/action | v4 | /api/v3/user-service | 153-157 |
| OTP・新 | POST otp | v4 | /api/v3/user-service | 107-111 |
| OTP・旧 | POST users/{path}/otp/send | v3 | /api/v3/user-service | 67-71 |
| 認証・新 | POST users/auth | v5/public | /user-service | 147-151 |
| 認証・旧 | POST users/login | v3 | /api/v3/user-service | 49-53 |
| 更新 | GET users/token | v4 | /api/v3/user-service | 130-134 |
| プロファイル・新 | GET users?include_telco=true | v4 | /api/v3/user-service | 61-65 |
| プロファイル・旧 | GET users/{userId}/profile?include_telco=true | v3 | /api/v3/user-service | 113-117 |

OTPと認証の新旧は loginV4Enabled で切り替える (`C11361s.java:25-27`, `C11365t0.java:592-594`)。login/actionの選択条件は別の captchaEnabled。

`p724j4/C17305f.java:61-96` はVLIローカライズ時 `/jp/ja/mobile` を挿入し、バージョン、prefixを前置する。VLIヘッダーは内部用で送信前に除去される。例: `/user-service/v5/public/jp/ja/mobile/users/auth`、`/api/v3/user-service/v4/jp/ja/mobile/otp`。後段のUniversal/LegacyUrlGeneratorにさらにURL変換があるため、最終URLはキャプチャを優先する。

## ヘッダー、クエリ、保持

- `C17305f.java:114-116`: X-App-Version。
- `p941u5/C21650i.java:154-160`: User-Agent、X-USER-ID、X-AUTH、X-App-Platform=Android。条件付きX-Enable-Auth=true。
- `C21650i.java:79-85`: JSON POSTにuuidがなければ追加。GETは保存済みservice_instance_numberがある場合sinと条件付きuuidを追加 (:101-110)。`uuid` の生成仕様は未追跡。
- `p1037z5/C22386b.java:201-225,302-327`: session_key を読み書き。secure migrationフラグにより保存先を切り替える。独自クライアントでもトークンをログやGitへ出さず、更新後の保持を原子的に行う。
- `p233L4/C2103h.java:57-69`: JWTの `expiry_time` をepoch secondsとして読む。標準expだけを見ない。
- `C2103h.java:113-133`: GET users/tokenで更新後、session_keyを置換しX-AUTHへ設定。refresh_tokenをPOSTするOAuth型の契約ではない。
- `C2103h.java:143-165`: user-serviceの一部は事前更新から除外。更新の再帰を避ける設計が必要。
- `p233L4/C2099d.java:54-83`: 401から更新を試し、同一JWTを返すケースは失敗、更新403はログアウト処理。並行更新は排他。実装では無制限再試行を避ける。

## ホストと残量取得

- `p233L4/C2097b.java:34-42` はホストを `p193J4/C1787b.f4582v`、portを同クラスf4538Pから採用。初期port=6443だが `C1787b.java:248` で復号設定値に更新するため6443を実通信ポートと断定しない。固定secretは抽出していない。最終authorityは実測でapp.povo.jp:443と確認（README参照）。
- `p213K4/InterfaceC1958a.java:63-90` の v4/localize GET群: account/addon/extra/all/get、account/addon/general/all/get、account/addon/topup/all/get（QueryMap）、account/usage/plan/get。戻り値は生ResponseBodyなのでここだけではJSONスキーマは不明。アクティブトッピング/残量の採用APIとレスポンス構造はキャプチャまたは別解析が必要。

## 検証上の注意

この文書は静的解析の根拠を記録する。実測は同ディレクトリのREADMEに分けて記録する。機能フラグ、新旧URL変換順序、応答envelope、CAPTCHA/PIN要否、device_id生成、uuid生成、ベースauthorityは実測または追加追跡で確定する。OTP制限時間の単位はフィールド名と15のみ確認しており、分と断定しない。

## 追加追跡（端末識別、CAPTCHA、最終URL、トッピング）

### 識別子

- `com/circles/selfcare/p592v2/login/C11349o.java:48-49` → `p025B3/C0216c.java:16-33`。device_id は SharedPreferences `permanent_device_uuid_preference` の `device_uuid` を再利用。初回、Android IDがあれば `new UUID(androidId.hashCode(), currentTimeMillis)` の文字列表現に `com.kddi.kdla.jp` を連結。なければ後述のランダムlong文字列に同suffix。Android IDそのものではなく、再インストールや保存領域の状態で変化しうる。
- `p025B3/C0224k.java:13-14`: リクエストuuidは randomUUID().getMostSignificantBits() の符号付き64bit整数の10進文字列。通常のハイフン入りUUIDではない。端末IDとは別に毎要求生成。
- `p724j4/C17300a.java:34-36`: X-REQUEST-ID は別途 randomUUID().toString()。
- `p631e4/C15957a.java:28`: X-Deviceid ヘッダーに上記device_id。
- `p612d4/C15785c.java:23-25`: X-Timezone はTimeZone.getDefault().getID()。

### CAPTCHAはアプリSDK経路

- `LoginFlags.java:31-35` の captcha_enabled / login_v4 は countries=16、defaultVal=true。参照定数は `com/newrelic/agent/android/api/p602v1/Defaults.java:6`。設定に上書きされていない状態の既定であり実機状態とは区別。
- `EmailLogInFragment.java:234-241` → RecaptchaBridge → login/action。
- `p068D4/C0814j.java` の m624j は `Recaptcha.fetchTaskClient(application, configuredKey)` を使用。`p068D4/C0808d.java:30` は `executeTask(RecaptchaAction.custom("LOGIN"), 10000L)` を実行する。初期化/実行エラーはnullトークンでコールバックされる。
- これは任意ブラウザで開けるWebチャレンジURLを返す設計ではない。今回のアプリ導線からWeb用サイトキー、ホストドメイン、Webトークン互換性は確認できない。ESP32からの独立認証にWeb CAPTCHAを実装する場合、対応する正規Web経路の別調査が必要。SDKトークンとWebトークンを同等と仮定しない。

### URL変換順序の確定

- `C8927X1.java:18`（UserService）と `C8943a2.java:18`（AccountService）はRetrofit baseを共有。`C8816B2.java:24` はapi_clientを使用。`C9000l2.java:27-31` はhttps、設定host、設定port、root pathでbase URLを構築。
- `C9068z0.java:36-46` に共通interceptor、`C8809A0.java:25` が後段UniversalUrlGeneratorを追加。従って順序はVersionLocale → ABTesting → DeviceInfo → TimeZone → Login応答処理 → Mock除去 → （nonCircles条件の処理）→ その他共通処理 → TokenRefreshBlocking → UniversalUrlGenerator → logging → ProactiveSessionManager。
- 今回UserServiceはLegacyUrlGenerator経路ではない。`C21650i.java:141-152` は v4/v1 かつ `api/v3/user-service` を含まないURLだけ /en→端末言語、/sg→/jp を行い、v1/discover以外で/apiを除去する。
- よってユーザーサービスのOTPは `/api/v3/user-service/v4/jp/ja/mobile/otp`、login/actionは同prefix/versionに `/users/login/action`、authは `/user-service/v5/public/jp/ja/mobile/users/auth`、更新は `/api/v3/user-service/v4/jp/ja/mobile/users/token`。これらのパスはUniversal変換で変わらない。
- accountのVLI v4は `/v4/jp/ja/mobile/account/...`。sin/uuid追加は前項参照。ホストとportの実測結果はREADMEを参照。

### トッピング取得と期限

- `p811o5/C20127A1.java:586-599` の topup/all/get は成功かつbodyありの応答を `com/circles/selfcare/util/C10953u0.java:19-22` で生JSON配列として読み、`p744k3/C17418c.java` に渡す。少なくともこの導線はトップレベル配列を期待する。
- `C17418c.java:42-60` のtopup項目キー: id、value、unit、price、payment、type、date、order_id、recurrent、app、auto、boost_image_url、description_short。:61-126に free.list[].id、combo[].type/value/unit、unlimited、disabled、subtitle、title、charged_upfront。dateはUTCのISO文字列として解析されるが、モデル上はdateNewであり「有効期限」とは確認できない。トップアップカタログのdateを有効期限に流用しない。
- 有効期限を明示する契約は `account/plan/details/get` (`InterfaceC1958a.java:103-106`)。`C20127A1.java:1082` → `p811o5/C20185b.java:52-59` → `addons_subscribed.general[]` → `p744k3/C17416a.java:408-484`。同モデルをgeneral/all/getにも使用。
- general項目には expiry_date、start_date、future、current、pending、kb、list（子項目）を含む。expiry_dateを `GeneralAddonModel.f24190p` (=expireDate、モデルtoStringで確認)へ、start_dateを別項目へ保存。dateは別項目のまま保持する。期限は `expiry_date` を採用する根拠があるが、UIの選別・current判定の最終規則までは未確定。
- `C0214a.java:169-205` はUTC Calendarで `yyyy-MM-ddTHH:mm:ss.SSSZ` 相当（T/Zは書式中でリテラル）の日時を解析し、日付のみのfallbackあり。独自実装はISO UTCとして読むのが妥当。
- `C20185b.java:35-59` のplan/detailsには basic_plan.data/sms/calls（value/unit）、billing_cycle.start_date/end_date/current_date、addons_subscribed.extra.data/sms/calls、general配列、bonus等。これは契約量/加入内容モデルであり、remaining残量の計算に無条件に使わない。
- usage/plan/getは `C20127A1.java:993-1053` で複数応答をまとめるC20155Mへ渡すが、既存jadxツリーにC20155M.javaがない（rg --files検索）。この静的解析出力だけではusage応答モデルを追跡完了できない。APK再decompileまたはキャプチャを優先。

## 現行Quiltプラン詳細（2026-09-05追加）

- plan-usage deeplinkの画面は `LegacyDashboardActivity.java:1169-1172` のPlanUsageFragment。`PlanUsageFragment.java:249,292` は `user-plan-details-v2` をQuiltViewModelへ渡す。
- `QuiltViewModel.java:44-49` → `QuiltDelegateImpl.java:233-235` mo50h → :87-90 mo47b → :95 mo48c。ヘッダー/paramsは空、force network=true。:132 が NetworkQuiltRepo `p940u4/C21638w.java:29` を呼ぶ。
- `InterfaceC21639x.java:34-37` は GET quilt/page/{page}、VLI-Version=v1、VLI-Prefix=/api、Quilt_Localize=true。Retrofit baseは `C9045u2.java:18`。最終パス `/api/v1/quilt/page/user-plan-details-v2`、Accept-Language=ja-JP。ローカライズされた /jp/ja/mobile はこのAPIには付かない。
- `WidgetComponentAdapter.java:110` は type=povo-tile-plan-detail を `p995x4/C22160k` に、povo-tile-countdown-notification を `C22157h` に割り当てる。
- `C22160k.java:26-37,146-153`: data.name/remaining/expiry は各 {title,label,value}、valueはString。`p795n4/C19905g.java:101-113` が expiry.title/value をそのままTextViewへ表示する。日付パーサーも有効期限によるfilterもこのadapterにはなく、サーバーが選択/整形したcomponentを表示する構造。プラン名や残量も同様。expiry.valueはローカライズ文字列として扱い、ISO timestampの存在を仮定しない。
- 別component `C22157h.java:161-175` の data.countdown.valid_until はLong。`p795n4/C19917s.java:92-94` が `(valid_until - Instant.now().getEpochSecond()) * 1000` をCountDownTimerへ渡すため、こちらはepoch秒と確定。ただし任意のcountdownバナーがデータトッピング期限とは限らない。型と対象プランの関係をキャプチャで確認する。
- API応答モデル `QuiltResponseV2.java:23-29` は widgets/pageWidgets 配列を持つ。native側で最新期限を選ぶ汎用ロジックの根拠は得られず、component順と表示を尊重している。
