#include <catch2/catch_test_macros.hpp>

#include "Settings.hpp"
#include "TestHelpers.hpp"
#include "UpdateArchiveExtractor.hpp"
#include "Updater.hpp"
#include "UpdaterLaunchOptions.hpp"
#include "UpdaterLiveTestConfig.hpp"
#include "UpdaterTestAccess.hpp"
#include "Utils.hpp"
#include "app_version.hpp"

#include <QAbstractButton>
#include <QMessageBox>
#include <QTimer>

#include <filesystem>
#include <fstream>
#include <memory>

namespace {

std::string utf8_string(const char8_t* value)
{
    return std::string(reinterpret_cast<const char*>(value));
}

void schedule_message_box_button_click(const QString& target_text, bool* saw_button = nullptr)
{
    auto clicker = std::make_shared<std::function<void(int)>>();
    *clicker = [clicker, target_text, saw_button](int attempts_remaining) {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) {
            if (attempts_remaining > 0) {
                QTimer::singleShot(0, [clicker, attempts_remaining]() {
                    (*clicker)(attempts_remaining - 1);
                });
                return;
            }
            if (saw_button) {
                *saw_button = false;
            }
            return;
        }

        for (auto* button : box->buttons()) {
            if (button && button->text() == target_text) {
                if (saw_button) {
                    *saw_button = true;
                }
                button->click();
                return;
            }
        }

        if (saw_button) {
            *saw_button = false;
        }
        if (auto* ok_button = box->button(QMessageBox::Ok)) {
            ok_button->click();
            return;
        }
        const auto buttons = box->buttons();
        if (!buttons.isEmpty() && buttons.first()) {
            buttons.first()->click();
        }
    };

    QTimer::singleShot(0, [clicker]() {
        (*clicker)(10);
    });
}

void schedule_message_box_capture_and_click(const QString& target_text,
                                            QString* captured_text,
                                            QString* captured_informative_text,
                                            bool* saw_button = nullptr)
{
    auto clicker = std::make_shared<std::function<void(int)>>();
    *clicker = [clicker, target_text, captured_text, captured_informative_text, saw_button](int attempts_remaining) {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) {
            if (attempts_remaining > 0) {
                QTimer::singleShot(0, [clicker, attempts_remaining]() {
                    (*clicker)(attempts_remaining - 1);
                });
                return;
            }
            if (saw_button) {
                *saw_button = false;
            }
            return;
        }

        if (captured_text) {
            *captured_text = box->text();
        }
        if (captured_informative_text) {
            *captured_informative_text = box->informativeText();
        }

        for (auto* button : box->buttons()) {
            if (button && button->text() == target_text) {
                if (saw_button) {
                    *saw_button = true;
                }
                button->click();
                return;
            }
        }

        if (saw_button) {
            *saw_button = false;
        }
        if (auto* ok_button = box->button(QMessageBox::Ok)) {
            ok_button->click();
            return;
        }
        const auto buttons = box->buttons();
        if (!buttons.isEmpty() && buttons.first()) {
            buttons.first()->click();
        }
    };

    QTimer::singleShot(0, [clicker]() {
        (*clicker)(10);
    });
}

} // namespace

#ifdef _WIN32
TEST_CASE("Updater live test mode synthesizes a newer update without a feed URL")
{
    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", std::nullopt);
    EnvVarGuard live_test_mode_guard(UpdaterLaunchOptions::kLiveTestModeEnv, std::string("1"));
    EnvVarGuard live_test_url_guard(UpdaterLaunchOptions::kLiveTestUrlEnv,
                                    std::string("https://filesorter.app/downloads/AIFileSorterSetup.zip"));
    EnvVarGuard live_test_sha_guard(UpdaterLaunchOptions::kLiveTestSha256Env,
                                    std::string("AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899"));
    EnvVarGuard live_test_version_guard(UpdaterLaunchOptions::kLiveTestVersionEnv, std::nullopt);
    EnvVarGuard live_test_min_guard(UpdaterLaunchOptions::kLiveTestMinVersionEnv, std::nullopt);

    Settings settings;
    Updater updater(settings);

    REQUIRE(UpdaterTestAccess::is_update_available(updater));
    const auto info = UpdaterTestAccess::current_update_info(updater);
    REQUIRE(info.has_value());
    CHECK(info->current_version == APP_VERSION.to_numeric_string() + ".1");
    CHECK(info->min_version == "0.0.0");
    CHECK(info->download_url == "https://filesorter.app/downloads/AIFileSorterSetup.zip");
    CHECK(info->installer_url == info->download_url);
    CHECK(info->installer_sha256 == "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899");
}
#endif

