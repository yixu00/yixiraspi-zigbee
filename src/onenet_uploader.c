#include "onenet_uploader.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define ONENET_KEEP_ALIVE_SECONDS 60
#define ONENET_SOCKET_TIMEOUT_SECONDS 3

typedef struct {
    uint32_t state[4];
    uint64_t bit_count;
    unsigned char buffer[64];
} md5_context_t;

static uint32_t md5_left_rotate(uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32U - bits));
}

static void md5_transform(md5_context_t *ctx, const unsigned char block[64]) {
    static const uint32_t shifts[64] = {
        7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
        5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U,
        4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
        6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U
    };
    static const uint32_t constants[64] = {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
        0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
        0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
        0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
        0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
        0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
        0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
        0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
        0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
        0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
        0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
        0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
        0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
        0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
        0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
    };
    uint32_t words[16];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    unsigned int i;

    for (i = 0; i < 16U; ++i) {
        words[i] = (uint32_t)block[i * 4U]
                 | ((uint32_t)block[i * 4U + 1U] << 8U)
                 | ((uint32_t)block[i * 4U + 2U] << 16U)
                 | ((uint32_t)block[i * 4U + 3U] << 24U);
    }

    for (i = 0; i < 64U; ++i) {
        uint32_t f;
        uint32_t g;
        if (i < 16U) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32U) {
            f = (d & b) | ((~d) & c);
            g = (5U * i + 1U) % 16U;
        } else if (i < 48U) {
            f = b ^ c ^ d;
            g = (3U * i + 5U) % 16U;
        } else {
            f = c ^ (b | (~d));
            g = (7U * i) % 16U;
        }

        {
            uint32_t temp = d;
            d = c;
            c = b;
            b = b + md5_left_rotate(a + f + constants[i] + words[g], shifts[i]);
            a = temp;
        }
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

static void md5_init(md5_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xefcdab89U;
    ctx->state[2] = 0x98badcfeU;
    ctx->state[3] = 0x10325476U;
}

static void md5_update(md5_context_t *ctx, const unsigned char *data, size_t length) {
    size_t offset = (size_t)((ctx->bit_count / 8U) % 64U);
    size_t i = 0;

    ctx->bit_count += (uint64_t)length * 8U;

    if (offset > 0U) {
        size_t need = 64U - offset;
        size_t copy = length < need ? length : need;
        memcpy(ctx->buffer + offset, data, copy);
        offset += copy;
        i += copy;
        if (offset == 64U) {
            md5_transform(ctx, ctx->buffer);
        }
    }

    while (i + 64U <= length) {
        md5_transform(ctx, data + i);
        i += 64U;
    }

    if (i < length) {
        memcpy(ctx->buffer, data + i, length - i);
    }
}

static void md5_final(md5_context_t *ctx, unsigned char out[16]) {
    unsigned char padding[64];
    unsigned char bit_length[8];
    size_t index;
    size_t pad_len;
    unsigned int i;

    memset(padding, 0, sizeof(padding));
    padding[0] = 0x80U;

    for (i = 0; i < 8U; ++i) {
        bit_length[i] = (unsigned char)((ctx->bit_count >> (8U * i)) & 0xffU);
    }

    index = (size_t)((ctx->bit_count / 8U) % 64U);
    pad_len = (index < 56U) ? (56U - index) : (120U - index);
    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bit_length, sizeof(bit_length));

    for (i = 0; i < 4U; ++i) {
        out[i * 4U] = (unsigned char)(ctx->state[i] & 0xffU);
        out[i * 4U + 1U] = (unsigned char)((ctx->state[i] >> 8U) & 0xffU);
        out[i * 4U + 2U] = (unsigned char)((ctx->state[i] >> 16U) & 0xffU);
        out[i * 4U + 3U] = (unsigned char)((ctx->state[i] >> 24U) & 0xffU);
    }
}

