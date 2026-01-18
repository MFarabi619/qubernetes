#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#define PORT 9090
#define BUFFER_SIZE 65536
#define MAX_PROCESSES 100

volatile sig_atomic_t running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

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

void json_escape(const char *src, char *dest, size_t dest_size) {
    size_t i, j = 0;
    if (!dest || dest_size == 0) return;
    if (!src) { dest[0] = '\0'; return; }
    
    for (i = 0; src[i] && j < dest_size - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j < dest_size - 3) { dest[j++] = '\\'; dest[j++] = c; }
        } else if (c == '\n') {
            if (j < dest_size - 3) { dest[j++] = '\\'; dest[j++] = 'n'; }
        } else if (c == '\r') {
            /* skip */
        } else if (c == '\t') {
            if (j < dest_size - 3) { dest[j++] = '\\'; dest[j++] = 't'; }
        } else if ((unsigned char)c >= 32) {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

unsigned long long get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ============================================================================
 * Metric Collection Functions
 * ============================================================================ */

int get_process_count(void) {
    char buf[64];
    run_command("pidin 2>/dev/null | wc -l", buf, sizeof(buf));
    return atoi(buf);
}

int get_thread_count(void) {
    char buf[64];
    run_command("pidin -F \"%H\" 2>/dev/null | awk '{s+=$1} END {print s}'", buf, sizeof(buf));
    int count = atoi(buf);
    return count > 0 ? count : get_process_count();
}

unsigned long long get_memory_total(void) {
    char buf[256];
    unsigned long long total = 0;
    
    run_command("pidin info 2>/dev/null | grep -i 'total' | head -1", buf, sizeof(buf));
    /* Try to parse memory value */
    char *p = buf;
    while (*p) {
        if (isdigit(*p)) {
            unsigned long long val = strtoull(p, &p, 10);
            if (*p == 'G' || *p == 'g') total = val * 1024 * 1024 * 1024;
            else if (*p == 'M' || *p == 'm') total = val * 1024 * 1024;
            else if (*p == 'K' || *p == 'k') total = val * 1024;
            else if (val > 1000000) total = val;
            if (total > 0) break;
        }
        p++;
    }
    return total > 0 ? total : 1073741824ULL; /* Default 1GB */
}

unsigned long long get_memory_free(void) {
    char buf[256];
    unsigned long long free_mem = 0;
    
    run_command("pidin info 2>/dev/null | grep -i 'free' | head -1", buf, sizeof(buf));
    char *p = buf;
    while (*p) {
        if (isdigit(*p)) {
            unsigned long long val = strtoull(p, &p, 10);
            if (*p == 'G' || *p == 'g') free_mem = val * 1024 * 1024 * 1024;
            else if (*p == 'M' || *p == 'm') free_mem = val * 1024 * 1024;
            else if (*p == 'K' || *p == 'k') free_mem = val * 1024;
            else if (val > 1000000) free_mem = val;
            if (free_mem > 0) break;
        }
        p++;
    }
    return free_mem > 0 ? free_mem : 536870912ULL; /* Default 512MB */
}

double get_cpu_usage(void) {
    char buf[1024];
    double total_cpu = 0.0;
    
    if (run_command("hogs -i 1 -% 0.1 2>/dev/null | head -20", buf, sizeof(buf)) > 0) {
        char *line = strtok(buf, "\n");
        while (line) {
            double cpu;
            if (sscanf(line, "%*d %lf", &cpu) == 1) {
                total_cpu += cpu;
            }
            line = strtok(NULL, "\n");
        }
    }
    return total_cpu > 0 ? total_cpu : 5.0; /* Default 5% */
}

int get_disk_usage_percent(const char *mount) {
    char cmd[256], buf[256];
    int usage = 0;
    
    snprintf(cmd, sizeof(cmd), "df %s 2>/dev/null | tail -1 | awk '{print $5}' | tr -d '%%'", 
             mount ? mount : "/");
    run_command(cmd, buf, sizeof(buf));
    usage = atoi(buf);
    return usage > 0 ? usage : 0;
}

/* ============================================================================
 * Grafana JSON API Response Generators
 * ============================================================================ */

/* GET / - Health check and metric list */
int generate_metric_list(char *out, size_t size) {
    return snprintf(out, size,
        "[\n"
        "  {\n"
        "    \"label\": \"CPU Usage\",\n"
        "    \"value\": \"cpu_usage\",\n"
        "    \"payloads\": []\n"
        "  },\n"
        "  {\n"
        "    \"label\": \"Memory Used\",\n"
        "    \"value\": \"memory_used\",\n"
        "    \"payloads\": []\n"
        "  },\n"
        "  {\n"
        "    \"label\": \"Memory Free\",\n"
        "    \"value\": \"memory_free\",\n"
        "    \"payloads\": []\n"
        "  },\n"
        "  {\n"
        "    \"label\": \"Memory Usage Percent\",\n"
        "    \"value\": \"memory_percent\",\n"
        "    \"payloads\": []\n"
        "  },\n"
        "  {\n"
        "    \"label\": \"Process Count\",\n"
        "    \"value\": \"process_count\",\n"
        "    \"payloads\": []\n"
        "  },\n"
        "  {\n"
        "    \"label\": \"Thread Count\",\n"
        "    \"value\": \"thread_count\",\n"
        "    \"payloads\": []\n"
        "  },\n"
        "  {\n"
        "    \"label\": \"Disk Usage\",\n"
        "    \"value\": \"disk_usage\",\n"
        "    \"payloads\": [\n"
        "      {\n"
        "        \"label\": \"Mount Point\",\n"
        "        \"name\": \"mount\",\n"
        "        \"type\": \"select\",\n"
        "        \"placeholder\": \"Select mount point\",\n"
        "        \"options\": [\n"
        "          {\"label\": \"/\", \"value\": \"/\"},\n"
        "          {\"label\": \"/tmp\", \"value\": \"/tmp\"},\n"
        "          {\"label\": \"/home\", \"value\": \"/home\"}\n"
        "        ]\n"
        "      }\n"
        "    ]\n"
        "  },\n"
        "  {\n"
        "    \"label\": \"System Info\",\n"
        "    \"value\": \"system_info\",\n"
        "    \"payloads\": []\n"
        "  }\n"
        "]\n");
}

/* POST /query - Return metric data */
int generate_query_response(const char *request_body, char *out, size_t size) {
    unsigned long long ts = get_timestamp_ms();
    unsigned long long mem_total, mem_free, mem_used;
    double mem_percent, cpu_usage;
    int proc_count, thread_count, disk_pct;
    char hostname[64] = "unknown";
    char kernel[64] = "unknown";
    size_t len = 0;
    
    /* Collect metrics */
    gethostname(hostname, sizeof(hostname) - 1);
    run_command("uname -r 2>/dev/null | tr -d '\n\r'", kernel, sizeof(kernel));
    
    mem_total = get_memory_total();
    mem_free = get_memory_free();
    mem_used = mem_total > mem_free ? mem_total - mem_free : 0;
    mem_percent = mem_total > 0 ? (double)mem_used / mem_total * 100.0 : 0.0;
    cpu_usage = get_cpu_usage();
    proc_count = get_process_count();
    thread_count = get_thread_count();
    disk_pct = get_disk_usage_percent("/");
    
    /* Check what metric is requested */
    int want_cpu = (strstr(request_body, "cpu_usage") != NULL);
    int want_mem_used = (strstr(request_body, "memory_used") != NULL);
    int want_mem_free = (strstr(request_body, "memory_free") != NULL);
    int want_mem_pct = (strstr(request_body, "memory_percent") != NULL);
    int want_proc = (strstr(request_body, "process_count") != NULL);
    int want_thread = (strstr(request_body, "thread_count") != NULL);
    int want_disk = (strstr(request_body, "disk_usage") != NULL);
    int want_info = (strstr(request_body, "system_info") != NULL);
    int want_all = (!want_cpu && !want_mem_used && !want_mem_free && 
                    !want_mem_pct && !want_proc && !want_thread && 
                    !want_disk && !want_info);
    
    len += snprintf(out + len, size - len, "[\n");
    
    int first = 1;
    
    /* CPU Usage */
    if (want_cpu || want_all) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"target\": \"cpu_usage\",\n"
            "    \"datapoints\": [\n"
            "      [%.2f, %llu]\n"
            "    ]\n"
            "  }",
            cpu_usage, ts);
    }
    
    /* Memory Used */
    if (want_mem_used || want_all) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"target\": \"memory_used\",\n"
            "    \"datapoints\": [\n"
            "      [%llu, %llu]\n"
            "    ]\n"
            "  }",
            mem_used, ts);
    }
    
    /* Memory Free */
    if (want_mem_free || want_all) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"target\": \"memory_free\",\n"
            "    \"datapoints\": [\n"
            "      [%llu, %llu]\n"
            "    ]\n"
            "  }",
            mem_free, ts);
    }
    
    /* Memory Percent */
    if (want_mem_pct || want_all) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"target\": \"memory_percent\",\n"
            "    \"datapoints\": [\n"
            "      [%.2f, %llu]\n"
            "    ]\n"
            "  }",
            mem_percent, ts);
    }
    
    /* Process Count */
    if (want_proc || want_all) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"target\": \"process_count\",\n"
            "    \"datapoints\": [\n"
            "      [%d, %llu]\n"
            "    ]\n"
            "  }",
            proc_count, ts);
    }
    
    /* Thread Count */
    if (want_thread || want_all) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"target\": \"thread_count\",\n"
            "    \"datapoints\": [\n"
            "      [%d, %llu]\n"
            "    ]\n"
            "  }",
            thread_count, ts);
    }
    
    /* Disk Usage */
    if (want_disk || want_all) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"target\": \"disk_usage\",\n"
            "    \"datapoints\": [\n"
            "      [%d, %llu]\n"
            "    ]\n"
            "  }",
            disk_pct, ts);
    }
    
    /* System Info - as table */
    if (want_info) {
        if (!first) len += snprintf(out + len, size - len, ",\n");
        first = 0;
        len += snprintf(out + len, size - len,
            "  {\n"
            "    \"type\": \"table\",\n"
            "    \"columns\": [\n"
            "      {\"text\": \"Property\", \"type\": \"string\"},\n"
            "      {\"text\": \"Value\", \"type\": \"string\"}\n"
            "    ],\n"
            "    \"rows\": [\n"
            "      [\"Hostname\", \"%s\"],\n"
            "      [\"Kernel\", \"%s\"],\n"
            "      [\"Processes\", \"%d\"],\n"
            "      [\"Threads\", \"%d\"],\n"
            "      [\"Memory Total\", \"%llu MB\"],\n"
            "      [\"Memory Free\", \"%llu MB\"],\n"
            "      [\"CPU Usage\", \"%.1f%%\"],\n"
            "      [\"Disk Usage\", \"%d%%\"]\n"
            "    ]\n"
            "  }",
            hostname, kernel, proc_count, thread_count,
            mem_total / (1024*1024), mem_free / (1024*1024),
            cpu_usage, disk_pct);
    }
    
    len += snprintf(out + len, size - len, "\n]\n");
    
    return (int)len;
}

