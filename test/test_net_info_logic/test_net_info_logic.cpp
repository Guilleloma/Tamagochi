// =============================================================================
// test_net_info_logic.cpp — tests unitarios para NetInfoLogic (estado de red,
// SSID/hostname truncados, IP, RSSI → barras) de la página de red de Stats.
//
// Framework: Unity (pio test -e native).
// NetInfoLogic es header-only y puro (sin Arduino.h ni WiFi.h) — se ejercita
// directamente. Los buffers de salida llevan un "canario" detrás para
// detectar escrituras fuera de rango.
// =============================================================================

#include <unity.h>
#include <string.h>
#include "NetInfoLogic.h"

using NetInfoLogic::NetStatus;

void setUp()    {}
void tearDown() {}

// Ancho útil de la página de red en el TFT: 240 - 2 * 16 px de margen seguro,
// a textSize 2 (12 px/char) → 17 caracteres. Mismo cálculo que el renderer.
static const size_t kValueChars = NetInfoLogic::maxCharsForWidth(240 - 2 * 16, 2);

// SSID de longitud máxima 802.11 (32 bytes).
static const char* kSsid32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";

// Byte de relleno para detectar desbordamientos tras el '\0' esperado.
static const char kCanary = '#';

static bool isPrintableAscii(const char* s) {
    for (; *s != '\0'; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// =============================================================================
// statusFor / statusLabel
// =============================================================================

void should_report_not_configured_when_ssid_is_empty_whatever_the_state() {
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Disabled,     false) == NetStatus::NotConfigured);
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Connecting,   false) == NetStatus::NotConfigured);
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Connected,    false) == NetStatus::NotConfigured);
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Disconnected, false) == NetStatus::NotConfigured);
}

void should_report_connected_when_wifi_manager_is_connected() {
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Connected, true) == NetStatus::Connected);
}

void should_report_connecting_while_waiting_for_ip() {
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Connecting, true) == NetStatus::Connecting);
}

void should_report_offline_when_connection_was_lost() {
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Disconnected, true) == NetStatus::Offline);
}

void should_report_offline_when_configured_but_wifi_not_started_yet() {
    // Disabled con SSID configurado solo ocurre antes de WifiManager::begin().
    TEST_ASSERT_TRUE(NetInfoLogic::statusFor(WifiState::Disabled, true) == NetStatus::Offline);
}

void should_return_ascii_spanish_label_for_each_status() {
    TEST_ASSERT_EQUAL_STRING("Conectado",           NetInfoLogic::statusLabel(NetStatus::Connected));
    TEST_ASSERT_EQUAL_STRING("Conectando...",       NetInfoLogic::statusLabel(NetStatus::Connecting));
    TEST_ASSERT_EQUAL_STRING("Sin conexion",        NetInfoLogic::statusLabel(NetStatus::Offline));
    TEST_ASSERT_EQUAL_STRING("WiFi no configurada", NetInfoLogic::statusLabel(NetStatus::NotConfigured));
}

// =============================================================================
// maxCharsForWidth
// =============================================================================

void should_compute_chars_that_fit_in_the_stats_page_width() {
    TEST_ASSERT_EQUAL_UINT32(17, NetInfoLogic::maxCharsForWidth(208, 2));
    TEST_ASSERT_EQUAL_UINT32(34, NetInfoLogic::maxCharsForWidth(208, 1));
}

void should_return_zero_chars_for_degenerate_width_or_text_size() {
    TEST_ASSERT_EQUAL_UINT32(0, NetInfoLogic::maxCharsForWidth(208, 0));
    TEST_ASSERT_EQUAL_UINT32(0, NetInfoLogic::maxCharsForWidth(0, 2));
    TEST_ASSERT_EQUAL_UINT32(0, NetInfoLogic::maxCharsForWidth(-12, 2));
}

// =============================================================================
// formatSsid / fitText
// =============================================================================

