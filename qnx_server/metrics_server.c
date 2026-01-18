#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>      /* Added for fd_set, FD_ZERO, FD_SET, FD_ISSET, select */
#include <sys/time.h>        /* Added for struct timeval */
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define PORT 9090
#define BUFFER_SIZE 65536
#define OUTPUT_BUFFER_SIZE 131072
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define REFRESH_INTERVAL 2

volatile sig_atomic_t running = 1;

/* Signal handler - must be defined before main() */
void signal_handler(int sig) {
    (void)sig;  /* Suppress unused parameter warning */
    running = 0;
}

/* Command structure for QNX system info gathering */
typedef struct {
    const char *name;
    const char *command;
    const char *description;
    int enabled;
} SystemCommand;

/* QNX-specific commands for comprehensive system monitoring */
SystemCommand qnx_commands[] = {
    /* Process and CPU information */
    {"PROCESS_LIST", "pidin -F \"%N %a %b %J %B %H %I %10L %50n\"", "Process List (PID, Args, PGrp, State, Blocked, Threads, FDs, CPU, Name)", 1},
    {"CPU_HOGS", "hogs -i 1 -% 0.1 2>/dev/null || pidin times", "CPU Usage by Process", 1},
    {"THREAD_INFO", "pidin -F \"%N %I %J %l %H %55h\"", "Thread Information", 1},
    
    /* Memory information */
    {"MEMORY_OVERVIEW", "pidin info | head -20", "System Memory Overview", 1},
    {"MEMORY_DETAILED", "showmem -P 2>/dev/null || pidin mem", "Detailed Memory Usage", 1},
    {"SYSPAGE_MEM", "pidin syspage=asinfo 2>/dev/null", "System Page Memory Info", 0},
    
    /* System information */
    {"SYSTEM_INFO", "pidin syspage=system 2>/dev/null || uname -a", "System Information", 1},
    {"HARDWARE_INFO", "pidin syspage=hwinfo 2>/dev/null", "Hardware Information", 0},
    {"CPU_INFO", "pidin syspage=cpuinfo 2>/dev/null", "CPU Information", 1},
    
    /* Network information */
    {"NETWORK_STATS", "netstat -i 2>/dev/null", "Network Interface Statistics", 1},
    {"NETWORK_CONN", "netstat -an 2>/dev/null | head -30", "Network Connections", 0},
    
    /* I/O and device information */
    {"DISK_USAGE", "df -h 2>/dev/null || df", "Disk Usage", 1},
    {"MOUNT_INFO", "mount 2>/dev/null", "Mounted Filesystems", 0},
    
    /* Resource usage */
    {"FILE_DESCRIPTORS", "pidin -F \"%N %I %55o\" | head -50", "Open File Descriptors", 0},
    {"TIMERS", "pidin timers 2>/dev/null | head -30", "Active Timers", 0},
    {"CHANNELS", "pidin channels 2>/dev/null | head -30", "IPC Channels", 0},
    
    /* Interrupt and IRQ info */
    {"INTERRUPTS", "pidin irqs 2>/dev/null", "Interrupt Information", 0},
    
    /* QNX specific */
    {"PULSE_INFO", "pidin pulses 2>/dev/null | head -30", "Pulse Information", 0},
    {"SIN_INFO", "sin info 2>/dev/null", "System Information Node", 0},
    {"USE_INFO", "use -i 1 2>/dev/null || pidin -F \"%N %L\"", "Resource Usage", 0},
    
    {NULL, NULL, NULL, 0}
};

/* Base64 encode function */
char* base64_encode(const unsigned char* input, int length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    
    char *output = malloc(bufferPtr->length + 1);
    if (output) {
        memcpy(output, bufferPtr->data, bufferPtr->length);
        output[bufferPtr->length] = '\0';
    }
    
    BIO_free_all(bio);
    return output;
}

