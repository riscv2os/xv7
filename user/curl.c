#include "kernel/types.h"
#include "user/user.h"
#include "kernel/net/socket.h"

int main(int argc, char *argv[])
{
    int soc;
    struct sockaddr_in server;
    char buf[4096];
    char request[256];
    int i, port = 8080;
    char *host = "192.0.2.2";

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = atoi(argv[2]);
    }

    printf("curl: connecting to %s:%d\n", host, port);

    soc = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (soc == -1) {
        printf("curl: socket failed\n");
        exit(1);
    }
    printf("curl: socket created\n");

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, host, &server.sin_addr);

    printf("curl: calling connect...\n");
    if (connect(soc, (struct sockaddr *)&server, sizeof(server)) == -1) {
        printf("curl: connect failed\n");
        close(soc);
        exit(1);
    }
    printf("curl: connected!\n");

    i = 0;
    request[i++] = 'G';
    request[i++] = 'E';
    request[i++] = 'T';
    request[i++] = ' ';
    request[i++] = '/';
    request[i++] = ' ';
    request[i++] = 'H';
    request[i++] = 'T';
    request[i++] = 'T';
    request[i++] = 'P';
    request[i++] = '/';
    request[i++] = '1';
    request[i++] = '.';
    request[i++] = '1';
    request[i++] = '\r';
    request[i++] = '\n';
    request[i++] = 'H';
    request[i++] = 'o';
    request[i++] = 's';
    request[i++] = 't';
    request[i++] = ':';
    request[i++] = ' ';
    {
        char *h = host;
        while (*h && i < 250) {
            request[i++] = *h++;
        }
    }
    request[i++] = '\r';
    request[i++] = '\n';
    request[i++] = '\r';
    request[i++] = '\n';
    
    send(soc, request, i);
    printf("curl: request sent\n");

    int n = recv(soc, buf, sizeof(buf) - 1);
    printf("curl: received %d bytes\n", n);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s\n", buf);
    }

    close(soc);
    exit(0);
}