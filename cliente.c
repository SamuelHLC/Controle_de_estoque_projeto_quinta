#include <stdio.h>
#include <winsock2.h>
#include <time.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

// CORRIGIDO: campo renomeado de "categoria" para "category" para ser identico ao servidor.c
typedef struct { 
    int id; 
    char nome[50]; 
    char category[30];   // era: char categoria[30]
    float preco; 
    int qtd; 
} Produto;

void buscar_estoque(Produto* l, int* t) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    // CORRIGIDO: inicializacao explicita com memset para zerar sin_zero
    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family = AF_INET;
    adr.sin_port = htons(8085);
    adr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int r[3] = {0, 0, 0}; 
        send(s, (char*)r, sizeof(r), 0);
        
        int recebidos = recv(s, (char*)t, sizeof(int), 0);
        
        if (recebidos > 0 && *t > 0) {
            int tamanho_lista = sizeof(Produto) * (*t);
            int total_lido = 0;
            char* ptr = (char*)l;
            
            while (total_lido < tamanho_lista) {
                int n = recv(s, ptr + total_lido, tamanho_lista - total_lido, 0);
                if (n <= 0) break;
                total_lido += n;
            }
        } else if (recebidos <= 0) {
            *t = 0;
        }
    } else {
        printf("[ERRO] Nao foi possivel conectar ao servidor.\n");
        *t = 0;
    }
    closesocket(s);
}

int main() {
    WSADATA w; WSAStartup(0x0202, &w);
    Produto lista[100];
    int total = 0, idx = 0, op;

    while (1) {
        buscar_estoque(lista, &total);
        
        if (total > 0 && idx >= total) idx = total - 1;
        if (total == 0) idx = 0;

        system("cls");
        printf("======= LOJA VIRTUAL - AMBIENTE DISTRIBUIDO =======\n");
        if (total > 0) {
            printf(" PRODUTO: [%03d] %s\n", lista[idx].id, lista[idx].nome);
            printf(" CATEGORIA: %s\n", lista[idx].category);  // CORRIGIDO: era .categoria
            printf(" PRECO: R$ %.2f | ESTOQUE: %d\n", lista[idx].preco, lista[idx].qtd);
            printf("---------------------------------------------------\n");
            printf(" Exibindo %d de %d\n", idx + 1, total);
        } else {
            printf("\n >>> SEM PRODUTOS NO SERVIDOR <<<\n");
        }

        printf("\n 1. Comprar\n 2. Proximo\n 3. Anterior\n 4. Sair\n Escolha: ");
        
        if (scanf("%d", &op) != 1) {
            // CORRIGIDO: fflush(stdin) tem comportamento indefinido
            int c; while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }

        if (op == 1 && total > 0) {
            int q;
            printf(" Quantidade desejada: "); scanf("%d", &q);
            
            clock_t inicio = clock();
            
            SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

            // CORRIGIDO: mesma correcao de inicializacao com memset
            struct sockaddr_in adr2;
            memset(&adr2, 0, sizeof(adr2));
            adr2.sin_family = AF_INET;
            adr2.sin_port = htons(8085);
            adr2.sin_addr.s_addr = inet_addr("127.0.0.1");
            
            if(connect(s, (struct sockaddr*)&adr2, sizeof(adr2)) == 0) {
                int req[3] = {2, lista[idx].id, q};
                send(s, (char*)req, sizeof(req), 0);
                
                char res[30];
                memset(res, 0, 30);
                int bytes = recv(s, res, 29, 0);
                if (bytes > 0) res[bytes] = '\0';
                
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