/* WebSocket handshake */
int websocket_handshake(int client_socket, char* request) {
    char *key_start = strstr(request, "Sec-WebSocket-Key: ");
    if (!key_start) return 0;
    
    key_start += 19;
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) return 0;
    
    int key_len = key_end - key_start;
    char key[256];
    strncpy(key, key_start, key_len);
    key[key_len] = '\0';
    
    char accept_key[512];
    snprintf(accept_key, sizeof(accept_key), "%s%s", key, WS_GUID);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)accept_key, strlen(accept_key), hash);
    
    char *accept_base64 = base64_encode(hash, SHA_DIGEST_LENGTH);
    if (!accept_base64) return 0;
    
    char response[1024];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_base64);
    
    send(client_socket, response, strlen(response), 0);
    free(accept_base64);
    return 1;
}

/* Send WebSocket frame */
int send_websocket_frame(int socket, const char* data, size_t len) {
    unsigned char frame[14];
    size_t frame_len = 0;
    
    frame[0] = 0x81; /* FIN bit + text frame */
    
    if (len <= 125) {
        frame[1] = (unsigned char)len;
        frame_len = 2;
    } else if (len <= 65535) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        frame_len = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[9 - i] = (len >> (i * 8)) & 0xFF;
        }
        frame_len = 10;
    }
    
    if (send(socket, frame, frame_len, 0) < 0) return -1;
    if (send(socket, data, len, 0) < 0) return -1;
    return 0;
}

/* Execute a command and capture output */
int execute_command(const char *cmd, char *output, size_t output_size) {
    FILE *fp;
    size_t total_read = 0;
    char buffer[4096];
    
    fp = popen(cmd, "r");
    if (fp == NULL) {
        snprintf(output, output_size, "[Command failed: %s]\n", cmd);
        return -1;
    }
    
    output[0] = '\0';
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        if (total_read + len < output_size - 1) {
            strcat(output, buffer);
            total_read += len;
        } else {
            break;
        }
    }
    
    pclose(fp);
    return (int)total_read;
}

/* Get current timestamp */
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Collect all QNX system metrics */
int collect_qnx_metrics(char *output, size_t output_size, int view_mode) {
    char section_output[BUFFER_SIZE];
    char timestamp[64];
    size_t total_len = 0;
    int i;
    
    get_timestamp(timestamp, sizeof(timestamp));
    
    /* Header */
    total_len += snprintf(output + total_len, output_size - total_len,
        "======================================================================\n"
        "                  QNX NEUTRINO SYSTEM MONITOR\n"
        "                  Timestamp: %s\n"
        "======================================================================\n\n",
        timestamp);
    
    /* Execute enabled commands based on view mode */
    for (i = 0; qnx_commands[i].name != NULL; i++) {
        /* In full view mode (1), show all commands */
        /* In standard view mode (0), show only enabled commands */
        if (view_mode == 1 || qnx_commands[i].enabled) {
            total_len += snprintf(output + total_len, output_size - total_len,
                "----------------------------------------------------------------------\n"
                " %s\n"
                " %s\n"
                "----------------------------------------------------------------------\n",
                qnx_commands[i].name,
                qnx_commands[i].description);
            
            if (execute_command(qnx_commands[i].command, section_output, sizeof(section_output)) >= 0) {
                size_t section_len = strlen(section_output);
                if (total_len + section_len < output_size - 100) {
                    total_len += snprintf(output + total_len, output_size - total_len, "%s\n", section_output);
                }
            }
            
            total_len += snprintf(output + total_len, output_size - total_len, "\n");
            
            if (total_len >= output_size - 1000) break;
        }
    }
    
    /* Footer */
    total_len += snprintf(output + total_len, output_size - total_len,
        "======================================================================\n"
        "                    Refresh every %d seconds\n"
        "======================================================================\n",
        REFRESH_INTERVAL);
    
    return (int)total_len;
}

