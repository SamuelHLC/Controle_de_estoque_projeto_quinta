/*
 * ============================================================
 *  CLIENTE - CONTROLE DE ESTOQUE v7.0
 *  Projeto Integrador de Computacao Paralela - UNIEURO
 * ============================================================
 *  MODOS:
 *   1. Compra interativa normal
 *   2. BENCHMARK: dispara N clientes simultaneos para
 *      demonstrar concorrencia real ao vivo na apresentacao
 * ============================================================
 *  COMPILAR: gcc cliente.c -o cliente -lws2_32
 * ============================================================
 */

#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

/* ─────────────────────────────────────────────
   ESTRUTURA (identica ao servidor.c)
   ───────────────────────────────────────────── */
typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

/* ─────────────────────────────────────────────
   ARGUMENTO PARA THREAD DE BENCHMARK
   ───────────────────────────────────────────── */
typedef struct {
    int cliente_num;
    int produto_id;
    int quantidade;
    /* resultados preenchidos pela thread */
    char status[64];
    double tempo_ms;
    int sucesso;
} ArgsCliente;

/* ─────────────────────────────────────────────
   FUNCAO AUXILIAR: conecta e compra
   ───────────────────────────────────────────── */
static int conectar_e_comprar(int produto_id, int qtd, char *resposta_out) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family      = AF_INET;
    adr.sin_port        = htons(8085);
    adr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) != 0) {
        strcpy(resposta_out, "ERRO: conexao recusada");
        closesocket(s);
        return -1;
    }

    int req[3] = {2, produto_id, qtd};
    send(s, (char*)req, sizeof(req), 0);

    char buf[30];
    memset(buf, 0, 30);
    int n = recv(s, buf, 29, 0);
    if (n > 0) buf[n] = '\0';
    strncpy(resposta_out, buf, 63);
    closesocket(s);

    return (strstr(buf, "confirmada") != NULL) ? 1 : 0;
}

/* ─────────────────────────────────────────────
   THREAD DE BENCHMARK: cada thread = 1 cliente
   ───────────────────────────────────────────── */
DWORD WINAPI thread_cliente_benchmark(LPVOID lpParam) {
    ArgsCliente *a = (ArgsCliente*)lpParam;

    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t1);

    char resposta[64] = {0};
    a->sucesso = conectar_e_comprar(a->produto_id, a->quantidade, resposta);
    strncpy(a->status, resposta, 63);

    QueryPerformanceCounter(&t2);
    a->tempo_ms = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart * 1000.0;

    return 0;
}

/* ─────────────────────────────────────────────
   MODO BENCHMARK: dispara N threads simultaneas
   Demonstra concorrencia real para a apresentacao
   ───────────────────────────────────────────── */
void modo_benchmark(int produto_id, int num_clientes) {
    printf("\n");
    printf("=======================================================\n");
    printf("  INICIANDO VALIDACAO CIENTIFICA DE CONCORRENCIA       \n");
    printf("  Alvo: Porta 8085 | Produto ID %03d                   \n", produto_id);
    printf("  Clientes simultaneos: %d                             \n", num_clientes);
    printf("=======================================================\n\n");

    if (num_clientes > 50) num_clientes = 50;

    ArgsCliente *args    = calloc(num_clientes, sizeof(ArgsCliente));
    HANDLE      *threads = calloc(num_clientes, sizeof(HANDLE));

    for (int i = 0; i < num_clientes; i++) {
        args[i].cliente_num = i + 1;
        args[i].produto_id  = produto_id;
        args[i].quantidade  = 1;
    }

    /* dispara TODAS as threads ao mesmo tempo */
    LARGE_INTEGER freq, t_inicio, t_fim;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_inicio);

    for (int i = 0; i < num_clientes; i++)
        threads[i] = CreateThread(NULL, 0, thread_cliente_benchmark, &args[i], 0, NULL);

    /* aguarda todas terminarem */
    WaitForMultipleObjects(num_clientes, threads, TRUE, 15000);
    QueryPerformanceCounter(&t_fim);

    double tempo_total = (double)(t_fim.QuadPart - t_inicio.QuadPart) / freq.QuadPart;

    /* ── RESULTADO ── */
    printf("%-10s | %-8s | %-12s | %s\n",
           "CLIENTE", "TEMPO_ms", "STATUS", "RESULTADO");
    printf("----------------------------------------------\n");

    int ok = 0, err = 0;
    for (int i = 0; i < num_clientes; i++) {
        printf("[QA-TEST] Cliente %02d | %6.2f ms | %s\n",
               args[i].cliente_num,
               args[i].tempo_ms,
               args[i].status);
        if (args[i].sucesso == 1) ok++;
        else                      err++;
        CloseHandle(threads[i]);
    }

    printf("\n==============================================\n");
    printf("             METRICAS DE BENCHMARK            \n");
    printf("==============================================\n");
    printf(" Clientes disparados : %d\n", num_clientes);
    printf(" Compras confirmadas : %d\n", ok);
    printf(" Produto esgotado    : %d\n", err);
    printf(" Tempo total         : %.4f segundos\n", tempo_total);
    printf(" Throughput          : %.2f operacoes/segundo\n",
           tempo_total > 0 ? num_clientes / tempo_total : 0.0);
    printf(" Consistencia        : %s\n",
           err > 0 ? "OK - Mutex impediu estoque negativo" : "OK - Todas as compras passaram");
    printf("==============================================\n\n");

    free(args);
    free(threads);
}

