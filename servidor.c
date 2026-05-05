#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct { int id; char nome[50]; char categoria[30]; float preco; int qtd; } Produto;

Produto lista[100];
int total_produtos = 0;
HANDLE lock;

void salvar() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) { fwrite(&total_produtos, sizeof(int), 1, f); fwrite(lista, sizeof(Produto), total_produtos, f); fclose(f); }
}

void carregar() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) { fread(&total_produtos, sizeof(int), 1, f); fread(lista, sizeof(Produto), total_produtos, f); fclose(f); }
}

DWORD WINAPI tratar(LPVOID arg) {
    SOCKET s = *(SOCKET*)arg;
    int dados[3];
    if (recv(s, (char*)dados, sizeof(dados), 0) > 0) {
        if (dados[0] == 0) { // Envia lista
            send(s, (char*)&total_produtos, sizeof(int), 0);
            send(s, (char*)lista, sizeof(Produto) * total_produtos, 0);
        } else if (dados[0] == 2) { // Compra
            int idx = -1;
            for(int i=0; i<total_produtos; i++) if(lista[i].id == dados[1]) idx = i;
            WaitForSingleObject(lock, INFINITE);
            if (idx != -1 && lista[idx].qtd >= dados[2]) {
                lista[idx].qtd -= dados[2]; salvar(); send(s, "COMPRA OK", 30, 0);
            } else send(s, "ERRO NO ESTOQUE", 30, 0);
            ReleaseMutex(lock);
        }
    }
    closesocket(s); free(arg); return 0;
}

int main() {
    WSADATA w; WSAStartup(0x0202, &w);
    lock = CreateMutex(NULL, FALSE, NULL);
    carregar();
    SOCKET s = socket(2, 1, 0);
    struct sockaddr_in adr = {2, htons(8080), 0};
    bind(s, (struct sockaddr*)&adr, sizeof(adr));
    listen(s, 5);
    printf("SERVIDOR ATIVO NA PORTA 8080\n");
    while(1) {
        SOCKET* ns = malloc(sizeof(SOCKET));
        *ns = accept(s, 0, 0);
        CreateThread(0, 0, tratar, ns, 0, 0);
    }
    return 0;
}