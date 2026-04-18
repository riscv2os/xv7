#include "kernel/types.h"
#include "user/user.h"
#include "kernel/net/socket.h"

static char WELCOME_MSG[] = 
    "\r\nxv7 telnet server\r\n"
    "Welcome! Try these commands: ls, cat, echo, ps, ifconfig\r\n"
    "Type 'exit' to logout\r\n\r\n";

static char SHELL_PROMPT[] = "$ ";

int main(int argc, char *argv[])
{
    int soc, acc, peerlen;
    struct sockaddr_in self, peer;
    char buf[128];
    char cmd_buf[128];
    int cmd_len = 0;
    int port = 23;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("Starting Telnet Server on port %d\n", port);
    
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
    
    listen(soc, 5);
    printf("waiting for connection...\n");

    while (1) {
        peerlen = sizeof(peer);
        acc = accept(soc, (struct sockaddr *)&peer, &peerlen);
        if (acc == -1) {
            printf("accept: failure\n");
            continue;
        }
        
        printf("connection from client\n");
        
        send(acc, WELCOME_MSG, strlen(WELCOME_MSG));
        send(acc, SHELL_PROMPT, 2);
        
        cmd_len = 0;
        
        while (1) {
            int ret = recv(acc, buf, sizeof(buf) - 1);
            if (ret <= 0) break;
            
            buf[ret] = '\0';
            
            int i;
            for (i = 0; i < ret; i++) {
                if (buf[i] == '\r' || buf[i] == '\n') {
                    cmd_buf[cmd_len] = '\0';
                    
                    if (cmd_len > 0) {
                        if (cmd_buf[0] == 'e' && cmd_buf[1] == 'x' && 
                            cmd_buf[2] == 'i' && cmd_buf[3] == 't') {
                            send(acc, "logout\r\n", 8);
                            break;
                        }
                        
                        send(acc, "Sorry, command execution is under development\r\n", 49);
                        send(acc, "Use 'sh' to start xv6 shell\r\n", 31);
                    }
                    
                    send(acc, SHELL_PROMPT, 2);
                    cmd_len = 0;
                } else if (cmd_len < (int)(sizeof(cmd_buf) - 1)) {
                    cmd_buf[cmd_len++] = buf[i];
                }
            }
        }
        
        close(acc);
        printf("client disconnected\n");
    }
    
    close(soc);
    exit(0);
}