static void hmac_md5(const unsigned char *key, size_t key_len, const unsigned char *data, size_t data_len, unsigned char out[16]) {
    unsigned char key_block[64];
    unsigned char inner_key[64];
    unsigned char outer_key[64];
    unsigned char inner_digest[16];
    md5_context_t ctx;
    size_t i;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > sizeof(key_block)) {
        md5_init(&ctx);
        md5_update(&ctx, key, key_len);
        md5_final(&ctx, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    for (i = 0; i < sizeof(key_block); ++i) {
        inner_key[i] = (unsigned char)(key_block[i] ^ 0x36U);
        outer_key[i] = (unsigned char)(key_block[i] ^ 0x5cU);
    }

    md5_init(&ctx);
    md5_update(&ctx, inner_key, sizeof(inner_key));
    md5_update(&ctx, data, data_len);
    md5_final(&ctx, inner_digest);

    md5_init(&ctx);
    md5_update(&ctx, outer_key, sizeof(outer_key));
    md5_update(&ctx, inner_digest, sizeof(inner_digest));
    md5_final(&ctx, out);
}

static size_t base64_decode(const char *input, unsigned char *output, size_t output_size) {
    static const signed char map[256] = {
        ['A'] = 0, ['B'] = 1, ['C'] = 2, ['D'] = 3, ['E'] = 4, ['F'] = 5, ['G'] = 6, ['H'] = 7,
        ['I'] = 8, ['J'] = 9, ['K'] = 10, ['L'] = 11, ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
        ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19, ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
        ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31,
        ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35, ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39,
        ['o'] = 40, ['p'] = 41, ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
        ['w'] = 48, ['x'] = 49, ['y'] = 50, ['z'] = 51, ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55,
        ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59, ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63
    };
    size_t out_len = 0;
    int val = 0;
    int valb = -8;

    while (*input != '\0') {
        unsigned char ch = (unsigned char)*input++;
        if (isspace((int)ch)) {
            continue;
        }
        if (ch == '=') {
            break;
        }
        if (map[ch] == 0 && ch != 'A') {
            return 0;
        }
        val = (val << 6) + map[ch];
        valb += 6;
        if (valb >= 0) {
            if (out_len >= output_size) {
                return 0;
            }
            output[out_len++] = (unsigned char)((val >> valb) & 0xff);
            valb -= 8;
        }
    }

    return out_len;
}

static size_t base64_encode(const unsigned char *input, size_t input_len, char *output, size_t output_size) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 0;
    size_t i;

    for (i = 0; i < input_len; i += 3U) {
        unsigned int octet_a = input[i];
        unsigned int octet_b = (i + 1U < input_len) ? input[i + 1U] : 0U;
        unsigned int octet_c = (i + 2U < input_len) ? input[i + 2U] : 0U;
        unsigned int triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;

        if (out_len + 4U >= output_size) {
            return 0;
        }

        output[out_len++] = table[(triple >> 18U) & 0x3fU];
        output[out_len++] = table[(triple >> 12U) & 0x3fU];
        output[out_len++] = (i + 1U < input_len) ? table[(triple >> 6U) & 0x3fU] : '=';
        output[out_len++] = (i + 2U < input_len) ? table[triple & 0x3fU] : '=';
    }

    output[out_len] = '\0';
    return out_len;
}

static size_t url_encode(const char *input, char *output, size_t output_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t out_len = 0;

    while (*input != '\0') {
        unsigned char ch = (unsigned char)*input++;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            if (out_len + 1U >= output_size) {
                return 0;
            }
            output[out_len++] = (char)ch;
        } else {
            if (out_len + 3U >= output_size) {
                return 0;
            }
            output[out_len++] = '%';
            output[out_len++] = hex[(ch >> 4U) & 0x0fU];
            output[out_len++] = hex[ch & 0x0fU];
        }
    }

    output[out_len] = '\0';
    return out_len;
}

static unsigned long long token_expiry_time(void) {
    return 1893456000ULL;
}

static bool build_token(const onenet_uploader_t *uploader, char *token, size_t token_size) {
    char resource[128];
    char resource_encoded[192];
    char raw_sign[256];
    unsigned char key[128];
    size_t key_len;
    unsigned char digest[16];
    char digest_b64[64];
    char digest_encoded[96];
    int written;

    written = snprintf(resource, sizeof(resource), "products/%s/devices/%s", uploader->product_id, uploader->device_name);
    if (written < 0 || (size_t)written >= sizeof(resource)) {
        return false;
    }
    if (url_encode(resource, resource_encoded, sizeof(resource_encoded)) == 0U) {
        return false;
    }

    written = snprintf(raw_sign, sizeof(raw_sign), "%llu\nmd5\n%s\n2018-10-31", token_expiry_time(), resource);
    if (written < 0 || (size_t)written >= sizeof(raw_sign)) {
        return false;
    }

    key_len = base64_decode(uploader->access_key, key, sizeof(key));
    if (key_len == 0U) {
        return false;
    }

    hmac_md5(key, key_len, (const unsigned char *)raw_sign, strlen(raw_sign), digest);
    if (base64_encode(digest, sizeof(digest), digest_b64, sizeof(digest_b64)) == 0U) {
        return false;
    }
    if (url_encode(digest_b64, digest_encoded, sizeof(digest_encoded)) == 0U) {
        return false;
    }

    written = snprintf(
        token,
        token_size,
        "version=2018-10-31&res=%s&et=%llu&method=md5&sign=%s",
        resource_encoded,
        token_expiry_time(),
        digest_encoded
    );
    return written > 0 && (size_t)written < token_size;
}