/* Continuous top monitoring mode */
int run_top_continuous(int client_socket) {
    int pipefd[2];
    pid_t pid;
    char buffer[BUFFER_SIZE];
    
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }
    
    pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        /* Child process: run top in batch mode */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        /* QNX top command - try different options */
        execlp("top", "top", "-b", "-d", "1", NULL);
        
        /* Fallback to hogs if top doesn't work */
        execlp("hogs", "hogs", "-i", "1", NULL);
        
        /* Last fallback - pidin in a loop would need a script */
        perror("execlp failed");
        _exit(1);
    }
    
    /* Parent process */
    close(pipefd[1]);
    
    /* Set non-blocking read */
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    
    while (running) {
        fd_set read_fds;
        struct timeval tv;
        int ret;
        int bytes_read;
        int status;
        
        FD_ZERO(&read_fds);
        FD_SET(pipefd[0], &read_fds);
        
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        ret = select(pipefd[0] + 1, &read_fds, NULL, NULL, &tv);
        
        if (ret > 0 && FD_ISSET(pipefd[0], &read_fds)) {
            bytes_read = read(pipefd[0], buffer, BUFFER_SIZE - 1);
            if (bytes_read <= 0) {
                if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    break;
                }
                continue;
            }
            
            buffer[bytes_read] = '\0';
            if (send_websocket_frame(client_socket, buffer, bytes_read) < 0) {
                break;
            }
        } else if (ret < 0 && errno != EINTR) {
            break;
        }
        
        /* Check if child is still running */
        if (waitpid(pid, &status, WNOHANG) != 0) {
            break;
        }
    }
    
    close(pipefd[0]);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    
    return 0;
}