TEST_CASE("Updater live test mode can read missing values from live-test.ini next to the executable")
{
    TempDir temp_dir;
    const auto exe_dir = temp_dir.path() / "bin";
    std::filesystem::create_directories(exe_dir);

    {
        std::ofstream out(exe_dir / "live-test.ini", std::ios::binary | std::ios::trunc);
        out << "[LiveTest]\n";
        out << "download_url = https://filesorter.app/downloads/from-ini.zip\n";
        out << "sha256 = 11223344556677889900AABBCCDDEEFF11223344556677889900AABBCCDDEEFF\n";
        out << "current_version = 9.9.9\n";
        out << "min_version = 1.0.0\n";
    }

    UpdaterLiveTestConfig config;
    config.enabled = true;

    const auto loaded = load_missing_values_from_live_test_ini(config, exe_dir / "aifilesorter.exe");
    REQUIRE(loaded.has_value());
    CHECK(loaded->filename() == "live-test.ini");
    REQUIRE(config.installer_url.has_value());
    REQUIRE(config.installer_sha256.has_value());
    REQUIRE(config.current_version.has_value());
    REQUIRE(config.min_version.has_value());
    CHECK(*config.installer_url == "https://filesorter.app/downloads/from-ini.zip");
    CHECK(*config.installer_sha256 == "11223344556677889900AABBCCDDEEFF11223344556677889900AABBCCDDEEFF");
    CHECK(*config.current_version == "9.9.9");
    CHECK(*config.min_version == "1.0.0");
}

TEST_CASE("Updater uses the development update feed when development mode is enabled")
{
    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", std::nullopt);
    EnvVarGuard development_update_spec_guard("UPDATE_SPEC_FILE_URL_DEVELOPMENT",
                                              "https://example.com/aifs_version_dev.json");

    Settings settings;
    Updater updater(settings, true);

    const auto selected_url = UpdaterTestAccess::selected_update_spec_file_url(updater);
    REQUIRE(selected_url.has_value());
    CHECK(*selected_url == "https://example.com/aifs_version_dev.json");
}

TEST_CASE("Updater falls back to the standard update feed in development mode when needed")
{
    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", "https://example.com/aifs_version.json");
    EnvVarGuard development_update_spec_guard("UPDATE_SPEC_FILE_URL_DEVELOPMENT", std::nullopt);

    Settings settings;
    Updater updater(settings, true);

    const auto selected_url = UpdaterTestAccess::selected_update_spec_file_url(updater);
    REQUIRE(selected_url.has_value());
    CHECK(*selected_url == "https://example.com/aifs_version.json");
}

TEST_CASE("Updater ignores the development update feed outside development mode")
{
    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", "https://example.com/aifs_version.json");
    EnvVarGuard development_update_spec_guard("UPDATE_SPEC_FILE_URL_DEVELOPMENT",
                                              "https://example.com/aifs_version_dev.json");

    Settings settings;
    Updater updater(settings);

    const auto selected_url = UpdaterTestAccess::selected_update_spec_file_url(updater);
    REQUIRE(selected_url.has_value());
    CHECK(*selected_url == "https://example.com/aifs_version.json");
}

