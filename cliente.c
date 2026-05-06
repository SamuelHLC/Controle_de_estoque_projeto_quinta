#include <stdio.h>
#include <winsock2.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct { 
    int id; 
    char nome[50]; 
    char categoria[30]; 
    float preco; 
    int qtd; 
} Produto;

void buscar_estoque(Produto* l, int* t) {
    SOCKET s = socket(2, 1, 0);
    struct sockaddr_in adr = {2, htons(8080), inet_addr("127.0.0.1")};
    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int r[3] = {0, 0, 0}; 
        send(s, (char*)r, sizeof(r), 0);
        recv(s, (char*)t, sizeof(int), 0);
        if (*t > 0) recv(s, (char*)l, sizeof(Produto) * (*t), 0);
    }
    closesocket(s);
}

int main() {
    WSADATA w; WSAStartup(0x0202, &w);
    Produto lista[100];
    int total = 0, idx = 0, op;

    while (1) {
        buscar_estoque(lista, &total);
        system("cls");
        printf("======= LOJA VIRTUAL - AMBIENTE DISTRIBUIDO =======\n");
        if (total > 0) {
            printf(" PRODUTO: [%03d] %s\n", lista[idx].id, lista[idx].nome);
            printf(" CATEGORIA: %s\n", lista[idx].categoria);
            printf(" PRECO: R$ %.2f | ESTOQUE: %d\n", lista[idx].preco, lista[idx].qtd);
            printf("---------------------------------------------------\n");
            printf(" Exibindo %d de %d\n", idx + 1, total);
        } else {
            printf("\n >>> SEM PRODUTOS NO SERVIDOR <<<\n");
        }

        printf("\n 1. Comprar\n 2. Proximo\n 3. Anterior\n 4. Sair\n Escolha: ");
        scanf("%d", &op);

        if (op == 1 && total > 0) {
            int q;
            printf(" Quantidade desejada: "); scanf("%d", &q);
            
            // Medição de Desempenho (Métrica sugerida pelo professor)[cite: 1]
            clock_t inicio = clock();
            
            SOCKET s = socket(2, 1, 0);
            struct sockaddr_in adr = {2, htons(8080), inet_addr("127.0.0.1")};
            if(connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
                int req[3] = {2, lista[idx].id, q};
                send(s, (char*)req, sizeof(req), 0);
                char res[30];
                recv(s, res, 30, 0);
                
                clock_t fim = clock();
                double tempo = ((double)(fim - inicio)) / CLOCKS_PER_SEC;
                
                printf("\n STATUS: %s\n TEMPO DE RESPOSTA: %.4f segundos\n", res, tempo);
            }
            closesocket(s);
            system("pause");
        } 
        else if (op == 2 && idx < total - 1) idx++;
        else if (op == 3 && idx > 0) idx--;
        else if (op == 4) break;
    }

    WSACleanup();
    return 0;
}
