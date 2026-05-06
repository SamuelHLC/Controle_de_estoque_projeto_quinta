#include <stdio.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct { int id; char nome[50]; char categoria[30]; float preco; int qtd; } Produto;
Produto lista[100];
int total = 0;
HANDLE lock;

void salvar() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) { fwrite(&total, sizeof(int), 1, f); fwrite(lista, sizeof(Produto), total, f); fclose(f); }
}

DWORD WINAPI tratar(LPVOID arg) {
    SOCKET s = *(SOCKET*)arg;
    int d[3];
    if (recv(s, (char*)d, sizeof(d), 0) > 0) {
        if (d[0] == 0) { // Sincronização da lista
            send(s, (char*)&total, sizeof(int), 0);
            send(s, (char*)lista, sizeof(Produto) * total, 0);
        } else if (d[0] == 2) { // Operação de compra
            WaitForSingleObject(lock, INFINITE);
            int ok = 0;
            for(int i=0; i<total; i++) {
                if(lista[i].id == d[1] && lista[i].qtd >= d[2]) {
                    lista[i].qtd -= d[2]; salvar(); ok = 1; break;
                }
            }
            send(s, ok ? "SUCESSO" : "ERRO_ESTOQUE", 30, 0);
            ReleaseMutex(lock);
        }
    }
    closesocket(s); free(arg); return 0;
}

int main() {
    WSADATA w; WSAStartup(0x0202, &w);
    lock = CreateMutex(NULL, FALSE, NULL);
    FILE *f = fopen("estoque.dat", "rb");
    if(f){ fread(&total, sizeof(int), 1, f); fread(lista, sizeof(Produto), total, f); fclose(f); }
    SOCKET s = socket(2, 1, 0);
    struct sockaddr_in adr = {2, htons(8080), 0};
    bind(s, (struct sockaddr*)&adr, sizeof(adr));
    listen(s, 5);
    printf("SERVIDOR INFRA ONLINE (Porta 8080)...\n");
    while(1) {
        SOCKET* ns = malloc(sizeof(SOCKET));
        *ns = accept(s, 0, 0);
        CreateThread(0, 0, tratar, ns, 0, 0);
    }
    return 0;
}
