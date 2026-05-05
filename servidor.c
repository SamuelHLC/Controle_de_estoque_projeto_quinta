#include <stdio.h>
#include <winsock2.h>
#include <pthread.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct {
    int qtd;
    pthread_mutex_t lock;
} Estoque;

Estoque prod = {20, PTHREAD_MUTEX_INITIALIZER};

// Função que cada thread de cliente vai executar
void* tratar_cliente(void* arg) {
    SOCKET novo_s = *(SOCKET*)arg;
    int opcao;
    
    if (recv(novo_s, (char*)&opcao, sizeof(int), 0) > 0) {
        if(opcao == 1) { 
            printf("[Socket %d] Solicitou consulta de estoque.\n", (int)novo_s);
            send(novo_s, (char*)&prod.qtd, sizeof(int), 0);
        } 
        else if(opcao == 2) { 
            printf("[Socket %d] Tentando acessar SECAO CRITICA (Locking Mutex)...\n", (int)novo_s);
            
            pthread_mutex_lock(&prod.lock); 
            
            printf("[Socket %d] >> DENTRO DA SECAO CRITICA. Processando venda...\n", (int)novo_s);
            if(prod.qtd > 0) {
                prod.qtd--;
                send(novo_s, "Venda OK", 20, 0);
                printf("[Socket %d] Venda concluída. Estoque restante: %d\n", (int)novo_s, prod.qtd);
            } else {
                send(novo_s, "Sem Estoque", 20, 0);
                printf("[Socket %d] Falha: Estoque zerado.\n", (int)novo_s);
            }
            
            pthread_mutex_unlock(&prod.lock);
            printf("[Socket %d] Saiu da SECAO CRITICA (Unlocking Mutex).\n", (int)novo_s);
        }
    }

    closesocket(novo_s);
    free(arg);
    return NULL;
}

int main() {
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in server;

    WSAStartup(MAKEWORD(2,2), &wsa);
    s = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8080);

    bind(s, (struct sockaddr *)&server, sizeof(server));
    listen(s, 10); // Aumentado para suportar mais conexões em fila

    printf("--- SERVIDOR DISTRIBUIDO MULTI-THREAD ONLINE (Porta 8080) ---\n");

    while(1) {
        struct sockaddr_in cliente_addr;
        int tam = sizeof(cliente_addr);
        SOCKET* novo_s = malloc(sizeof(SOCKET));
        *novo_s = accept(s, (struct sockaddr*)&cliente_addr, &tam);

        printf("\n[SISTEMA] Nova conexao aceita no Socket %d\n", (int)*novo_s);

        // Criando a thread para gerenciar o novo cliente de forma independente
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, tratar_cliente, novo_s);
        pthread_detach(thread_id); // Libera os recursos da thread ao finalizar
    }

    closesocket(s);
    WSACleanup();
    return 0;
}
