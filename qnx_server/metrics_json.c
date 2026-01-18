#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define PORT 9090
#define BUFFER_SIZE 16384

volatile sig_atomic_t running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Safe command execution */
int run_command(const char *cmd, char *output, size_t size) {
    FILE *fp;
    size_t len = 0;
    char line[512];
    
    if (!cmd || !output || size < 2) return -1;
    output[0] = '\0';
    
    fp = popen(cmd, "r");
    if (!fp) return -1;
    
    while (fgets(line, sizeof(line), fp) && len < size - 512) {
        size_t line_len = strlen(line);
        memcpy(output + len, line, line_len);
        len += line_len;
    }
    output[len] = '\0';
    
    pclose(fp);
    return (int)len;
}

/* Escape string for JSON */
void json_escape_string(const char *src, char *dest, size_t dest_size) {
    size_t i, j = 0;
    
    if (!dest || dest_size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    
    for (i = 0; src[i] && j < dest_size - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j < dest_size - 3) {
                dest[j++] = '\\';
                dest[j++] = c;
            }
        } else if (c == '\n') {
            if (j < dest_size - 3) {
                dest[j++] = '\\';
                dest[j++] = 'n';
            }
        } else if (c == '\r') {
            /* skip */
        } else if (c == '\t') {
            if (j < dest_size - 3) {
                dest[j++] = '\\';
                dest[j++] = 't';
            }
        } else if ((unsigned char)c >= 32) {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

/* Generate comprehensive JSON metrics */
int generate_json(char *out, size_t size) {
    char hostname[64] = "unknown";
    char kernel[64] = "unknown";
    char cpu_info[128] = "unknown";
    char uptime_str[128] = "";
    char mem_raw[1024] = "";
    char proc_raw[8192] = "";
    char disk_raw[2048] = "";
    char net_raw[2048] = "";
    char proc_count[32] = "0";
    char escaped[4096];
    size_t len = 0;
    unsigned long long timestamp;
    
    /* Gather system info */
    gethostname(hostname, sizeof(hostname) - 1);
    run_command("uname -r 2>/dev/null | tr -d '\n\r'", kernel, sizeof(kernel));
    run_command("uname -m 2>/dev/null | tr -d '\n\r'", cpu_info, sizeof(cpu_info));
    run_command("uptime 2>/dev/null | tr -d '\n\r'", uptime_str, sizeof(uptime_str));
    
    /* Memory info */
    run_command("pidin info 2>/dev/null | grep -i mem", mem_raw, sizeof(mem_raw));
    if (strlen(mem_raw) == 0) {
        run_command("showmem 2>/dev/null | head -10", mem_raw, sizeof(mem_raw));
    }
    
    /* Process list */
    run_command("pidin -F \"%N %H %J %n\" 2>/dev/null | head -50", proc_raw, sizeof(proc_raw));
    if (strlen(proc_raw) == 0) {
        run_command("pidin 2>/dev/null | head -50", proc_raw, sizeof(proc_raw));
    }
    
    /* Process count */
    run_command("pidin 2>/dev/null | wc -l | tr -d ' \n\r'", proc_count, sizeof(proc_count));
    
    /* Disk info */
    run_command("df -h 2>/dev/null", disk_raw, sizeof(disk_raw));
    if (strlen(disk_raw) == 0) {
        run_command("df 2>/dev/null", disk_raw, sizeof(disk_raw));
    }
    
    /* Network info */
    run_command("netstat -i 2>/dev/null | head -20", net_raw, sizeof(net_raw));
    if (strlen(net_raw) == 0) {
        run_command("ifconfig 2>/dev/null | head -30", net_raw, sizeof(net_raw));
    }
    
    timestamp = (unsigned long long)time(NULL);
    
    /* Build JSON */
    len += snprintf(out + len, size - len, "{\n");
    
    /* System info */
    len += snprintf(out + len, size - len,
        "  \"system\": {\n"
        "    \"hostname\": \"%s\",\n"
        "    \"kernel\": \"%s\",\n"
        "    \"architecture\": \"%s\",\n"
        "    \"timestamp\": %llu,\n",
        hostname, kernel, cpu_info, timestamp);
    
    json_escape_string(uptime_str, escaped, sizeof(escaped));
    len += snprintf(out + len, size - len,
        "    \"uptime\": \"%s\"\n"
        "  },\n",
        escaped);
    
    /* Process summary */
    len += snprintf(out + len, size - len,
        "  \"processes\": {\n"
        "    \"count\": %s,\n",
        proc_count[0] ? proc_count : "0");
    
    json_escape_string(proc_raw, escaped, sizeof(escaped));
    len += snprintf(out + len, size - len,
        "    \"list\": \"%s\"\n"
        "  },\n",
        escaped);
    
    /* Memory */
    len += snprintf(out + len, size - len, "  \"memory\": {\n");
    json_escape_string(mem_raw, escaped, sizeof(escaped));
    len += snprintf(out + len, size - len,
        "    \"info\": \"%s\"\n"
        "  },\n",
        escaped);
    
    /* Disk */
    len += snprintf(out + len, size - len, "  \"disk\": {\n");
    json_escape_string(disk_raw, escaped, sizeof(escaped));
    len += snprintf(out + len, size - len,
        "    \"info\": \"%s\"\n"
        "  },\n",
        escaped);
    
    /* Network */
    len += snprintf(out + len, size - len, "  \"network\": {\n");
    json_escape_string(net_raw, escaped, sizeof(escaped));
    len += snprintf(out + len, size - len,
        "    \"info\": \"%s\"\n"
        "  },\n",
        escaped);
    
    /* Status */
    len += snprintf(out + len, size - len,
        "  \"status\": \"ok\"\n"
        "}\n");
    
    return (int)len;
}

/* Generate Prometheus metrics */
int generate_prometheus(char *out, size_t size) {
    char hostname[64] = "unknown";
    char kernel[64] = "unknown";
    char proc_count[32] = "0";
    size_t len = 0;
    unsigned long long timestamp;
    
    gethostname(hostname, sizeof(hostname) - 1);
    run_command("uname -r 2>/dev/null | tr -d '\n\r'", kernel, sizeof(kernel));
    run_command("pidin 2>/dev/null | wc -l | tr -d ' \n\r'", proc_count, sizeof(proc_count));
    
    timestamp = (unsigned long long)time(NULL);
    
    len += snprintf(out + len, size - len,
        "# HELP qnx_up QNX exporter is running\n"
        "# TYPE qnx_up gauge\n"
        "qnx_up 1\n\n"
        "# HELP qnx_info QNX system information\n"
        "# TYPE qnx_info gauge\n"
        "qnx_info{hostname=\"%s\",kernel=\"%s\"} 1\n\n"
        "# HELP qnx_time_seconds Current unix timestamp\n"
        "# TYPE qnx_time_seconds gauge\n"
        "qnx_time_seconds %llu\n\n"
        "# HELP qnx_processes_total Total number of processes\n"
        "# TYPE qnx_processes_total gauge\n"
        "qnx_processes_total %s\n",
        hostname, kernel, timestamp, 
        proc_count[0] ? proc_count : "0");
    
    return (int)len;
}

void send_response(int sock, int code, const char *status, 
                   const char *content_type, const char *body, size_t body_len) {
    char header[512];
    int hlen;
    
    hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        code, status, content_type, body_len);
    
    send(sock, header, hlen, 0);
    if (body && body_len > 0) {
        send(sock, body, body_len, 0);
    }
}

void* handle_client(void* arg) {
    int sock = *(int*)arg;
    char request[4096];
    char *response;
    int req_len, resp_len;
    
    free(arg);
    
    req_len = recv(sock, request, sizeof(request) - 1, 0);
    if (req_len <= 0) {
        close(sock);
        return NULL;
    }
    request[req_len] = '\0';
    
    printf("Request: %.60s...\n", request);
    
    /* Allocate response buffer */
    response = malloc(BUFFER_SIZE);
    if (!response) {
        const char *err = "{\"error\": \"out of memory\"}";
        send_response(sock, 500, "Error", "application/json", err, strlen(err));
        close(sock);
        return NULL;
    }
    
    /* Handle routes */
    if (strstr(request, "GET /health") || strstr(request, "GET /-/healthy")) {
        /* Health check */
        send_response(sock, 200, "OK", "text/plain", "OK", 2);
    }
    else if (strstr(request, "GET /metrics")) {
        /* Prometheus format */
        resp_len = generate_prometheus(response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "text/plain; version=0.0.4; charset=utf-8", 
                     response, resp_len);
    }
    else if (strstr(request, "GET /favicon")) {
        /* Ignore favicon */
        send_response(sock, 204, "No Content", "text/plain", "", 0);
    }
    else if (strstr(request, "GET /") || strstr(request, "GET / ")) {
        /* Root path - serve JSON */
        resp_len = generate_json(response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "application/json", response, resp_len);
    }
    else {
        /* 404 for anything else */
        const char *not_found = "{\"error\": \"not found\"}";
        send_response(sock, 404, "Not Found", "application/json", 
                     not_found, strlen(not_found));
    }
    
    free(response);
    close(sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    int server_fd, client_fd;
    int opt = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int i;
    
    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            printf("QNX Metrics Exporter\n\n");
            printf("Usage: %s [-p port]\n\n", argv[0]);
            printf("Endpoints:\n");
            printf("  /          JSON metrics (default)\n");
            printf("  /metrics   Prometheus format\n");
            printf("  /health    Health check\n");
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    
    printf("=====================================\n");
    printf("  QNX Metrics Exporter\n");
    printf("=====================================\n");
    printf("  JSON:       http://0.0.0.0:%d/\n", port);
    printf("  Prometheus: http://0.0.0.0:%d/metrics\n", port);
    printf("  Health:     http://0.0.0.0:%d/health\n", port);
    printf("=====================================\n\n");
    
    while (running) {
        int *client_ptr;
        pthread_t thread;
        
        client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        if (client_fd < 0) {
            if (running) perror("accept");
            continue;
        }
        
        printf("Connection from %s:%d\n", 
               inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        
        client_ptr = malloc(sizeof(int));
        if (!client_ptr) {
            close(client_fd);
            continue;
        }
        *client_ptr = client_fd;
        
        if (pthread_create(&thread, NULL, handle_client, client_ptr) != 0) {
            free(client_ptr);
            close(client_fd);
            continue;
        }
        pthread_detach(thread);
    }
    
    close(server_fd);
    printf("\nShutdown complete\n");
    return 0;
}