/* Thread function to handle client */
void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    int is_metrics, is_top, is_full, is_root;
    
    free(arg);
    
    bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_socket);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';
    printf("Received request:\n%s\n", buffer);
    
    /* Route handling */
    is_metrics = (strstr(buffer, "GET /metrics") != NULL);
    is_top = (strstr(buffer, "GET /top") != NULL);
    is_full = (strstr(buffer, "GET /full") != NULL);
    is_root = (strstr(buffer, "GET / ") != NULL) || (strstr(buffer, "GET / HTTP") != NULL);
    
    if (!is_metrics && !is_top && !is_full && !is_root) {
        const char* error_msg = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
        close(client_socket);
        return NULL;
    }
    
    /* Check if this is a WebSocket upgrade request */
    if (strstr(buffer, "Upgrade: websocket") == NULL) {
        /* Serve HTML page */
        const char* html_page = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>QNX System Monitor</title>\n"
            "    <meta charset='UTF-8'>\n"
            "    <style>\n"
            "        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
            "        body {\n"
            "            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);\n"
            "            color: #00ff88;\n"
            "            font-family: 'Courier New', monospace;\n"
            "            min-height: 100vh;\n"
            "            padding: 20px;\n"
            "        }\n"
            "        .header {\n"
            "            text-align: center;\n"
            "            padding: 20px;\n"
            "            border-bottom: 2px solid #00ff88;\n"
            "            margin-bottom: 20px;\n"
            "        }\n"
            "        h1 { color: #00ffff; text-shadow: 0 0 10px #00ffff; }\n"
            "        .nav {\n"
            "            display: flex;\n"
            "            justify-content: center;\n"
            "            gap: 20px;\n"
            "            margin: 20px 0;\n"
            "            flex-wrap: wrap;\n"
            "        }\n"
            "        .nav button {\n"
            "            background: #0a3d62;\n"
            "            color: #00ff88;\n"
            "            border: 1px solid #00ff88;\n"
            "            padding: 10px 25px;\n"
            "            font-family: 'Courier New', monospace;\n"
            "            font-size: 14px;\n"
            "            cursor: pointer;\n"
            "            transition: all 0.3s;\n"
            "        }\n"
            "        .nav button:hover, .nav button.active {\n"
            "            background: #00ff88;\n"
            "            color: #1a1a2e;\n"
            "        }\n"
            "        .status {\n"
            "            text-align: center;\n"
            "            padding: 10px;\n"
            "            margin-bottom: 10px;\n"
            "        }\n"
            "        .status.connected { color: #00ff88; }\n"
            "        .status.disconnected { color: #ff4444; }\n"
            "        .status.connecting { color: #ffff00; }\n"
            "        #output {\n"
            "            background: rgba(0, 0, 0, 0.7);\n"
            "            border: 1px solid #00ff88;\n"
            "            border-radius: 5px;\n"
            "            padding: 15px;\n"
            "            white-space: pre;\n"
            "            overflow-x: auto;\n"
            "            font-size: 12px;\n"
            "            line-height: 1.4;\n"
            "            min-height: 500px;\n"
            "            max-height: 80vh;\n"
            "            overflow-y: auto;\n"
            "        }\n"
            "        .legend {\n"
            "            margin-top: 20px;\n"
            "            padding: 15px;\n"
            "            background: rgba(0, 0, 0, 0.5);\n"
            "            border: 1px solid #444;\n"
            "            border-radius: 5px;\n"
            "        }\n"
            "        .legend h3 { color: #00ffff; margin-bottom: 10px; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class='header'>\n"
            "        <h1>QNX NEUTRINO SYSTEM MONITOR</h1>\n"
            "        <p>Real-time system metrics via WebSocket</p>\n"
            "    </div>\n"
            "    \n"
            "    <div class='nav'>\n"
            "        <button onclick=\"connect('/metrics')\" id='btn-metrics'>Standard Metrics</button>\n"
            "        <button onclick=\"connect('/full')\" id='btn-full'>Full Metrics</button>\n"
            "        <button onclick=\"connect('/top')\" id='btn-top'>Live Top</button>\n"
            "        <button onclick=\"disconnect()\" id='btn-disconnect'>Stop</button>\n"
            "    </div>\n"
            "    \n"
            "    <div class='status' id='status'>Click a button to start monitoring</div>\n"
            "    <pre id='output'>Waiting for connection...</pre>\n"
            "    \n"
            "    <div class='legend'>\n"
            "        <h3>Available Endpoints</h3>\n"
            "        <p>/metrics - Standard system overview</p>\n"
            "        <p>/full - Extended metrics (all data)</p>\n"
            "        <p>/top - Continuous top output</p>\n"
            "    </div>\n"
            "    \n"
            "    <script>\n"
            "        var ws = null;\n"
            "        var currentEndpoint = '';\n"
            "        \n"
            "        function updateStatus(text, className) {\n"
            "            var status = document.getElementById('status');\n"
            "            status.textContent = text;\n"
            "            status.className = 'status ' + className;\n"
            "        }\n"
            "        \n"
            "        function setActiveButton(endpoint) {\n"
            "            var buttons = document.querySelectorAll('.nav button');\n"
            "            for (var i = 0; i < buttons.length; i++) {\n"
            "                buttons[i].classList.remove('active');\n"
            "            }\n"
            "            if (endpoint === '/metrics') document.getElementById('btn-metrics').classList.add('active');\n"
            "            else if (endpoint === '/full') document.getElementById('btn-full').classList.add('active');\n"
            "            else if (endpoint === '/top') document.getElementById('btn-top').classList.add('active');\n"
            "        }\n"
            "        \n"
            "        function connect(endpoint) {\n"
            "            if (ws) ws.close();\n"
            "            \n"
            "            currentEndpoint = endpoint;\n"
            "            setActiveButton(endpoint);\n"
            "            updateStatus('Connecting to ' + endpoint + '...', 'connecting');\n"
            "            document.getElementById('output').textContent = 'Connecting...';\n"
            "            \n"
            "            ws = new WebSocket('ws://' + window.location.host + endpoint);\n"
            "            \n"
            "            ws.onopen = function() {\n"
            "                updateStatus('Connected to ' + endpoint, 'connected');\n"
            "            };\n"
            "            \n"
            "            ws.onmessage = function(event) {\n"
            "                document.getElementById('output').textContent = event.data;\n"
            "            };\n"
            "            \n"
            "            ws.onerror = function(error) {\n"
            "                updateStatus('Connection error', 'disconnected');\n"
            "            };\n"
            "            \n"
            "            ws.onclose = function() {\n"
            "                updateStatus('Disconnected', 'disconnected');\n"
            "                setActiveButton('');\n"
            "            };\n"
            "        }\n"
            "        \n"
            "        function disconnect() {\n"
            "            if (ws) {\n"
            "                ws.close();\n"
            "                ws = null;\n"
            "            }\n"
            "            setActiveButton('');\n"
            "            updateStatus('Disconnected', 'disconnected');\n"
            "            document.getElementById('output').textContent = 'Click a button to start';\n"
            "        }\n"
            "    </script>\n"
            "</body>\n"
            "</html>";
        
        send(client_socket, html_page, strlen(html_page), 0);
        close(client_socket);
        return NULL;
    }
    
    /* Perform WebSocket handshake */
    if (!websocket_handshake(client_socket, buffer)) {
        printf("WebSocket handshake failed\n");
        close(client_socket);
        return NULL;
    }
    
    printf("WebSocket client connected, handshake complete\n");
    
    /* Handle /top endpoint with continuous streaming */
    if (is_top) {
        printf("Starting continuous top monitoring\n");
        run_top_continuous(client_socket);
        close(client_socket);
        printf("Top monitoring client disconnected\n");
        return NULL;
    }
    
    /* Handle /metrics or /full with periodic polling */
    {
        int view_mode = is_full ? 1 : 0;
        char *output = malloc(OUTPUT_BUFFER_SIZE);
        
        if (!output) {
            perror("malloc");
            close(client_socket);
            return NULL;
        }
        
        printf("Starting %s metrics monitoring\n", is_full ? "full" : "standard");
        
        while (running) {
            collect_qnx_metrics(output, OUTPUT_BUFFER_SIZE, view_mode);
            
            if (send_websocket_frame(client_socket, output, strlen(output)) < 0) {
                printf("Failed to send data, client disconnected\n");
                break;
            }
            
            sleep(REFRESH_INTERVAL);
        }
        
        free(output);
    }
    
    close(client_socket);
    printf("Metrics client disconnected\n");
    
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    int server_socket;
    int opt = 1;
    struct sockaddr_in server_addr;
    int i;
    
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("QNX System Monitor WebSocket Server\n\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -p PORT    Listen on specified port (default: %d)\n", PORT);
            printf("  -h         Show this help message\n\n");
            printf("Endpoints:\n");
            printf("  /          Web interface\n");
            printf("  /metrics   Standard system metrics (WebSocket)\n");
            printf("  /full      Extended metrics (WebSocket)\n");
            printf("  /top       Continuous top output (WebSocket)\n");
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        return 1;
    }
    
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        return 1;
    }
    
    if (listen(server_socket, 10) == -1) {
        perror("listen");
        close(server_socket);
        return 1;
    }
    
    printf("======================================================\n");
    printf("     QNX Neutrino System Monitor WebSocket Server\n");
    printf("======================================================\n");
    printf("  Web Interface:  http://localhost:%d\n", port);
    printf("  Metrics WS:     ws://localhost:%d/metrics\n", port);
    printf("  Full Metrics:   ws://localhost:%d/full\n", port);
    printf("  Live Top:       ws://localhost:%d/top\n", port);
    printf("======================================================\n");
    
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket;
        int *client_sock_ptr;
        pthread_t thread;
        
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == -1) {
            if (running) perror("accept");
            continue;
        }
        
        printf("New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));
        
        client_sock_ptr = malloc(sizeof(int));
        if (!client_sock_ptr) {
            close(client_socket);
            continue;
        }
        *client_sock_ptr = client_socket;
        
        if (pthread_create(&thread, NULL, handle_client, client_sock_ptr) != 0) {
            perror("pthread_create");
            free(client_sock_ptr);
            close(client_socket);
            continue;
        }
        pthread_detach(thread);
    }
    
    close(server_socket);
    printf("\nServer shut down\n");
    return 0;
}