Source the correct compiler

source ~/qnx800/qnxsdp-env.sh

Compile

qcc -Vgcc_ntoaarch64le -o metrics_server metrics_server.c -lsocket
qcc -Vgcc_ntoaarch64le -o metrics_json  metrics_json.c -lsocket -lcrypto

Reverse Proxy to send it to the QNX

vim /system/etc/startup/post_startup.sh

scp jissa@10.0.198:~/Downloads/qubernetes/qnx_server/metrics_json  ~/server