void should_copy_short_ssid_unchanged() {
    char out[32];
    const size_t n = NetInfoLogic::formatSsid("CasaGuille", kValueChars, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("CasaGuille", out);
    TEST_ASSERT_EQUAL_UINT32(10, n);
}

void should_show_placeholder_when_ssid_is_empty() {
    char out[32];
    NetInfoLogic::formatSsid("", kValueChars, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-", out);
}

void should_show_placeholder_when_ssid_is_null() {
    char out[32];
    NetInfoLogic::formatSsid(nullptr, kValueChars, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-", out);
}

void should_keep_ssid_that_fits_exactly_without_ellipsis() {
    char out[32];
    const char* ssid17 = "ABCDEFGHIJKLMNOPQ";  // 17 chars == kValueChars
    NetInfoLogic::formatSsid(ssid17, kValueChars, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(ssid17, out);
}

void should_truncate_32_char_ssid_with_ellipsis_to_the_available_width() {
    char out[40];
    const size_t n = NetInfoLogic::formatSsid(kSsid32, kValueChars, out, sizeof(out));
    // 17 caracteres: 14 del SSID + "..."
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMN...", out);
    TEST_ASSERT_EQUAL_UINT32(kValueChars, n);
    TEST_ASSERT_EQUAL_UINT32(kValueChars, strlen(out));
}

void should_keep_32_char_ssid_when_there_is_room_for_it() {
    char out[NetInfoLogic::kMaxSsidLen + 1];
    NetInfoLogic::formatSsid(kSsid32, NetInfoLogic::kMaxSsidLen, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(kSsid32, out);
}

void should_replace_control_and_utf8_bytes_with_a_single_question_mark() {
    char out[32];
    // "Café\tWiFi": 'é' (0xC3 0xA9) es un solo '?'; el tabulador, otro '?'.
    NetInfoLogic::formatSsid("Caf\xC3\xA9\tWiFi", kValueChars, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Caf??WiFi", out);
    TEST_ASSERT_TRUE(isPrintableAscii(out));
}

void should_count_utf8_characters_not_bytes_when_truncating() {
    char out[32];
    // 6 caracteres "ñ" (12 bytes) caben en 6 posiciones → sin elipsis.
    NetInfoLogic::formatSsid("\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1", 6, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("??????", out);
}

void should_never_write_past_a_small_output_buffer() {
    char out[8 + 4];
    memset(out, kCanary, sizeof(out));
    // maxChars generoso, pero el buffer solo admite 7 chars + '\0'.
    const size_t n = NetInfoLogic::fitText(kSsid32, kValueChars, out, 8);
    TEST_ASSERT_EQUAL_STRING("ABCD...", out);
    TEST_ASSERT_EQUAL_UINT32(7, n);
    for (size_t i = 8; i < sizeof(out); ++i) TEST_ASSERT_EQUAL_CHAR(kCanary, out[i]);
}

void should_hard_cut_without_ellipsis_when_there_is_no_room_for_it() {
    char out[8];
    NetInfoLogic::fitText("ABCDEFG", 3, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("ABC", out);
}

void should_do_nothing_when_output_buffer_is_null_or_empty() {
    char out[4] = {kCanary, kCanary, kCanary, kCanary};
    TEST_ASSERT_EQUAL_UINT32(0, NetInfoLogic::fitText("ABC", 3, nullptr, 4));
    TEST_ASSERT_EQUAL_UINT32(0, NetInfoLogic::fitText("ABC", 3, out, 0));
    TEST_ASSERT_EQUAL_CHAR(kCanary, out[0]);
}

// =============================================================================
// formatHost
// =============================================================================

void should_append_local_suffix_to_mdns_hostname() {
    char out[32];
    NetInfoLogic::formatHost("tamagotchi", kValueChars, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("tamagotchi.local", out);
}

void should_show_placeholder_when_hostname_is_empty() {
    char out[32];
    NetInfoLogic::formatHost("", kValueChars, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-", out);
}

void should_truncate_long_hostname_with_ellipsis() {
    char out[32];
    NetInfoLogic::formatHost("tamagotchi-salon", kValueChars, out, sizeof(out));
    // "tamagotchi-salon.local" = 22 chars → 14 + "..."
    TEST_ASSERT_EQUAL_STRING("tamagotchi-sal...", out);
}

// =============================================================================
// formatIp
// =============================================================================

void should_format_ip_as_dotted_quad() {
    const uint8_t ip[4] = {192, 168, 1, 42};
    char out[NetInfoLogic::kIpTextSize];
    NetInfoLogic::formatIp(ip, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("192.168.1.42", out);
}

void should_show_placeholder_for_unassigned_ip_0_0_0_0() {
    const uint8_t ip[4] = {0, 0, 0, 0};
    char out[NetInfoLogic::kIpTextSize];
    TEST_ASSERT_FALSE(NetInfoLogic::isIpAssigned(ip));
    NetInfoLogic::formatIp(ip, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-", out);
}

void should_treat_null_ip_as_unassigned() {
    char out[NetInfoLogic::kIpTextSize];
    TEST_ASSERT_FALSE(NetInfoLogic::isIpAssigned(nullptr));
    NetInfoLogic::formatIp(nullptr, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-", out);
}

void should_fit_longest_ip_in_recommended_buffer() {
    const uint8_t ip[4] = {255, 255, 255, 255};
    char out[NetInfoLogic::kIpTextSize];
    NetInfoLogic::formatIp(ip, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("255.255.255.255", out);
}

void should_truncate_ip_without_overflow_on_small_buffer() {
    const uint8_t ip[4] = {192, 168, 100, 200};
    char out[6 + 4];
    memset(out, kCanary, sizeof(out));
    NetInfoLogic::formatIp(ip, out, 6);
    TEST_ASSERT_EQUAL_STRING("192.1", out);
    for (size_t i = 6; i < sizeof(out); ++i) TEST_ASSERT_EQUAL_CHAR(kCanary, out[i]);
}

// =============================================================================
// rssiToBars / formatRssi
// =============================================================================

void should_show_four_bars_for_excellent_signal() {
    TEST_ASSERT_EQUAL_UINT8(4, NetInfoLogic::rssiToBars(-30));
    TEST_ASSERT_EQUAL_UINT8(4, NetInfoLogic::rssiToBars(-55));
}

void should_show_four_bars_for_strongest_valid_rssi_minus_1() {
    TEST_ASSERT_EQUAL_UINT8(4, NetInfoLogic::rssiToBars(-1));
}

void should_step_down_bars_exactly_at_each_threshold() {
    TEST_ASSERT_EQUAL_UINT8(3, NetInfoLogic::rssiToBars(-56));
    TEST_ASSERT_EQUAL_UINT8(3, NetInfoLogic::rssiToBars(-67));
    TEST_ASSERT_EQUAL_UINT8(2, NetInfoLogic::rssiToBars(-68));
    TEST_ASSERT_EQUAL_UINT8(2, NetInfoLogic::rssiToBars(-78));
    TEST_ASSERT_EQUAL_UINT8(1, NetInfoLogic::rssiToBars(-79));
    TEST_ASSERT_EQUAL_UINT8(1, NetInfoLogic::rssiToBars(-89));
    TEST_ASSERT_EQUAL_UINT8(0, NetInfoLogic::rssiToBars(-90));
}

void should_show_zero_bars_for_weakest_int8_rssi() {
    TEST_ASSERT_EQUAL_UINT8(0, NetInfoLogic::rssiToBars(INT8_MIN));  // -128 dBm
}

void should_show_zero_bars_when_rssi_is_zero_or_positive() {
    // WifiManager::rssi() devuelve 0 sin conexión; >0 no es un RSSI real.
    TEST_ASSERT_EQUAL_UINT8(0, NetInfoLogic::rssiToBars(0));
    TEST_ASSERT_EQUAL_UINT8(0, NetInfoLogic::rssiToBars(INT8_MAX));
}

void should_format_rssi_in_dbm() {
    char out[NetInfoLogic::kRssiTextSize];
    NetInfoLogic::formatRssi(-62, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-62 dBm", out);
}

void should_fit_weakest_rssi_text_in_recommended_buffer() {
    char out[NetInfoLogic::kRssiTextSize];
    NetInfoLogic::formatRssi(INT8_MIN, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-128 dBm", out);
}

void should_show_placeholder_when_rssi_is_not_available() {
    char out[NetInfoLogic::kRssiTextSize];
    NetInfoLogic::formatRssi(0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-", out);
}

// =============================================================================
// main — punto de entrada Unity
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(should_report_not_configured_when_ssid_is_empty_whatever_the_state);
    RUN_TEST(should_report_connected_when_wifi_manager_is_connected);
    RUN_TEST(should_report_connecting_while_waiting_for_ip);
    RUN_TEST(should_report_offline_when_connection_was_lost);
    RUN_TEST(should_report_offline_when_configured_but_wifi_not_started_yet);
    RUN_TEST(should_return_ascii_spanish_label_for_each_status);

    RUN_TEST(should_compute_chars_that_fit_in_the_stats_page_width);
    RUN_TEST(should_return_zero_chars_for_degenerate_width_or_text_size);

    RUN_TEST(should_copy_short_ssid_unchanged);
    RUN_TEST(should_show_placeholder_when_ssid_is_empty);
    RUN_TEST(should_show_placeholder_when_ssid_is_null);
    RUN_TEST(should_keep_ssid_that_fits_exactly_without_ellipsis);
    RUN_TEST(should_truncate_32_char_ssid_with_ellipsis_to_the_available_width);
    RUN_TEST(should_keep_32_char_ssid_when_there_is_room_for_it);
    RUN_TEST(should_replace_control_and_utf8_bytes_with_a_single_question_mark);
    RUN_TEST(should_count_utf8_characters_not_bytes_when_truncating);
    RUN_TEST(should_never_write_past_a_small_output_buffer);
    RUN_TEST(should_hard_cut_without_ellipsis_when_there_is_no_room_for_it);
    RUN_TEST(should_do_nothing_when_output_buffer_is_null_or_empty);

    RUN_TEST(should_append_local_suffix_to_mdns_hostname);
    RUN_TEST(should_show_placeholder_when_hostname_is_empty);
    RUN_TEST(should_truncate_long_hostname_with_ellipsis);

    RUN_TEST(should_format_ip_as_dotted_quad);
    RUN_TEST(should_show_placeholder_for_unassigned_ip_0_0_0_0);
    RUN_TEST(should_treat_null_ip_as_unassigned);
    RUN_TEST(should_fit_longest_ip_in_recommended_buffer);
    RUN_TEST(should_truncate_ip_without_overflow_on_small_buffer);

    RUN_TEST(should_show_four_bars_for_excellent_signal);
    RUN_TEST(should_show_four_bars_for_strongest_valid_rssi_minus_1);
    RUN_TEST(should_step_down_bars_exactly_at_each_threshold);
    RUN_TEST(should_show_zero_bars_for_weakest_int8_rssi);
    RUN_TEST(should_show_zero_bars_when_rssi_is_zero_or_positive);
    RUN_TEST(should_format_rssi_in_dbm);
    RUN_TEST(should_fit_weakest_rssi_text_in_recommended_buffer);
    RUN_TEST(should_show_placeholder_when_rssi_is_not_available);

    return UNITY_END();
}