static size_t mqtt_encode_remaining_length(size_t value, unsigned char out[4]) {
    size_t index = 0;
    do {
        unsigned char encoded = (unsigned char)(value % 128U);
        value /= 128U;
        if (value > 0U) {
            encoded = (unsigned char)(encoded | 0x80U);
        }
        out[index++] = encoded;
    } while (value > 0U && index < 4U);
    return index;
}

static unsigned char *mqtt_write_string(unsigned char *cursor, unsigned char *end, const char *text) {
    size_t length = strlen(text);
    if ((size_t)(end - cursor) < length + 2U) {
        return NULL;
    }
    cursor[0] = (unsigned char)((length >> 8U) & 0xffU);
    cursor[1] = (unsigned char)(length & 0xffU);
    memcpy(cursor + 2, text, length);
    return cursor + 2 + length;
}

static bool socket_send_all(int fd, const unsigned char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t count = send(fd, data + sent, length - sent, 0);
        if (count <= 0) {
            return false;
        }
        sent += (size_t)count;
    }
    return true;
}

static void set_error(onenet_uploader_t *uploader, const char *message) {
    snprintf(uploader->last_error, sizeof(uploader->last_error), "%s", message);
}

static void disconnect_socket(onenet_uploader_t *uploader) {
    if (uploader->socket_fd >= 0) {
        close(uploader->socket_fd);
        uploader->socket_fd = -1;
    }
    uploader->connected = false;
}

static bool socket_recv_all(int fd, unsigned char *data, size_t length) {
    size_t received = 0;
    while (received < length) {
        ssize_t count = recv(fd, data + received, length - received, 0);
        if (count <= 0) {
            return false;
        }
        received += (size_t)count;
    }
    return true;
}

