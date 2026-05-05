#include <stdio.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in server;
    int menu, resposta_qtd;
    char buffer[20] = {0};

    WSAStartup(MAKEWORD(2,2), &wsa);
    
    printf("--- CLIENTE LOJA DISTRIBUIDA ---\n");
    printf("1. Consultar Estoque\n2. Comprar Produto\nEscolha: ");
    scanf("%d", &menu);

    s = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);

    if (connect(s, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("Erro ao conectar ao servidor.\n");
        return 1;
    }

    send(s, (char*)&menu, sizeof(int), 0);

    if(menu == 1) {
        recv(s, (char*)&resposta_qtd, sizeof(int), 0);
        printf("\n[RESPOSTA] Estoque atual: %d unidades.\n", resposta_qtd);
    } else {
        recv(s, buffer, 20, 0);
        printf("\n[RESPOSTA] Servidor diz: %s\n", buffer);
    }

    closesocket(s);
    WSACleanup();
    return 0;
}
