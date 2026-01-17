#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>

#define PORT 9090
#define WS_RESOURCE_PATH "/metrics"
#define BUFFER_SIZE 4096
#define WS_MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

#define SHA1_ROTL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e;
    uint32_t w[80];
    int i;
    
    for (i = 0; i < 16; i++) {
        w[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) |
               (buffer[i * 4 + 2] << 8) | buffer[i * 4 + 3];
    }
    
    for (i = 16; i < 80; i++) {
        w[i] = SHA1_ROTL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }
    
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    
    for (i = 0; i < 80; i++) {
        uint32_t f, k, temp;
        
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        
        temp = SHA1_ROTL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = SHA1_ROTL(b, 30);
        b = a;
        a = temp;
    }
    
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    size_t i, j;
    
    j = (ctx->count[0] >> 3) & 63;
    
    if ((ctx->count[0] += len << 3) < (len << 3))
        ctx->count[1]++;
    ctx->count[1] += (len >> 29);
    
    if ((j + len) > 63) {
        i = 64 - j;
        memcpy(&ctx->buffer[j], data, i);
        sha1_transform(ctx->state, ctx->buffer);
        
        for (; i + 63 < len; i += 64) {
            sha1_transform(ctx->state, &data[i]);
        }
        j = 0;
    } else {
        i = 0;
    }
    
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

void sha1_final(uint8_t digest[20], SHA1_CTX *ctx) {
    uint8_t finalcount[8];
    int i;
    
    for (i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >>
            ((3 - (i & 3)) * 8)) & 255);
    }
    
    sha1_update(ctx, (uint8_t *)"\200", 1);
    while ((ctx->count[0] & 504) != 448) {
        sha1_update(ctx, (uint8_t *)"\0", 1);
    }
    
    sha1_update(ctx, finalcount, 8);
    
    for (i = 0; i < 20; i++) {
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

// Base64 encoding (
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const uint8_t *input, size_t len, char *output) {
    size_t i, j;
    uint32_t octet_a, octet_b, octet_c, triple;
    
    for (i = 0, j = 0; i < len;) {
        octet_a = i < len ? input[i++] : 0;
        octet_b = i < len ? input[i++] : 0;
        octet_c = i < len ? input[i++] : 0;
        
        triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        output[j++] = base64_chars[(triple >> 18) & 0x3F];
        output[j++] = base64_chars[(triple >> 12) & 0x3F];
        output[j++] = base64_chars[(triple >> 6) & 0x3F];
        output[j++] = base64_chars[triple & 0x3F];
    }
    
    int padding = (3 - len % 3) % 3;
    for (i = 0; i < padding; i++) {
        output[j - 1 - i] = '=';
    }
    output[j] = '\0';
}

// WebSocket handshake
int ws_handshake(int client_sock, char *request) {
    char *key_start = strstr(request, "Sec-WebSocket-Key: ");
    if (!key_start) return -1;
    
    key_start += 19;
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) return -1;
    
    int key_len = key_end - key_start;
    char key[256];
    strncpy(key, key_start, key_len);
    key[key_len] = '\0';
    
    // Concatenate key with magic string
    char accept_key[512];
    snprintf(accept_key, sizeof(accept_key), "%s%s", key, WS_MAGIC_STRING);
    
    // SHA1 hash
    SHA1_CTX ctx;
    uint8_t hash[20];
    sha1_init(&ctx);
    sha1_update(&ctx, (uint8_t*)accept_key, strlen(accept_key));
    sha1_final(hash, &ctx);
    
    // Base64 encode
    char accept_base64[64];
    base64_encode(hash, 20, accept_base64);
    
    // Send handshake response
    char response[1024];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept_base64);
    
    write(client_sock, response, strlen(response));
    return 0;
}

// Send WebSocket frame
int ws_send_frame(int client_sock, const char *data) {
    size_t data_len = strlen(data);
    uint8_t frame[BUFFER_SIZE];
    size_t frame_len = 0;
    
    // FIN bit set, text frame (opcode 0x1)
    frame[0] = 0x81;
    
    // Payload length
    if (data_len <= 125) {
        frame[1] = (uint8_t)data_len;
        frame_len = 2;
    } else if (data_len <= 65535) {
        frame[1] = 126;
        frame[2] = (data_len >> 8) & 0xFF;
        frame[3] = data_len & 0xFF;
        frame_len = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (data_len >> (56 - i * 8)) & 0xFF;
        }
        frame_len = 10;
    }
    
    // Copy payload
    memcpy(frame + frame_len, data, data_len);
    frame_len += data_len;
    
    ssize_t written = write(client_sock, frame, frame_len);
    if (written < 0) {
        return -1; // Likely client disconnected
    }
    return 0;
}

