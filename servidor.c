#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct { 
    int id; 
    char nome[50]; 
    char categoria[30]; 
    float preco; 
    int qtd; 
} Produto;

Produto lista[100];
int total = 0;

// Mecanismos de Sincronização (RF_07, RF_08, RF_10)
HANDLE mutex_estoque;
HANDLE semaforo_leitura;

// RF_04: Ordenação QuickSort O(n log n)
int comparar_id(const void *a, const void *b) {
    return ((Produto*)a)->id - ((Produto*)b)->id;
}

void salvar_dados() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) {
        fwrite(&total, sizeof(int), 1, f);
        fwrite(lista, sizeof(Produto), total, f);
        fclose(f);
    }
}

void carregar_dados() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) {
        if(fread(&total, sizeof(int), 1, f) != 1) total = 0;
        else fread(lista, sizeof(Produto), total, f);
        fclose(f);
    }
}

DWORD WINAPI tratar_cliente(LPVOID lpParam) {
    SOCKET s = *(SOCKET*)lpParam;
    free(lpParam);

    struct sockaddr_in cliente_info;
    int size = sizeof(cliente_info);
    getpeername(s, (struct sockaddr*)&cliente_info, &size);

    int req[3]; // [0]=Tipo, [1]=ID, [2]=Qtd
    if (recv(s, (char*)req, sizeof(req), 0) <= 0) {
        closesocket(s);
        return 0;
    }

    // RF_09: Logs de Concorrência e Rede
    printf("\n[REDE] Cliente %s conectado.\n", inet_ntoa(cliente_info.sin_addr));

    if (req[0] == 0) { // OPERAÇÃO DE LEITURA
        // RF_10: Controle de Leitura com Semáforo
        printf("[SEM] Thread %d aguardando semaforo de leitura...\n", GetCurrentThreadId());
        WaitForSingleObject(semaforo_leitura, INFINITE);
        
        printf("[SEM] Thread %d lendo estoque (Sincronizacao Hibrida).\n", GetCurrentThreadId());
        qsort(lista, total, sizeof(Produto), comparar_id); // RF_04
        
        send(s, (char*)&total, sizeof(int), 0);
        if (total > 0) send(s, (char*)lista, sizeof(Produto) * total, 0);
        
        ReleaseSemaphore(semaforo_leitura, 1, NULL);
    } 
    else if (req[0] == 2) { // OPERAÇÃO DE ESCRITA (COMPRA)
        // RF_07: Mutex para evitar Race Condition
        printf("[MTX] Thread %d bloqueada: Aguardando Mutex...\n", GetCurrentThreadId());
        WaitForSingleObject(mutex_estoque, INFINITE);
        
        printf("[MTX] Thread %d em SECAO CRITICA - Processando ID %d\n", GetCurrentThreadId(), req[1]);
        char resposta[30] = "Erro: Produto esgotado";

        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1] && lista[i].qtd >= req[2]) {
                lista[i].qtd -= req[2];
                strcpy(resposta, "Compra confirmada!");
                salvar_dados(); // Persistência RNF_02[cite: 1]
                break;
            }
        }
        
        send(s, resposta, 30, 0);
        printf("[MTX] Thread %d concluiu. Liberando Mutex.\n", GetCurrentThreadId());
        ReleaseMutex(mutex_estoque);
    }

    closesocket(s);
    return 0;
}

int main() {
    WSADATA w; WSAStartup(0x0202, &w);
    carregar_dados();

    // Inicialização (RF_07 e RF_10)[cite: 1]
    mutex_estoque = CreateMutex(NULL, FALSE, NULL);
    semaforo_leitura = CreateSemaphore(NULL, 2, 2, NULL); // Até 2 leituras simultâneas

    SOCKET servidor = socket(2, 1, 0);
    struct sockaddr_in adr = {2, htons(8080), 0};
    bind(servidor, (struct sockaddr*)&adr, sizeof(adr));
    listen(servidor, 10);

    printf("==============================================\n");
    printf(" SERVIDOR DISTRIBUIDO V6.0 - AGUARDANDO REDE  \n");
    printf("==============================================\n");

    while (1) {
        SOCKET* p_cliente = malloc(sizeof(SOCKET));
        *p_cliente = accept(servidor, 0, 0);
        CreateThread(NULL, 0, tratar_cliente, p_cliente, 0, NULL);
    }

    return 0;
}
