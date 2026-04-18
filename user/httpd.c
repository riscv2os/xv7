#include "kernel/types.h"
#include "user/user.h"
#include "kernel/net/socket.h"

static char HTTP_RESPONSE[] = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "<h1>Hello!</h1>";

static char HTTP_RESPONSE_404[] = 
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "\r\n"
    "Not Found";

int main(int argc, char *argv[])
{
    int soc, acc, peerlen;
    struct sockaddr_in self, peer;
    unsigned char *addr;
    char buf[2048];
    int port = 8080;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("Starting HTTP Server on port %d\n", port);
    
    soc = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (soc == -1) {
        printf("socket: failure\n");
        exit(1);
    }
    printf("socket: success, soc=%d\n", soc);
    
    self.sin_family = AF_INET;
    self.sin_addr.s_addr = INADDR_ANY;
    self.sin_port = htons(port);
    
    if (bind(soc, (struct sockaddr *)&self, sizeof(self)) == -1) {
        printf("bind: failure\n");
        close(soc);
        exit(1);
    }
    
    addr = (unsigned char *)&self.sin_addr;
    printf("bind: success, http://%d.%d.%d.%d:%d\n", 
           addr[0], addr[1], addr[2], addr[3], ntohs(self.sin_port));
    
    listen(soc, 5);
    printf("waiting for connection...\n");

    while (1) {
        peerlen = sizeof(peer);
        acc = accept(soc, (struct sockaddr *)&peer, &peerlen);
        if (acc == -1) {
            printf("accept: failure\n");
            continue;
        }
        
        addr = (unsigned char *)&peer.sin_addr;
        printf("accept: from %d.%d.%d.%d:%d\n", 
               addr[0], addr[1], addr[2], addr[3], ntohs(peer.sin_port));
        
        int ret = recv(acc, buf, sizeof(buf) - 1);
        if (ret > 0) {
            buf[ret] = '\0';
            printf("recv: %d bytes\n", ret);
            
            if (buf[0] == 'G' && buf[1] == 'E' && buf[2] == 'T' && buf[3] == ' ') {
                send(acc, HTTP_RESPONSE, strlen(HTTP_RESPONSE));
            } else {
                send(acc, HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404));
            }
        }
        
        close(acc);
    }
    
    close(soc);
    exit(0);
}