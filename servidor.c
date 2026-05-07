#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

// REFERENCIA: este e o arquivo original do servidor — o campo "category" (ingles)
// e o nome correto. admin.c e cliente.c foram corrigidos para usar "category" tambem.
typedef struct { 
    int id; 
    char nome[50]; 
    char category[30];
    float preco; 
    int qtd; 
} Produto;

Produto lista[100];
int total = 0;

HANDLE mutex_estoque;
HANDLE semaforo_leitura;

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
    } else {
        total = 0;
    }
}

DWORD WINAPI tratar_cliente(LPVOID lpParam) {
    SOCKET s = *(SOCKET*)lpParam;
    free(lpParam);

    int req[3]; 
    if (recv(s, (char*)req, sizeof(req), 0) <= 0) {
        closesocket(s);
        return 0;
    }

    if (req[0] == 0) { // LEITURA
        WaitForSingleObject(semaforo_leitura, INFINITE);
        
        carregar_dados();
        
        qsort(lista, total, sizeof(Produto), comparar_id);
        send(s, (char*)&total, sizeof(int), 0);
        if (total > 0) {
            send(s, (char*)lista, sizeof(Produto) * total, 0);
        }
        
        ReleaseSemaphore(semaforo_leitura, 1, NULL);
    } 
    else if (req[0] == 2) { // COMPRA (ESCRITA)
        printf("[MTX] Thread %d: Aguardando Mutex...\n", GetCurrentThreadId());
        WaitForSingleObject(mutex_estoque, INFINITE);
        
        carregar_dados();
        
        printf("[MTX] Thread %d: Processando ID %d\n", GetCurrentThreadId(), req[1]);
        char resposta[30] = "Erro: Produto esgotado";

        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1] && lista[i].qtd >= req[2]) {
                lista[i].qtd -= req[2];
                strcpy(resposta, "Compra confirmada!");
                salvar_dados(); 
                break;
            }
        }
        
        send(s, resposta, 30, 0);
        printf("[MTX] Thread %d: Liberando Mutex.\n", GetCurrentThreadId());
        ReleaseMutex(mutex_estoque);
    }

    closesocket(s);
    return 0;
}

int main() {
    WSADATA w; WSAStartup(0x0202, &w);
    carregar_dados();

    mutex_estoque = CreateMutex(NULL, FALSE, NULL);
    semaforo_leitura = CreateSemaphore(NULL, 2, 2, NULL); 

    SOCKET servidor = socket(AF_INET, SOCK_STREAM, 0);
    
    // CORRIGIDO: memset para garantir sin_zero zerado
    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family = AF_INET;
    adr.sin_addr.s_addr = INADDR_ANY; 
    adr.sin_port = htons(8085); 

    bind(servidor, (struct sockaddr*)&adr, sizeof(adr));
    listen(servidor, SOMAXCONN); 

    printf("==============================================\n");
    printf(" SERVIDOR DISTRIBUIDO - PORTA 8085            \n");
    printf("==============================================\n");

    while (1) {
        SOCKET* p_cliente = malloc(sizeof(SOCKET));
        *p_cliente = accept(servidor, 0, 0);
        if (*p_cliente != INVALID_SOCKET) {
            CreateThread(NULL, 0, tratar_cliente, p_cliente, 0, NULL);
        }
    }

    WSACleanup();
    return 0;
}
