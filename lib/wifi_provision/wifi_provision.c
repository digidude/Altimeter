#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "dns_server.h"
#include "factory_reset.h"
#include "wifi_provision.h"

// Generated from provision.html at CMake configure time — see the
// comment in CMakeLists.txt for why. Edit provision.html, not this.
#include "provision_html_generated.h"

static const char *TAG = "wifi_provision";

#define NVS_NAMESPACE "wifi_cfg"
#define MAX_BOOT_RETRIES 5      // attempts on a fresh boot before giving up and re-provisioning
#define BACKOFF_CAP_MS 60000    // steady-state reconnect backoff ceiling

// Nearby-network picker (the /scan endpoint) — kept small since it has
// to fit in one heap allocation sized for the worst case.
#define MAX_SCAN_RESULTS 16
#define SSID_MAX_BYTES 32
#define JSON_MAX_ESCAPED_SSID (SSID_MAX_BYTES * 6) // every byte could become \u00XX

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_count = 0;
static bool s_stable = false; // true once we've gotten an IP at least once this boot

// WIFI_EVENT_STA_START fires from two different esp_wifi_start() call
// sites: try_connect_sta() (a real boot-time connection attempt with
// saved credentials) and start_captive_portal() (APSTA bring-up with a
// deliberately blank STA config, only so /scan has an interface to
// scan with). The auto-connect-on-START below exists for the former;
// gated on this flag so the latter doesn't also try to connect with a
// blank SSID, fail, and burn through the bounded retry counter into a
// spurious CONNECT_FAILED notification right as provisioning starts.
static bool s_boot_connect_pending = false;

static esp_timer_handle_t s_reconnect_timer;
static httpd_handle_t s_httpd = NULL;
static wifi_provision_status_cb_t s_status_cb = NULL;

static void notify(wifi_provision_event_t event)
{
    if (s_status_cb) {
        s_status_cb(event);
    }
}

// ---------------------------------------------------------------------
// NVS credential storage
// ---------------------------------------------------------------------

static esp_err_t load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err; // most likely ESP_ERR_NVS_NOT_FOUND on a fresh device
    }

    size_t ssid_cap = ssid_len;
    err = nvs_get_str(h, "ssid", ssid, &ssid_cap);
    if (err == ESP_OK) {
        size_t pass_cap = pass_len;
        esp_err_t perr = nvs_get_str(h, "pass", pass, &pass_cap);
        if (perr == ESP_ERR_NVS_NOT_FOUND) {
            pass[0] = '\0'; // open network, legitimately no password saved
        } else if (perr != ESP_OK) {
            err = perr;
        }
    }

    nvs_close(h);
    return err;
}

static esp_err_t save_credentials(const char *ssid, const char *pass, const char *tz, const char *host)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, "tz", (tz && tz[0]) ? tz : "UTC");
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, "host", (host && host[0]) ? host : "altimeter");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t wifi_provision_erase_credentials(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// Static storage, not a caller-supplied buffer: the returned pointer
// needs to stay valid for as long as whoever calls this holds onto it
// (e.g. time_sync_config_t.timezone just stores the pointer, it doesn't
// copy the string), so this can't point into a stack buffer.
static char s_timezone[48] = "UTC";

const char *wifi_provision_get_timezone(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        char tz[sizeof(s_timezone)];
        size_t len = sizeof(tz);
        if (nvs_get_str(h, "tz", tz, &len) == ESP_OK && tz[0]) {
            strlcpy(s_timezone, tz, sizeof(s_timezone));
        }
        nvs_close(h);
    }
    return s_timezone;
}

// Same static-storage rationale as s_timezone above.
static char s_hostname[33] = "altimeter";

const char *wifi_provision_get_hostname(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        char host[sizeof(s_hostname)];
        size_t len = sizeof(host);
        if (nvs_get_str(h, "host", host, &len) == ESP_OK && host[0]) {
            strlcpy(s_hostname, host, sizeof(s_hostname));
        }
        nvs_close(h);
    }
    return s_hostname;
}

// ---------------------------------------------------------------------
// WiFi event handling / reconnect logic
// ---------------------------------------------------------------------

static void reconnect_timer_cb(void *arg)
{
    esp_wifi_connect();
}

