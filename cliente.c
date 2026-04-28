#include <stdio.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in server;
    int menu, resposta_qtd;
    char buffer[20];

    WSAStartup(MAKEWORD(2,2), &wsa);
    
    printf("1. Consultar | 2. Comprar: ");
    scanf("%d", &menu);

    s = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);

    connect(s, (struct sockaddr *)&server, sizeof(server));
    send(s, (char*)&menu, sizeof(int), 0);

    if(menu == 1) {
        recv(s, (char*)&resposta_qtd, sizeof(int), 0);
        printf("Estoque atual: %d\n", resposta_qtd);
    } else {
        recv(s, buffer, 20, 0);
        printf("Servidor diz: %s\n", buffer);
    }

    closesocket(s);
    return 0;
}