// Generate Prometheus metrics
void generate_metrics(char *buffer, size_t size) {
    static time_t start_time = 0;
    if (start_time == 0) start_time = time(NULL);
    
    time_t now = time(NULL);
    long uptime = now - start_time;
    
    snprintf(buffer, size,
        "# HELP process_uptime_seconds Total uptime of the process\n"
        "# TYPE process_uptime_seconds counter\n"
        "process_uptime_seconds %ld\n"
        "\n"
        "# HELP cpu_usage_percent CPU usage percentage\n"
        "# TYPE cpu_usage_percent gauge\n"
        "cpu_usage_percent %.2f\n"
        "\n"
        "# HELP memory_usage_bytes Memory usage in bytes\n"
        "# TYPE memory_usage_bytes gauge\n"
        "memory_usage_bytes %d\n"
        "\n"
        "# HELP current_timestamp Unix timestamp\n"
        "# TYPE current_timestamp gauge\n"
        "current_timestamp %ld\n",
        uptime,
        25.5 + (rand() % 100) / 10.0,
        1024000 + (rand() % 100000),
        now
    );
}

// Handle connections
void handle_connection(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1);
    
    if (bytes_read <= 0) {
        close(client_sock);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    // Check for WebSocket upgrade
    if (strstr(buffer, "Upgrade: websocket")) {
        printf("WebSocket connection established\n");
        
        if (ws_handshake(client_sock, buffer) == 0) {
            char metrics[BUFFER_SIZE];
            while (1) {
                generate_metrics(metrics, sizeof(metrics));
                if (ws_send_frame(client_sock, metrics) < 0) {
                    printf("WebSocket client disconnected\n");
                    break; 
                }
                sleep(2);
            }
        }
    }
    // HTTP /metrics endpoint
    else if (strncmp(buffer, "GET /metrics", 12) == 0) {
        // Check if it's an HTTP request (not WebSocket upgrade)
        if (!strstr(buffer, "Upgrade:")) {
            char metrics[BUFFER_SIZE];
            char response[BUFFER_SIZE * 2];
            
            generate_metrics(metrics, sizeof(metrics));
            
            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain; version=0.0.4\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(metrics), metrics);
            
            write(client_sock, response, strlen(response));
        } else {
            // WebSocket upgrade for /metrics path
            printf("WebSocket connection established on /metrics\n");
            
            if (ws_handshake(client_sock, buffer) == 0) {
                char metrics[BUFFER_SIZE];
                while (1) {
                    generate_metrics(metrics, sizeof(metrics));
                    if (ws_send_frame(client_sock, metrics) < 0) {
                        printf("WebSocket client disconnected on /metrics\n");
                        break; 
                    }
                    sleep(2);
                }
            }
        }
    }
    // Root endpoint with HTML
    else {
        const char *html = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html><html><body>"
            "<h1>QNX Metrics Server</h1>"
            "<pre id='m'>Connecting...</pre>"
                "<script>"
<                "function connect(){"
                "  var w=new WebSocket('ws://'+location.host);"
                "  w.onmessage=function(e){document.getElementById('m').textContent=e.data};"
                "  w.onclose=function(){document.getElementById('m').textContent='Disconnected.';};"
                "  w.onerror=function(){document.getElementById('m').textContent='Connection error.';};"
                "}"
                "connect();"
                "</script></body></html>";
        
        write(client_sock, html, strlen(html));
    }
    
    close(client_sock);
}

void sigchld_handler(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    srand(time(NULL));

    // Ignore SIGPIPE so a disconnected WebSocket client doesn't kill the server
    signal(SIGPIPE, SIG_IGN);

    signal(SIGCHLD, sigchld_handler);
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        return 1;
    }
    
    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        close(server_sock);
        return 1;
    }
    
    printf("=== QNX Metrics Server Started ===\n");
    printf("Port: %d\n", PORT);
    printf("HTTP metrics: http://<ip>:%d/metrics\n", PORT);
    printf("WebSocket: ws://<ip>:%d\n", PORT);
    printf("Web UI: http://<ip>:%d/\n", PORT);
    printf("===================================\n");
    
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            close(client_sock);
            continue;
        } else if (pid == 0) {
            close(server_sock); 
            handle_connection(client_sock);
            exit(0);
        } else {
            close(client_sock);
        }
        
    }
    
    close(server_sock);
    return 0;
}