static uint32_t backoff_ms(int attempt)
{
    if (attempt > 6) {
        attempt = 6;
    }
    uint32_t ms = 1000u << attempt; // 1s, 2s, 4s, ... 64s
    return ms > BACKOFF_CAP_MS ? BACKOFF_CAP_MS : ms;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_boot_connect_pending) {
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        if (!s_stable) {
            // Still in the bounded boot-time attempt window.
            if (s_retry_count < MAX_BOOT_RETRIES) {
                s_retry_count++;
                ESP_LOGW(TAG, "connect attempt %d/%d failed (reason %d), retrying",
                         s_retry_count, MAX_BOOT_RETRIES, disc->reason);
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "gave up after %d attempts", MAX_BOOT_RETRIES);
                notify(WIFI_PROVISION_EVENT_CONNECT_FAILED);
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        } else {
            // We were connected before (this boot) — WiFi just dropped.
            // Keep retrying indefinitely with backoff. Do NOT fall back
            // to AP mode: the device is provisioned, the network (or
            // the router) is probably just bouncing.
            uint32_t delay = backoff_ms(s_retry_count++);
            ESP_LOGW(TAG, "WiFi dropped, reconnecting in %u ms", (unsigned)delay);
            notify(WIFI_PROVISION_EVENT_CONNECTING);
            esp_timer_stop(s_reconnect_timer); // no-op if not running
            esp_timer_start_once(s_reconnect_timer, (uint64_t)delay * 1000);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "got IP, connection stable");
        s_retry_count = 0;
        s_stable = true;
        notify(WIFI_PROVISION_EVENT_CONNECTED);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

static bool try_connect_sta(const char *ssid, const char *pass)
{
    s_stable = false;
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        (pass == NULL || strlen(pass) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    s_boot_connect_pending = true;
    ESP_ERROR_CHECK(esp_wifi_start()); // triggers WIFI_EVENT_STA_START -> esp_wifi_connect()

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

// ---------------------------------------------------------------------
// Captive portal (SoftAP + DNS hijack + HTTP form)
// ---------------------------------------------------------------------

static void url_decode_inplace(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '+') {
            *o++ = ' ';
            s++;
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
}

// Reduces to a valid single DNS label in place: lowercase letters,
// digits, and internal hyphens only, no leading/trailing hyphen. Runs
// only during the provisioning POST — never on a normal boot — so it
// has no steady-state cost. Falls back to "altimeter" if nothing valid
// is left (blank field, or a name that was all punctuation/whitespace).
static void sanitize_hostname(char *host, size_t cap)
{
    char *w = host;
    for (char *r = host; *r; r++) {
        unsigned char c = (unsigned char)*r;
        if (isalnum(c)) {
            *w++ = (char)tolower(c);
        } else if (c == '-' && w != host) {
            *w++ = '-';
        }
    }
    *w = '\0';
    while (w > host && *(w - 1) == '-') {
        *--w = '\0';
    }
    if (host[0] == '\0') {
        strlcpy(host, "altimeter", cap);
    }
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PROVISION_HTML, HTTPD_RESP_USE_STRLEN);
}

// Any path we don't otherwise recognize redirects to the setup page.
// This is what triggers the OS's automatic captive-portal popup, since
// those probes request arbitrary well-known URLs and expect either a
// 204 (means "no portal") or something else (means "there's a portal").
static esp_err_t catchall_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

// Appends `in`, JSON-string-escaped, into `out` starting at `pos`.
// `cap` must already account for the worst case (every byte of `in`
// expanding to a 6-char \u00XX escape) — SSIDs are attacker-controlled
// (anyone nearby can broadcast whatever bytes they want), so this can't
// just assume printable ASCII.
static size_t json_escape_append(char *out, size_t cap, size_t pos, const char *in)
{
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        char esc_buf[7];
        const char *esc = NULL;
        switch (*p) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            default:
                if (*p < 0x20) {
                    snprintf(esc_buf, sizeof(esc_buf), "\\u%04x", *p);
                    esc = esc_buf;
                }
        }
        size_t len = esc ? strlen(esc) : 1;
        if (pos + len >= cap) {
            break; // shouldn't happen — cap is sized for the worst case
        }
        if (esc) {
            memcpy(out + pos, esc, len);
        } else {
            out[pos] = (char)*p;
        }
        pos += len;
    }
    return pos;
}

