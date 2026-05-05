#include <stdio.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct { int id; char nome[50]; char categoria[30]; float preco; int qtd; } Produto;

void sync_lista(Produto* l, int* t) {
    SOCKET s = socket(2, 1, 0);
    struct sockaddr_in adr = {2, htons(8080), inet_addr("127.0.0.1")};
    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int r[3] = {0,0,0}; send(s, (char*)r, sizeof(r), 0);
        recv(s, (char*)t, sizeof(int), 0);
        if (*t > 0) recv(s, (char*)l, sizeof(Produto) * (*t), 0);
    }
    closesocket(s);
}

int main() {
    WSADATA w; WSAStartup(0x0202, &w);
    Produto lista[100]; int total = 0, idx = 0, op;
    do {
        sync_lista(lista, &total);
        system("cls");
        printf("\n======= LOJA VIRTUAL =======\n");
        if (total > 0) {
            printf(" EXIBINDO: %d de %d\n ID: %03d | %s\n Categoria: %s\n Preco: R$ %.2f\n Estoque: %d\n", 
                    idx+1, total, lista[idx].id, lista[idx].nome, lista[idx].categoria, lista[idx].preco, lista[idx].qtd);
        }
        printf("----------------------------\n 1. Comprar\n 2. Proximo\n 3. Anterior\n 4. Sair\n Escolha: ");
        scanf("%d", &op);
        if (op == 1 && total > 0) {
            int q; printf("Quantidade: "); scanf("%d", &q);
            SOCKET s = socket(2, 1, 0);
            struct sockaddr_in adr = {2, htons(8080), inet_addr("127.0.0.1")};
            connect(s, (struct sockaddr*)&adr, sizeof(adr));
            int c[3] = {2, lista[idx].id, q}; send(s, (char*)c, sizeof(c), 0);
            char st[30]; recv(s, st, 30, 0); printf("%s\n", st);
            closesocket(s); system("pause");
        } else if (op == 2 && idx < total - 1) idx++;
        else if (op == 3 && idx > 0) idx--;
    } while (op != 4);
    return 0;
}