/* POST /search - Return available metrics (alternative endpoint) */
int generate_search_response(char *out, size_t size) {
    return snprintf(out, size,
        "[\"cpu_usage\", \"memory_used\", \"memory_free\", \"memory_percent\", "
        "\"process_count\", \"thread_count\", \"disk_usage\", \"system_info\"]\n");
}

/* POST /metric-payload-options - Return payload options */
int generate_payload_options(const char *request_body, char *out, size_t size) {
    /* Check what payload is being requested */
    if (strstr(request_body, "\"name\":\"mount\"") || strstr(request_body, "\"name\": \"mount\"")) {
        /* Return mount point options */
        char buf[2048];
        size_t len = 0;
        
        len += snprintf(out + len, size - len, "[\n");
        
        /* Get actual mount points from df */
        if (run_command("df 2>/dev/null | tail -n +2 | awk '{print $NF}'", buf, sizeof(buf)) > 0) {
            char *line = strtok(buf, "\n");
            int first = 1;
            while (line && len < size - 200) {
                char *trimmed = line;
                while (*trimmed && isspace(*trimmed)) trimmed++;
                if (strlen(trimmed) > 0) {
                    if (!first) len += snprintf(out + len, size - len, ",\n");
                    first = 0;
                    char escaped[256];
                    json_escape(trimmed, escaped, sizeof(escaped));
                    len += snprintf(out + len, size - len,
                        "  {\"label\": \"%s\", \"value\": \"%s\"}",
                        escaped, escaped);
                }
                line = strtok(NULL, "\n");
            }
        }
        
        if (len == 2) {
            /* No mount points found, add default */
            len += snprintf(out + len, size - len,
                "  {\"label\": \"/\", \"value\": \"/\"}");
        }
        
        len += snprintf(out + len, size - len, "\n]\n");
        return (int)len;
    }
    
    /* Default empty options */
    return snprintf(out, size, "[]\n");
}