// Scans for nearby APs and returns a JSON array of unique SSIDs (no
// RSSI/security info — the picker just needs names), strongest signal
// first. Blocks the calling httpd worker for the scan duration (active
// scan across all channels, ~1-3s) — acceptable for a single-client
// captive portal; the page shows a "Scanning..." state meanwhile.
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num > MAX_SCAN_RESULTS) {
        num = MAX_SCAN_RESULTS;
    }
    if (num == 0) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
    }

    wifi_ap_record_t *records = calloc(num, sizeof(wifi_ap_record_t));
    if (!records) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    uint16_t actual = num;
    esp_wifi_scan_get_ap_records(&actual, records);

    // Dedup by SSID — mesh/multi-AP setups broadcast the same name from
    // several BSSIDs — keeping whichever BSSID had the strongest signal.
    // O(n^2) is fine at this size (<= MAX_SCAN_RESULTS).
    wifi_ap_record_t *uniq[MAX_SCAN_RESULTS];
    int uniq_count = 0;
    for (int i = 0; i < actual; i++) {
        if (records[i].ssid[0] == '\0') {
            continue; // hidden network — nothing to show in the picker
        }
        int found = -1;
        for (int j = 0; j < uniq_count; j++) {
            if (strcmp((char *)uniq[j]->ssid, (char *)records[i].ssid) == 0) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            if (records[i].rssi > uniq[found]->rssi) {
                uniq[found] = &records[i];
            }
        } else {
            uniq[uniq_count++] = &records[i];
        }
    }

    // Insertion sort by RSSI descending — uniq_count is tiny.
    for (int i = 1; i < uniq_count; i++) {
        wifi_ap_record_t *key = uniq[i];
        int j = i - 1;
        while (j >= 0 && uniq[j]->rssi < key->rssi) {
            uniq[j + 1] = uniq[j];
            j--;
        }
        uniq[j + 1] = key;
    }

    size_t cap = 3 + (size_t)uniq_count * (JSON_MAX_ESCAPED_SSID + 3);
    char *json = malloc(cap);
    if (!json) {
        free(records);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    size_t pos = 0;
    json[pos++] = '[';
    for (int i = 0; i < uniq_count; i++) {
        if (i > 0) {
            json[pos++] = ',';
        }
        json[pos++] = '"';
        pos = json_escape_append(json, cap, pos, (char *)uniq[i]->ssid);
        json[pos++] = '"';
    }
    json[pos++] = ']';
    json[pos] = '\0';

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_send(req, json, pos);

    free(json);
    free(records);
    return send_err;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[256];
    int total = req->content_len < (int)sizeof(buf) - 1 ? (int)req->content_len : (int)sizeof(buf) - 1;
    int received = httpd_req_recv(req, buf, total);
    if (received <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};
    char tz[48] = {0};
    char host[33] = {0};
    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(buf, "password", pass, sizeof(pass));
    httpd_query_key_value(buf, "tz", tz, sizeof(tz));
    httpd_query_key_value(buf, "hostname", host, sizeof(host));
    url_decode_inplace(ssid);
    url_decode_inplace(pass);
    url_decode_inplace(tz);
    url_decode_inplace(host);
    sanitize_hostname(host, sizeof(host));

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "saving credentials for SSID '%s' (tz '%s', host '%s')", ssid, tz[0] ? tz : "UTC", host);
    esp_err_t err = save_credentials(ssid, pass, tz, host);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save credentials: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const char *resp = "<html><body><h3>Saved. Rebooting and connecting...</h3></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    // Give the response time to actually flush to the client before we
    // tear everything down.
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK; // unreachable
}

static void start_captive_portal(void)
{
    ESP_LOGI(TAG, "starting provisioning AP + captive portal");

    esp_wifi_stop();

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32-Setup-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap_config = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ap_ssid);

    // APSTA rather than plain AP: the /scan endpoint needs a STA
    // interface to scan with, even though it never associates. The AP
    // side (SoftAP + portal) behaves identically either way.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // esp_wifi persists the last-used STA SSID/password in its own
    // flash storage, separate from (and not cleared by) our own NVS
    // credential store — blank it so the STA interface (up only for
    // /scan, never meant to associate) has nothing stale to try. Also
    // clear s_boot_connect_pending: this esp_wifi_start() still fires
    // WIFI_EVENT_STA_START, and without this, event_handler() would
    // try to connect with that (now blank) config, fail, and burn
    // through the bounded retry counter into a spurious CONNECT_FAILED
    // notification right as provisioning starts.
    wifi_config_t blank_sta_config = { 0 };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &blank_sta_config));
    s_boot_connect_pending = false;

    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_get_ip_info(ap_netif, &ip_info);
    captive_dns_start(ip_info.ip.addr);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&s_httpd, &config));

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler };
    httpd_uri_t catchall = { .uri = "/*", .method = HTTP_GET, .handler = catchall_get_handler };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &save);
    httpd_register_uri_handler(s_httpd, &scan);
    httpd_register_uri_handler(s_httpd, &catchall); // must stay last — "/*" matches everything

    ESP_LOGI(TAG, "join WiFi network '%s' and a setup page should pop up "
                  "(or visit http://192.168.4.1)", ap_ssid);
}

// ---------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------

void wifi_provision_start(wifi_provision_status_cb_t status_cb)
{
    s_status_cb = status_cb;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // Runs for the lifetime of the device, independent of AP/STA state,
    // so the BOOT button works as a factory-reset trigger whether or
    // not the device is currently provisioned.
    factory_reset_start();

    char ssid[33] = {0};
    char pass[65] = {0};
    bool have_creds = (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK)
                       && strlen(ssid) > 0;

    if (have_creds) {
        ESP_LOGI(TAG, "found saved credentials for '%s', attempting to connect", ssid);
        notify(WIFI_PROVISION_EVENT_CONNECTING);
        if (try_connect_sta(ssid, pass)) {
            ESP_LOGI(TAG, "connected to '%s'", ssid);
            return; // steady-state reconnect handled by event_handler from here on
        }
        ESP_LOGW(TAG, "could not connect with saved credentials, falling back to provisioning");
    } else {
        ESP_LOGI(TAG, "no saved credentials, starting provisioning");
        notify(WIFI_PROVISION_EVENT_BOOT_CLEAN);
    }

    start_captive_portal();
}
