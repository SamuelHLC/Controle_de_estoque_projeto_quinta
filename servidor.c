#include <stdio.h>
#include <winsock2.h>
#include <pthread.h>

#pragma comment(lib, "ws2_32.lib")

// Estrutura do Produto
typedef struct {
    int qtd;
    pthread_mutex_t lock;
} Estoque;

Estoque prod = {20, PTHREAD_MUTEX_INITIALIZER};

int main() {
    WSADATA wsa;
    SOCKET s, novo_s;
    struct sockaddr_in server;
    int opcao;

    WSAStartup(MAKEWORD(2,2), &wsa);
    s = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8080);

    bind(s, (struct sockaddr *)&server, sizeof(server));
    listen(s, 3);

    printf("--- SERVIDOR DISTRIBUIDO ONLINE (Porta 8080) ---\n");

    while(1) {
        novo_s = accept(s, NULL, NULL);
        recv(novo_s, (char*)&opcao, sizeof(int), 0);

        if(opcao == 1) { // Consulta
            send(novo_s, (char*)&prod.qtd, sizeof(int), 0);
        } 
        else if(opcao == 2) { // Venda
            pthread_mutex_lock(&prod.lock); // Concorrência protegida!
            if(prod.qtd > 0) {
                prod.qtd--;
                char *msg = "Venda OK";
                send(novo_s, msg, 20, 0);
            }
            pthread_mutex_unlock(&prod.lock);
        }
        closesocket(novo_s);
    }
    return 0;
}