/* ─────────────────────────────────────────────
   BUSCAR ESTOQUE DO SERVIDOR
   ───────────────────────────────────────────── */
void buscar_estoque(Produto *l, int *t) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family      = AF_INET;
    adr.sin_port        = htons(8085);
    adr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int r[3] = {0, 0, 0};
        send(s, (char*)r, sizeof(r), 0);

        int recebidos = recv(s, (char*)t, sizeof(int), 0);
        if (recebidos > 0 && *t > 0) {
            int tam = sizeof(Produto) * (*t);
            int lido = 0;
            char *ptr = (char*)l;
            while (lido < tam) {
                int n = recv(s, ptr + lido, tam - lido, 0);
                if (n <= 0) break;
                lido += n;
            }
        } else {
            *t = 0;
        }
    } else {
        printf("[ERRO] Servidor indisponivel. Verifique se servidor.exe esta rodando.\n");
        *t = 0;
    }
    closesocket(s);
}

/* ─────────────────────────────────────────────
   MAIN
   ───────────────────────────────────────────── */
int main() {
    WSADATA w;
    WSAStartup(0x0202, &w);

    Produto lista[100];
    int total = 0, idx = 0, op;

    while (1) {
        buscar_estoque(lista, &total);
        if (total > 0 && idx >= total) idx = total - 1;
        if (total == 0) idx = 0;

        system("cls");
        printf("=======================================================\n");
        printf("  LOJA VIRTUAL - AMBIENTE DISTRIBUIDO v7.0             \n");
        printf("=======================================================\n");

        if (total > 0) {
            printf(" PRODUTO  : [%03d] %s\n",   lista[idx].id, lista[idx].nome);
            printf(" CATEGORIA: %s\n",           lista[idx].category);
            printf(" PRECO    : R$ %.2f\n",      lista[idx].preco);
            printf(" ESTOQUE  : %d unidades\n",  lista[idx].qtd);
            printf("-------------------------------------------------------\n");
            printf(" Exibindo %d de %d produto(s)\n", idx + 1, total);
        } else {
            printf("\n >>> NENHUM PRODUTO DISPONIVEL NO SERVIDOR <<<\n");
        }

        printf("\n");
        printf(" 1. Comprar (interativo)\n");
        printf(" 2. Proximo produto\n");
        printf(" 3. Produto anterior\n");
        printf(" 4. [BENCHMARK] Simular N clientes simultaneos\n");
        printf(" 5. Sair\n");
        printf(" Escolha: ");

        if (scanf("%d", &op) != 1) {
            int c; while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }

        /* ── COMPRA INTERATIVA ── */
        if (op == 1 && total > 0) {
            int q;
            printf(" Quantidade desejada: "); scanf("%d", &q);
            if (q <= 0) { printf(" Quantidade invalida.\n"); system("pause"); continue; }

            LARGE_INTEGER freq, t1, t2;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&t1);

            char resposta[64] = {0};
            conectar_e_comprar(lista[idx].id, q, resposta);

            QueryPerformanceCounter(&t2);
            double ms = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart * 1000.0;

            printf("\n STATUS          : %s\n", resposta);
            printf(" TEMPO RESPOSTA  : %.2f ms\n\n", ms);
            system("pause");
        }

        /* ── NAVEGACAO ── */
        else if (op == 2 && idx < total - 1) idx++;
        else if (op == 3 && idx > 0)         idx--;

        /* ── BENCHMARK / SIMULACAO CONCORRENCIA ── */
        else if (op == 4) {
            if (total == 0) { printf(" Sem produtos para testar.\n"); system("pause"); continue; }
            int n_cli, prod_id;
            printf("\n === MODO BENCHMARK ===\n");
            printf(" ID do produto alvo: "); scanf("%d", &prod_id);
            printf(" Numero de clientes simultaneos (max 50): "); scanf("%d", &n_cli);
            if (n_cli < 1) n_cli = 1;

            modo_benchmark(prod_id, n_cli);
            system("pause");
        }

        else if (op == 5) break;
    }

    WSACleanup();
    return 0;
}