/* POST /tag-keys - For ad-hoc filtering */
int generate_tag_keys(char *out, size_t size) {
    return snprintf(out, size,
        "[{\"type\": \"string\", \"text\": \"hostname\"}, "
        "{\"type\": \"string\", \"text\": \"metric\"}]\n");
}

/* POST /tag-values - For ad-hoc filtering */
int generate_tag_values(const char *request_body, char *out, size_t size) {
    char hostname[64] = "unknown";
    gethostname(hostname, sizeof(hostname) - 1);
    
    if (strstr(request_body, "hostname")) {
        return snprintf(out, size, "[{\"text\": \"%s\"}]\n", hostname);
    }
    if (strstr(request_body, "metric")) {
        return snprintf(out, size,
            "[{\"text\": \"cpu_usage\"}, {\"text\": \"memory_used\"}, "
            "{\"text\": \"memory_free\"}, {\"text\": \"process_count\"}]\n");
    }
    return snprintf(out, size, "[]\n");
}

/* ============================================================================
 * HTTP Handling
 * ============================================================================ */

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
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "\r\n",
        code, status, content_type, body_len);
    
    send(sock, header, hlen, 0);
    if (body && body_len > 0) {
        send(sock, body, body_len, 0);
    }
}

/* Extract request body from HTTP request */
char* get_request_body(char *request) {
    char *body = strstr(request, "\r\n\r\n");
    if (body) {
        return body + 4;
    }
    body = strstr(request, "\n\n");
    if (body) {
        return body + 2;
    }
    return "";
}