TEST_CASE("Updater live test flags override live-test.ini values")
{
    TempDir temp_dir;
    const auto exe_dir = temp_dir.path() / "bin";
    std::filesystem::create_directories(exe_dir);

    {
        std::ofstream out(exe_dir / "live-test.ini", std::ios::binary | std::ios::trunc);
        out << "[LiveTest]\n";
        out << "download_url = https://filesorter.app/downloads/from-ini.zip\n";
        out << "sha256 = 11223344556677889900AABBCCDDEEFF11223344556677889900AABBCCDDEEFF\n";
    }

    UpdaterLiveTestConfig config;
    config.enabled = true;
    config.installer_url = "https://filesorter.app/downloads/from-flag.zip";

    const auto loaded = load_missing_values_from_live_test_ini(config, exe_dir / "aifilesorter.exe");
    REQUIRE(loaded.has_value());
    REQUIRE(config.installer_url.has_value());
    REQUIRE(config.installer_sha256.has_value());
    CHECK(*config.installer_url == "https://filesorter.app/downloads/from-flag.zip");
    CHECK(*config.installer_sha256 == "11223344556677889900AABBCCDDEEFF11223344556677889900AABBCCDDEEFF");
}

TEST_CASE("Updater error dialog offers manual update fallback without quitting when not requested")
{
    EnvVarGuard platform_guard("QT_QPA_PLATFORM", "offscreen");
    QtAppContext qt_context;

    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", "https://example.com/aifs_version.json");

    Settings settings;
    Updater updater(settings);

    std::string opened_url;
    bool quit_called = false;
    bool saw_manual_button = false;

    UpdaterTestAccess::set_open_download_url_handler(updater, [&](const std::string& url) {
        opened_url = url;
    });
    UpdaterTestAccess::set_quit_handler(updater, [&]() {
        quit_called = true;
    });

    UpdateInfo info;
    info.download_url = "https://filesorter.app/download";

    schedule_message_box_button_click(QStringLiteral("Update manually"), &saw_manual_button);

    const bool result = UpdaterTestAccess::handle_update_error(
        updater,
        info,
        QStringLiteral("Failed to prepare the update installer."),
        nullptr,
        false);

    CHECK(result);
    CHECK(saw_manual_button);
    CHECK(opened_url == info.download_url);
    CHECK_FALSE(quit_called);
}

TEST_CASE("Updater error dialog can request quit after manual fallback")
{
    EnvVarGuard platform_guard("QT_QPA_PLATFORM", "offscreen");
    QtAppContext qt_context;

    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", "https://example.com/aifs_version.json");

    Settings settings;
    Updater updater(settings);

    std::string opened_url;
    bool quit_called = false;

    UpdaterTestAccess::set_open_download_url_handler(updater, [&](const std::string& url) {
        opened_url = url;
    });
    UpdaterTestAccess::set_quit_handler(updater, [&]() {
        quit_called = true;
    });

    UpdateInfo info;
    info.download_url = "https://filesorter.app/download";

    schedule_message_box_button_click(QStringLiteral("Update manually"));

    const bool result = UpdaterTestAccess::handle_update_error(
        updater,
        info,
        QStringLiteral("The installer could not be launched."),
        nullptr,
        true);

    CHECK(result);
    CHECK(opened_url == info.download_url);
    CHECK(quit_called);
}

TEST_CASE("Updater error dialog omits manual fallback when no download URL is available")
{
    EnvVarGuard platform_guard("QT_QPA_PLATFORM", "offscreen");
    QtAppContext qt_context;

    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", "https://example.com/aifs_version.json");

    Settings settings;
    Updater updater(settings);

    std::string opened_url;
    bool quit_called = false;
    bool saw_manual_button = true;

    UpdaterTestAccess::set_open_download_url_handler(updater, [&](const std::string& url) {
        opened_url = url;
    });
    UpdaterTestAccess::set_quit_handler(updater, [&]() {
        quit_called = true;
    });

    UpdateInfo info;

    schedule_message_box_button_click(QStringLiteral("Update manually"), &saw_manual_button);

    const bool result = UpdaterTestAccess::handle_update_error(
        updater,
        info,
        QStringLiteral("No download target is available for this update."),
        nullptr,
        true);

    CHECK_FALSE(result);
    CHECK_FALSE(saw_manual_button);
    CHECK(opened_url.empty());
    CHECK_FALSE(quit_called);
}

