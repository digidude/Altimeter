// Minimal captive-portal DNS server: answers every A-record query with
// a fixed IP. No recursion, no caching, no records other than A —
// on purpose. This only needs to run while the device is in
// provisioning (SoftAP) mode.

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dns_server.h"

static const char *TAG = "dns_server";

#define DNS_PORT 53
#define DNS_MAX_LEN 512

static TaskHandle_t s_dns_task = NULL;
static int s_sock = -1;
static uint32_t s_reply_ip; // network byte order

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static void dns_task(void *arg)
{
    uint8_t buf[DNS_MAX_LEN];
    uint8_t resp[DNS_MAX_LEN];
    struct sockaddr_in client;

    while (1) {
        socklen_t client_len = sizeof(client);
        int len = recvfrom(s_sock, buf, sizeof(buf), 0,
                            (struct sockaddr *)&client, &client_len);
        if (len < (int)sizeof(dns_header_t)) {
            continue; // too short to be a real query, or socket error
        }

        dns_header_t *hdr = (dns_header_t *)buf;
        uint16_t flags = ntohs(hdr->flags);
        uint16_t qdcount = ntohs(hdr->qdcount);
        uint8_t opcode = (flags >> 11) & 0x0F;
        if (opcode != 0 || qdcount == 0) {
            continue; // only handle standard queries with >=1 question
        }

        // Walk the question's QNAME (sequence of length-prefixed labels,
        // terminated by a zero-length label) to find where it ends.
        int pos = (int)sizeof(dns_header_t);
        while (pos < len && buf[pos] != 0) {
            int label_len = buf[pos];
            if (label_len > 63 || pos + label_len + 1 > len) {
                pos = -1; // malformed, bail
                break;
            }
            pos += label_len + 1;
        }
        if (pos < 0) {
            continue;
        }
        pos += 1;      // consume the zero-length terminator label
        pos += 4;      // consume QTYPE(2) + QCLASS(2)
        if (pos > len) {
            continue;
        }
        int question_len = pos - (int)sizeof(dns_header_t);

        // Response = header + original question, verbatim + one answer RR
        // pointing the queried name at s_reply_ip.
        int rpos = 0;
        dns_header_t rhdr = {
            .id = hdr->id,
            .flags = htons(0x8180), // response, authoritative, no error
            .qdcount = htons(1),
            .ancount = htons(1),
            .nscount = 0,
            .arcount = 0,
        };
        memcpy(resp + rpos, &rhdr, sizeof(rhdr));
        rpos += sizeof(rhdr);

        memcpy(resp + rpos, buf + sizeof(dns_header_t), question_len);
        rpos += question_len;

        // Answer RR: name = compression pointer back to the question name
        // at offset 0x0C (right after the 12-byte header).
        resp[rpos++] = 0xC0;
        resp[rpos++] = 0x0C;
        resp[rpos++] = 0x00; resp[rpos++] = 0x01; // TYPE = A
        resp[rpos++] = 0x00; resp[rpos++] = 0x01; // CLASS = IN
        resp[rpos++] = 0x00; resp[rpos++] = 0x00; // TTL = 60s
        resp[rpos++] = 0x00; resp[rpos++] = 0x3C;
        resp[rpos++] = 0x00; resp[rpos++] = 0x04; // RDLENGTH = 4
        memcpy(resp + rpos, &s_reply_ip, 4);
        rpos += 4;

        sendto(s_sock, resp, rpos, 0, (struct sockaddr *)&client, client_len);
    }
}

esp_err_t captive_dns_start(uint32_t ip_addr_be)
{
    s_reply_ip = ip_addr_be;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind() :%d failed: errno %d", DNS_PORT, errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    if (xTaskCreate(dns_task, "captive_dns", 4096, NULL, 5, &s_dns_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to create dns_task");
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "captive DNS server started on :%d", DNS_PORT);
    return ESP_OK;
}

void captive_dns_stop(void)
{
    if (s_dns_task) {
        vTaskDelete(s_dns_task);
        s_dns_task = NULL;
    }
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}