void* handle_client(void* arg) {
    int sock = *(int*)arg;
    char request[BUFFER_SIZE];
    char *response;
    int req_len, resp_len;
    char *body;
    
    free(arg);
    
    req_len = recv(sock, request, sizeof(request) - 1, 0);
    if (req_len <= 0) {
        close(sock);
        return NULL;
    }
    request[req_len] = '\0';
    
    printf("Request: %.100s...\n", request);
    
    /* Get request body */
    body = get_request_body(request);
    
    /* Allocate response buffer */
    response = malloc(BUFFER_SIZE);
    if (!response) {
        const char *err = "{\"error\": \"out of memory\"}";
        send_response(sock, 500, "Error", "application/json", err, strlen(err));
        close(sock);
        return NULL;
    }
    
    /* Handle CORS preflight */
    if (strstr(request, "OPTIONS ")) {
        send_response(sock, 204, "No Content", "text/plain", "", 0);
        free(response);
        close(sock);
        return NULL;
    }
    
    /* Route handling */
    if (strstr(request, "POST /query")) {
        /* Grafana query endpoint */
        resp_len = generate_query_response(body, response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "application/json", response, resp_len);
    }
    else if (strstr(request, "POST /search")) {
        /* Grafana search endpoint */
        resp_len = generate_search_response(response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "application/json", response, resp_len);
    }
    else if (strstr(request, "POST /metric-payload-options")) {
        /* Payload options for dropdowns */
        resp_len = generate_payload_options(body, response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "application/json", response, resp_len);
    }
    else if (strstr(request, "POST /tag-keys")) {
        resp_len = generate_tag_keys(response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "application/json", response, resp_len);
    }
    else if (strstr(request, "POST /tag-values")) {
        resp_len = generate_tag_values(body, response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "application/json", response, resp_len);
    }
    else if (strstr(request, "GET /health") || strstr(request, "GET /-/healthy")) {
        send_response(sock, 200, "OK", "text/plain", "OK", 2);
    }
    else if (strstr(request, "GET /favicon")) {
        send_response(sock, 204, "No Content", "text/plain", "", 0);
    }
    else if (strstr(request, "GET /") || strstr(request, "POST /")) {
        /* Root path - return metric list (Grafana JSON datasource format) */
        resp_len = generate_metric_list(response, BUFFER_SIZE);
        send_response(sock, 200, "OK", "application/json", response, resp_len);
    }
    else {
        const char *not_found = "{\"error\": \"not found\"}";
        send_response(sock, 404, "Not Found", "application/json", 
                     not_found, strlen(not_found));
    }
    
    free(response);
    close(sock);
    return NULL;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    int port = PORT;
    int server_fd, client_fd;
    int opt = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int i;
    
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            printf("QNX Grafana JSON Datasource Exporter\n\n");
            printf("Usage: %s [-p port]\n\n", argv[0]);
            printf("Grafana JSON Datasource Endpoints:\n");
            printf("  GET/POST /                    Metric list\n");
            printf("  POST /query                   Query metrics\n");
            printf("  POST /search                  Search metrics\n");
            printf("  POST /metric-payload-options  Payload options\n");
            printf("  GET /health                   Health check\n\n");
            printf("Available Metrics:\n");
            printf("  cpu_usage       - CPU usage percentage\n");
            printf("  memory_used     - Used memory in bytes\n");
            printf("  memory_free     - Free memory in bytes\n");
            printf("  memory_percent  - Memory usage percentage\n");
            printf("  process_count   - Number of processes\n");
            printf("  thread_count    - Number of threads\n");
            printf("  disk_usage      - Disk usage percentage\n");
            printf("  system_info     - System information table\n");
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
    
    printf("================================================\n");
    printf("  QNX Grafana JSON Datasource Exporter\n");
    printf("================================================\n");
    printf("  URL: http://0.0.0.0:%d\n", port);
    printf("================================================\n");
    printf("  Grafana Setup:\n");
    printf("  1. Add JSON datasource\n");
    printf("  2. URL: http://<qnx-ip>:%d\n", port);
    printf("  3. Save & Test\n");
    printf("================================================\n\n");
    
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