TEST_CASE("Updater optional dialog shows changelog items from the update feed")
{
    EnvVarGuard platform_guard("QT_QPA_PLATFORM", "offscreen");
    QtAppContext qt_context;

    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", "https://example.com/aifs_version.json");

    Settings settings;
    Updater updater(settings);

    UpdateInfo info;
    info.current_version = "1.9.0";
    info.download_url = "https://filesorter.app/download";
    info.changelog_items = {
        "Improved Linux backend selection",
        "Added cache maintenance controls"
    };

    QString captured_text;
    QString captured_informative_text;
    bool saw_button = false;
    schedule_message_box_capture_and_click(QStringLiteral("Cancel"),
                                           &captured_text,
                                           &captured_informative_text,
                                           &saw_button);

    UpdaterTestAccess::show_optional_update_dialog(updater, info, nullptr);

    CHECK(saw_button);
    CHECK(captured_text == QStringLiteral("An optional update is available. Would you like to update now?"));
    CHECK(captured_informative_text == QStringLiteral(
        "What's new in version 1.9.0:\n"
        "- Improved Linux backend selection\n"
        "- Added cache maintenance controls"));
}

TEST_CASE("Updater required dialog shows changelog items before forcing quit")
{
    EnvVarGuard platform_guard("QT_QPA_PLATFORM", "offscreen");
    QtAppContext qt_context;

    TempDir config_dir;
    EnvVarGuard config_guard("AI_FILE_SORTER_CONFIG_DIR", config_dir.path().string());
    EnvVarGuard update_spec_guard("UPDATE_SPEC_FILE_URL", "https://example.com/aifs_version.json");

    Settings settings;
    Updater updater(settings);

    UpdateInfo info;
    info.current_version = "2.0.0";
    info.download_url = "https://filesorter.app/download";
    info.changelog_items = {
        "Requires the latest model metadata",
        "Improves updater error recovery"
    };

    bool quit_called = false;
    UpdaterTestAccess::set_quit_handler(updater, [&]() {
        quit_called = true;
    });

    QString captured_informative_text;
    bool saw_button = false;
    schedule_message_box_capture_and_click(QStringLiteral("Quit"),
                                           nullptr,
                                           &captured_informative_text,
                                           &saw_button);

    UpdaterTestAccess::show_required_update_dialog(updater, info, nullptr);

    CHECK(saw_button);
    CHECK(quit_called);
    CHECK(captured_informative_text == QStringLiteral(
        "What's new in version 2.0.0:\n"
        "- Requires the latest model metadata\n"
        "- Improves updater error recovery"));
}

TEST_CASE("UpdateArchiveExtractor extracts installer from Unicode path") {
    TempDir temp;
    const auto unicode_dir = temp.path() / Utils::utf8_to_path(utf8_string(u8"업데이트_폴더_🚀"));
    std::filesystem::create_directories(unicode_dir);
    const auto zip_path = unicode_dir / Utils::utf8_to_path(utf8_string(u8"패키지_update.zip"));

    static const unsigned char kValidZipWithInstaller[] = {
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xc8, 0xb0, 0x26, 0x5d, 0x5d, 0x9b,
        0xb0, 0x8f, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x73, 0x65,
        0x74, 0x75, 0x70, 0x2e, 0x65, 0x78, 0x65, 0xf3, 0x8d, 0x02, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14,
        0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xc8, 0xb0, 0x26, 0x5d, 0x5d, 0x9b, 0xb0, 0x8f, 0x04,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x65, 0x74, 0x75, 0x70, 0x2e, 0x65,
        0x78, 0x65, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x37, 0x00,
        0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    {
        std::ofstream file(zip_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(kValidZipWithInstaller), sizeof(kValidZipWithInstaller));
    }

    const auto dest_dir = temp.path() / "extracted";
    const auto result = UpdateArchiveExtractor::extract_installer(zip_path, dest_dir);

    CHECK(result.ok());
    CHECK(!result.installer_path.empty());
    CHECK(std::filesystem::exists(result.installer_path));
}

