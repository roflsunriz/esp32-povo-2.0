#pragma once
namespace povo { namespace text {
constexpr const char* title = "povo トッピング";
constexpr const char* unknown = "不明";
constexpr const char* sources[] = {"不明", "手動", "推定", "公式取得"};
constexpr const char* states[] = {"状態不明", "待機中", "適用中", "再試行中", "要ログイン", "要確認", "適用完了", "コード期限切れ"};
constexpr const char* pending = "更新確認待ち";
constexpr const char* remaining = "残り %llu日 %llu時間";
constexpr const char* minutes = "%llu時間 %llu分";
constexpr const char* expiry = "期限 ";
constexpr const char* codeDeadline = "入力期限 ";
constexpr const char* renewal = "自動更新 %s  回数 %lu/%lu";
constexpr const char* sync = "最終同期 %llu分前";
constexpr const char* stale = "同期停止・古い情報";
constexpr const char* noStatus = "状態を待っています";
constexpr const char* configuring = "設定不足: device-config.h";
constexpr const char* wifi = "Wi-Fi接続待ち";
constexpr const char* clock = "時刻同期待ち・NTP確認";
constexpr const char* connection = "通信失敗・接続と証明書確認";
constexpr const char* unauthorized = "認証失敗・読取トークン確認";
constexpr const char* unavailable = "中継未受信・アプリを確認";
constexpr const char* invalid = "応答異常・中継設定を確認";
} }
