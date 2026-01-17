#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>

#define PORT 9090
#define BUFFER_SIZE 4096

typedef struct {
    double cpu_used;
    long long mem_used_bytes;
    time_t start_time;
    pthread_mutex_t lock;
} metrics_cache_t;

static metrics_cache_t metrics_cache = {
    .cpu_used = 0.0,
    .mem_used_bytes = 0,
    .start_time = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static int get_cpu_mem_from_top(double *cpu_used, long long *mem_used_bytes);

void generate_metrics(char *buffer, size_t size) {
    time_t now = time(NULL);

    pthread_mutex_lock(&metrics_cache.lock);
    double cpu = metrics_cache.cpu_used;
    long long mem = metrics_cache.mem_used_bytes;
    time_t start = metrics_cache.start_time;
    pthread_mutex_unlock(&metrics_cache.lock);

    long uptime = start ? (now - start) : 0;

    snprintf(buffer, size,
        "# HELP process_uptime_seconds Process uptime\n"
        "# TYPE process_uptime_seconds counter\n"
        "process_uptime_seconds %ld\n\n"

        "# HELP cpu_usage_percent CPU usage percent\n"
        "# TYPE cpu_usage_percent gauge\n"
        "cpu_usage_percent %.2f\n\n"

        "# HELP memory_usage_bytes Memory usage in bytes\n"
        "# TYPE memory_usage_bytes gauge\n"
        "memory_usage_bytes %lld\n\n"

        "# HELP current_timestamp Unix timestamp\n"
        "# TYPE current_timestamp gauge\n"
        "current_timestamp %ld\n",
        uptime, cpu, mem, now
    );
}

void update_metrics_cache(void) {
    double cpu = 0.0;
    long long mem = 0;

    if (get_cpu_mem_from_top(&cpu, &mem) == 0) {
        pthread_mutex_lock(&metrics_cache.lock);
        metrics_cache.cpu_used = cpu;
        metrics_cache.mem_used_bytes = mem;
        if (metrics_cache.start_time == 0)
            metrics_cache.start_time = time(NULL);
        pthread_mutex_unlock(&metrics_cache.lock);
    }
}

void* metrics_update_thread(void *arg) {
    (void)arg;
    while (1) {
        update_metrics_cache();
        sleep(2);
    }
    return NULL;
}

static int get_cpu_mem_from_top(double *cpu_used, long long *mem_used_bytes) {
    FILE *fp = popen("top", "r");
    if (!fp) return -1;

    char line[512];
    double idle_sum = 0.0;
    int idle_count = 0;

    double total_mem = -1.0;
    double avail_mem = -1.0;

    while (fgets(line, sizeof(line), fp)) {
        // strip leading spaces
        char *p = line;
        while (isspace(*p)) p++;

        // Memory line
        if (strncmp(p, "Memory:", 7) == 0) {
            double t = 0.0, a = 0.0;
            char tu = 0, au = 0;

            // Example: Memory: 8128M total, 7628M avail, page size 4K
            if (sscanf(p, "Memory: %lf%c total, %lf%c avail", &t, &tu, &a, &au) >= 3) {
                double mul_t = (tu == 'G') ? 1024.0*1024.0*1024.0 :
                               (tu == 'M') ? 1024.0*1024.0 :
                               1.0;

                double mul_a = (au == 'G') ? 1024.0*1024.0*1024.0 :
                               (au == 'M') ? 1024.0*1024.0 :
                               1.0;

                total_mem = t * mul_t;
                avail_mem = a * mul_a;
            }
        }

        // CPU Idle lines:
        // CPU  0:     99.8%     99%     99%     99%
        if (strncmp(p, "CPU", 3) == 0) {
            int cpu_num;
            double idle_val = 0.0;

            // parse first number after colon (idle %)
            char *colon = strchr(p, ':');
            if (colon) {
                if (sscanf(colon + 1, "%lf", &idle_val) == 1) {
                    idle_sum += idle_val;
                    idle_count++;
                }
            }
        }
    }

    pclose(fp);

    if (idle_count <= 0) return -1;

    double avg_idle = idle_sum / idle_count;
    *cpu_used = 100.0 - avg_idle;

    if (total_mem >= 0 && avail_mem >= 0) {
        *mem_used_bytes = (long long)(total_mem - avail_mem);
        return 0;
    }

    return -1;
}

void handle_connection(int c) {
    char buf[BUFFER_SIZE];
    int r = read(c, buf, sizeof(buf)-1);
    if (r <= 0) { close(c); return; }
    buf[r] = '\0';

    if (strncmp(buf, "GET /metrics", 12) == 0) {
        char metrics[BUFFER_SIZE];
        char resp[BUFFER_SIZE*2];
        generate_metrics(metrics, sizeof(metrics));

        snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; version=0.0.4\r\n"
            "Content-Length: %zu\r\n\r\n%s",
            strlen(metrics), metrics);

        write(c, resp, strlen(resp));
    }
    close(c);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(PORT);

    bind(s, (struct sockaddr*)&a, sizeof(a));
    listen(s, 5);

    pthread_t tid;
    pthread_create(&tid, NULL, metrics_update_thread, NULL);

    printf("Metrics: http://<ip>:%d/metrics\n", PORT);

    while (1) {
        int c = accept(s, NULL, NULL);
        if (c >= 0) handle_connection(c);
    }
}
