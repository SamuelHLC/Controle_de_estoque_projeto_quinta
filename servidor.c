#include <stdio.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct { int id; char nome[50]; char categoria[30]; float preco; int qtd; } Produto;

void sync_loja(Produto* l, int* t) {
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
    Produto lista[100];
    int total = 0, idx = 0, op;

    do {
        sync_loja(lista, &total);
        system("cls");
        printf("======= LOJA VIRTUAL =======\n");
        int fim = (idx + 5 > total) ? total : idx + 5;
        
        if (total > 0) {
            for (int i = idx; i < fim; i++) {
                printf(" [%d] %-15s | R$ %.2f | Estoque: %d\n", i + 1, lista[i].nome, lista[i].preco, lista[i].qtd);
            }
            printf("\n Pagina %d de %d\n", (idx/5)+1, (total == 0 ? 1 : (total-1)/5 + 1));
        } else printf(" LOJA SEM PRODUTOS CADASTRADOS\n");

        printf("----------------------------\n");
        printf(" 1. Comprar | 2. Prox Pag | 3. Ant Pag | 4. Sair\n Escolha: ");
        scanf("%d", &op);

        if (op == 1 && total > 0) {
            int sel, q;
            printf(" Escolha o numero do item ([%d]-[%d]): ", idx + 1, fim); scanf("%d", &sel);
            printf(" Quantidade desejada: "); scanf("%d", &q);
            
            SOCKET s = socket(2, 1, 0);
            struct sockaddr_in adr = {2, htons(8080), inet_addr("127.0.0.1")};
            if(connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
                int c[3] = {2, lista[sel-1].id, q};
                send(s, (char*)c, sizeof(c), 0);
                char res[30]; recv(s, res, 30, 0);
                printf(" RESPOSTA DO SERVIDOR: %s\n", res);
            }
            closesocket(s); system("pause");
        } else if (op == 2 && idx + 5 < total) idx += 5;
        else if (op == 3 && idx - 5 >= 0) idx -= 5;

    } while (op != 4);
    WSACleanup();
    return 0;
}