static bool set_socket_timeouts(int fd) {
    struct timeval timeout;
    timeout.tv_sec = ONENET_SOCKET_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0
        && setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

static bool connect_socket(onenet_uploader_t *uploader) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *cursor;
    char port_text[16];
    int rc;

    snprintf(port_text, sizeof(port_text), "%d", uploader->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(uploader->host, port_text, &hints, &result);
    if (rc != 0) {
        set_error(uploader, gai_strerror(rc));
        return false;
    }

    for (cursor = result; cursor != NULL; cursor = cursor->ai_next) {
        int fd = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (!set_socket_timeouts(fd)) {
            close(fd);
            continue;
        }
        if (connect(fd, cursor->ai_addr, cursor->ai_addrlen) == 0) {
            uploader->socket_fd = fd;
            freeaddrinfo(result);
            return true;
        }
        close(fd);
    }

    freeaddrinfo(result);
    set_error(uploader, strerror(errno));
    return false;
}

static bool mqtt_connect(onenet_uploader_t *uploader) {
    unsigned char packet[1024];
    unsigned char remaining_length[4];
    unsigned char reply[4];
    unsigned char *cursor;
    char token[512];
    size_t variable_length;
    size_t total_length;
    ssize_t count;

    if (!connect_socket(uploader)) {
        return false;
    }

    if (!build_token(uploader, token, sizeof(token))) {
        disconnect_socket(uploader);
        set_error(uploader, "Failed to build token");
        return false;
    }

    cursor = packet + 1;
    cursor = mqtt_write_string(cursor, packet + sizeof(packet), "MQTT");
    if (cursor == NULL) {
        disconnect_socket(uploader);
        set_error(uploader, "CONNECT packet overflow");
        return false;
    }
    *cursor++ = 0x04U;
    *cursor++ = 0xc2U;
    *cursor++ = 0x00U;
    *cursor++ = ONENET_KEEP_ALIVE_SECONDS;
    if ((size_t)((packet + sizeof(packet)) - cursor) < 4U) {
        disconnect_socket(uploader);
        set_error(uploader, "CONNECT packet overflow");
        return false;
    }
    cursor = mqtt_write_string(cursor, packet + sizeof(packet), uploader->device_name);
    if (cursor == NULL) {
        disconnect_socket(uploader);
        set_error(uploader, "CONNECT packet overflow");
        return false;
    }
    cursor = mqtt_write_string(cursor, packet + sizeof(packet), uploader->product_id);
    if (cursor == NULL) {
        disconnect_socket(uploader);
        set_error(uploader, "CONNECT packet overflow");
        return false;
    }
    cursor = mqtt_write_string(cursor, packet + sizeof(packet), token);
    if (cursor == NULL) {
        disconnect_socket(uploader);
        set_error(uploader, "CONNECT packet overflow");
        return false;
    }

    variable_length = (size_t)(cursor - (packet + 1));
    total_length = mqtt_encode_remaining_length(variable_length, remaining_length);
    memmove(packet + 1 + total_length, packet + 1, variable_length);
    packet[0] = 0x10U;
    memcpy(packet + 1, remaining_length, total_length);

    if (!socket_send_all(uploader->socket_fd, packet, 1U + total_length + variable_length)) {
        disconnect_socket(uploader);
        set_error(uploader, "Failed to send CONNECT");
        return false;
    }

    count = 4;
    if (!socket_recv_all(uploader->socket_fd, reply, sizeof(reply)) || count != 4 || reply[0] != 0x20U || reply[1] != 0x02U || reply[3] != 0x00U) {
        disconnect_socket(uploader);
        set_error(uploader, "Broker rejected CONNECT");
        return false;
    }

    uploader->connected = true;
    uploader->last_error[0] = '\0';
    return true;
}

static bool mqtt_publish(onenet_uploader_t *uploader, const char *payload) {
    unsigned char packet[1024];
    unsigned char remaining_length[4];
    unsigned char *cursor = packet + 1;
    size_t topic_length = strlen(uploader->topic);
    size_t payload_length = strlen(payload);
    size_t body_length;
    size_t header_length;

    if (!uploader->connected && !mqtt_connect(uploader)) {
        return false;
    }

    cursor = mqtt_write_string(cursor, packet + sizeof(packet), uploader->topic);
    if (cursor == NULL) {
        disconnect_socket(uploader);
        set_error(uploader, "PUBLISH packet overflow");
        return false;
    }
    if ((size_t)((packet + sizeof(packet)) - cursor) < payload_length) {
        disconnect_socket(uploader);
        set_error(uploader, "PUBLISH packet overflow");
        return false;
    }
    memcpy(cursor, payload, payload_length);
    cursor += payload_length;

    body_length = 2U + topic_length + payload_length;
    header_length = mqtt_encode_remaining_length(body_length, remaining_length);
    memmove(packet + 1 + header_length, packet + 1, body_length);
    packet[0] = 0x30U;
    memcpy(packet + 1, remaining_length, header_length);

    if (!socket_send_all(uploader->socket_fd, packet, 1U + header_length + body_length)) {
        disconnect_socket(uploader);
        set_error(uploader, "Failed to publish datapoint");
        return false;
    }

    uploader->last_error[0] = '\0';
    return true;
}

static bool parse_labeled_value(const char *text, const char *label, double *value) {
    const char *cursor = text;
    size_t label_len = strlen(label);

    while ((cursor = strstr(cursor, label)) != NULL) {
        char *end = NULL;
        double parsed;

        cursor += label_len;
        parsed = strtod(cursor, &end);
        if (end != cursor && isfinite(parsed)) {
            *value = parsed;
            return true;
        }
    }

    return false;
}

bool onenet_extract_measurements(const char *text, double *temperature, double *humidity) {
    return parse_labeled_value(text, "Temp:", temperature)
        && parse_labeled_value(text, "Humi:", humidity);
}

void onenet_uploader_init(onenet_uploader_t *uploader, const uart_options_t *options) {
    memset(uploader, 0, sizeof(*uploader));
    uploader->product_id = options->onenet_product_id;
    uploader->device_name = options->onenet_device_name;
    uploader->access_key = options->onenet_access_key;
    uploader->host = options->onenet_host;
    uploader->port = options->onenet_port;
    uploader->socket_fd = -1;
    uploader->enabled = options->onenet_product_id[0] != '\0';
    uploader->next_message_id = 1U;

    if (snprintf(uploader->topic, sizeof(uploader->topic), "$sys/%s/%s/dp/post/json", uploader->product_id, uploader->device_name) >= (int)sizeof(uploader->topic)) {
        uploader->enabled = false;
        set_error(uploader, "OneNET topic too long");
        return;
    }

    if (!uploader->enabled) {
        set_error(uploader, "OneNET disabled: missing --onenet-product-id");
    }
}

void onenet_uploader_cleanup(onenet_uploader_t *uploader) {
    disconnect_socket(uploader);
}

bool onenet_uploader_publish_frame(onenet_uploader_t *uploader, const char *frame_text) {
    double temperature;
    double humidity;
    char payload[256];

    if (!uploader->enabled) {
        return false;
    }
    if (!onenet_extract_measurements(frame_text, &temperature, &humidity)) {
        set_error(uploader, "OneNET waiting for two numeric values");
        return false;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"id\":%u,\"dp\":{\"temperature\":[{\"v\":%.2f}],\"humidity\":[{\"v\":%.2f}],\"light\":[{\"v\":0}]}}",
        uploader->next_message_id++,
        temperature,
        humidity
    );

    return mqtt_publish(uploader, payload);
}

const char *onenet_uploader_status(const onenet_uploader_t *uploader) {
    if (uploader->last_error[0] != '\0') {
        return uploader->last_error;
    }
    if (!uploader->enabled) {
        return uploader->last_error;
    }
    if (uploader->connected) {
        return "connected";
    }
    return "ready";
}
