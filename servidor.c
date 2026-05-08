/*
 * ============================================================
 *  SERVIDOR DISTRIBUIDO - CONTROLE DE ESTOQUE v7.0
 *  Projeto Integrador de Computacao Paralela - UNIEURO
 * ============================================================
 *  ARQUITETURA:
 *   - Concorrencia: Mutex (escrita) + Semaforo (leitura)
 *   - Paralelismo:  Fila de tarefas (Task Queue) + Worker Thread
 *                   O worker processa logs de forma INDEPENDENTE
 *                   da thread principal, em background.
 *   - Distribuicao: Sockets TCP/IP (porta 8085)
 *                   Cada cliente gera uma thread exclusiva.
 * ============================================================
 *  COMPILAR: gcc servidor.c -o servidor -lws2_32
 * ============================================================
 */

#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

/* ─────────────────────────────────────────────
   ESTRUTURA DE DADOS
   ───────────────────────────────────────────── */
typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

/* ─────────────────────────────────────────────
   FILA DE LOG (PARALELISMO REAL)
   - Produtor: threads de atendimento (req[0]==2)
   - Consumidor: worker_log (thread dedicada)
   - Sincronizacao: mutex_fila + semaforo_fila
   ───────────────────────────────────────────── */
#define FILA_MAX 256

typedef struct {
    char  mensagem[128];
    DWORD thread_id;
    int   produto_id;
    int   quantidade;
    int   sucesso;          /* 1 = confirmada | 0 = esgotado */
    time_t timestamp;
} LogEntry;

LogEntry   fila_log[FILA_MAX];
int        fila_head  = 0;
int        fila_tail  = 0;
int        fila_count = 0;
HANDLE     mutex_fila;          /* protege head/tail/count */
HANDLE     sem_fila_cheia;      /* bloqueia produtor se fila cheia  */
HANDLE     sem_fila_vazia;      /* acorda worker quando ha item     */
int        servidor_rodando = 1;

/* ─────────────────────────────────────────────
   ESTADO GLOBAL DO ESTOQUE
   ───────────────────────────────────────────── */
Produto lista[100];
int     total = 0;
HANDLE  mutex_estoque;      /* exclusao mutua em escrita */
HANDLE  sem_leitura;        /* ate 2 leitores simultaneos */

/* contadores de benchmark */
volatile long total_compras_ok  = 0;
volatile long total_compras_err = 0;
HANDLE        mutex_bench;

/* ─────────────────────────────────────────────
   UTILIDADES
   ───────────────────────────────────────────── */
int comparar_id(const void *a, const void *b) {
    return ((Produto*)a)->id - ((Produto*)b)->id;
}

void salvar_dados() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) {
        fwrite(&total, sizeof(int),     1,     f);
        fwrite(lista,  sizeof(Produto), total, f);
        fclose(f);
    }
}

void carregar_dados() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) {
        if (fread(&total, sizeof(int), 1, f) != 1) total = 0;
        else fread(lista, sizeof(Produto), total, f);
        fclose(f);
    } else {
        total = 0;
    }
}

/* ─────────────────────────────────────────────
   FILA: ENQUEUE (chamado pelas threads cliente)
   ───────────────────────────────────────────── */
void enqueue_log(LogEntry *entry) {
    /* sem_fila_cheia bloqueia se fila estiver cheia */
    WaitForSingleObject(sem_fila_cheia, INFINITE);

    WaitForSingleObject(mutex_fila, INFINITE);
    fila_log[fila_tail] = *entry;
    fila_tail = (fila_tail + 1) % FILA_MAX;
    fila_count++;
    ReleaseMutex(mutex_fila);

    /* acorda o worker */
    ReleaseSemaphore(sem_fila_vazia, 1, NULL);
}

/* ─────────────────────────────────────────────
   FILA: DEQUEUE (chamado somente pelo worker)
   ───────────────────────────────────────────── */
int dequeue_log(LogEntry *out) {
    WaitForSingleObject(mutex_fila, INFINITE);
    if (fila_count == 0) {
        ReleaseMutex(mutex_fila);
        return 0;
    }
    *out = fila_log[fila_head];
    fila_head = (fila_head + 1) % FILA_MAX;
    fila_count--;
    ReleaseMutex(mutex_fila);

    /* libera espaco para produtor */
    ReleaseSemaphore(sem_fila_cheia, 1, NULL);
    return 1;
}

/* ─────────────────────────────────────────────
   WORKER THREAD DE LOG (PARALELISMO)
   - Roda em background, independente das requisicoes
   - Consome a fila e grava em log_operacoes.txt
   ───────────────────────────────────────────── */
DWORD WINAPI worker_log(LPVOID lpParam) {
    (void)lpParam;
    FILE *arq_log = fopen("log_operacoes.txt", "a");

    printf("[WORKER] Thread de log iniciada (TID=%d)\n", GetCurrentThreadId());
    printf("[WORKER] Gravando em: log_operacoes.txt\n\n");

    while (servidor_rodando || fila_count > 0) {
        /* dorme ate chegar um item na fila */
        if (WaitForSingleObject(sem_fila_vazia, 500) == WAIT_TIMEOUT) {
            continue; /* timeout -> verifica servidor_rodando */
        }

        LogEntry e;
        if (!dequeue_log(&e)) continue;

        /* formata timestamp */
        char buf_time[32];
        struct tm *tm_info = localtime(&e.timestamp);
        strftime(buf_time, sizeof(buf_time), "%Y-%m-%d %H:%M:%S", tm_info);

        /* grava no arquivo */
        if (arq_log) {
            fprintf(arq_log,
                "[%s] TID=%-6d | ProdID=%03d | Qtd=%d | Status=%s\n",
                buf_time, e.thread_id, e.produto_id,
                e.quantidade, e.sucesso ? "CONFIRMADA" : "ESGOTADO");
            fflush(arq_log);
        }

        /* exibe no servidor */
        printf("[LOG-WORKER] %s | TID=%-6d | Prod=%03d | %s\n",
               buf_time, e.thread_id, e.produto_id,
               e.sucesso ? "OK" : "ESGOTADO");
    }

    if (arq_log) fclose(arq_log);
    printf("[WORKER] Thread de log encerrada.\n");
    return 0;
}

/* ─────────────────────────────────────────────
   THREAD POR CLIENTE
   ───────────────────────────────────────────── */
DWORD WINAPI tratar_cliente(LPVOID lpParam) {
    SOCKET s = *(SOCKET*)lpParam;
    free(lpParam);

    int req[3];
    if (recv(s, (char*)req, sizeof(req), 0) <= 0) {
        closesocket(s);
        return 0;
    }

    /* ── LEITURA (req[0] == 0) ───────────────── */
    if (req[0] == 0) {
        /* semaforo permite ate 2 leitores simultaneos */
        WaitForSingleObject(sem_leitura, INFINITE);

        carregar_dados();
        qsort(lista, total, sizeof(Produto), comparar_id);
        send(s, (char*)&total, sizeof(int), 0);
        if (total > 0)
            send(s, (char*)lista, sizeof(Produto) * total, 0);

        ReleaseSemaphore(sem_leitura, 1, NULL);
    }

    /* ── COMPRA/ESCRITA (req[0] == 2) ───────── */
    else if (req[0] == 2) {
        printf("[MTX] Thread %d: Aguardando Mutex...\n", GetCurrentThreadId());
        WaitForSingleObject(mutex_estoque, INFINITE);

        carregar_dados();
        printf("[MTX] Thread %d: EM SECAO CRITICA - Produto ID %d\n",
               GetCurrentThreadId(), req[1]);

        char resposta[30] = "Erro: Produto esgotado";
        int  sucesso = 0;

        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1] && lista[i].qtd >= req[2]) {
                lista[i].qtd -= req[2];
                strcpy(resposta, "Compra confirmada!");
                salvar_dados();
                sucesso = 1;
                break;
            }
        }

        send(s, resposta, 30, 0);
        printf("[MTX] Thread %d: Liberando Mutex.\n", GetCurrentThreadId());
        ReleaseMutex(mutex_estoque);

        /* ── ENFILEIRA LOG (desacoplado do atendimento) ── */
        LogEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.thread_id  = GetCurrentThreadId();
        entry.produto_id = req[1];
        entry.quantidade = req[2];
        entry.sucesso    = sucesso;
        entry.timestamp  = time(NULL);
        snprintf(entry.mensagem, sizeof(entry.mensagem),
                 "Compra Produto=%03d Qtd=%d -> %s",
                 req[1], req[2], sucesso ? "OK" : "ESGOTADO");
        enqueue_log(&entry);

        /* atualiza benchmark de forma atomica */
        WaitForSingleObject(mutex_bench, INFINITE);
        if (sucesso) total_compras_ok++;
        else         total_compras_err++;
        ReleaseMutex(mutex_bench);
    }

    closesocket(s);
    return 0;
}

/* ─────────────────────────────────────────────
   THREAD DE BENCHMARK (imprime metricas a cada 5s)
   ───────────────────────────────────────────── */
DWORD WINAPI thread_benchmark(LPVOID lpParam) {
    (void)lpParam;
    time_t inicio = time(NULL);
    while (servidor_rodando) {
        Sleep(5000);
        double elapsed = difftime(time(NULL), inicio);
        long   ok  = total_compras_ok;
        long   err = total_compras_err;
        long   tot = ok + err;
        printf("\n[BENCH] Uptime: %.0fs | Compras OK: %ld | Esgotados: %ld"
               " | Throughput: %.2f op/s\n\n",
               elapsed, ok, err, elapsed > 0 ? tot / elapsed : 0.0);
    }
    return 0;
}

/* ─────────────────────────────────────────────
   MAIN
   ───────────────────────────────────────────── */
int main() {
    WSADATA w;
    WSAStartup(0x0202, &w);
    carregar_dados();

    /* --- primitivas de sincronizacao --- */
    mutex_estoque = CreateMutex(NULL, FALSE, NULL);
    sem_leitura   = CreateSemaphore(NULL, 2, 2, NULL); /* max 2 leitores */
    mutex_fila    = CreateMutex(NULL, FALSE, NULL);
    mutex_bench   = CreateMutex(NULL, FALSE, NULL);
    sem_fila_cheia = CreateSemaphore(NULL, FILA_MAX, FILA_MAX, NULL);
    sem_fila_vazia = CreateSemaphore(NULL, 0,        FILA_MAX, NULL);

    /* --- inicia worker de log (paralelismo desacoplado) --- */
    CreateThread(NULL, 0, worker_log,        NULL, 0, NULL);
    CreateThread(NULL, 0, thread_benchmark,  NULL, 0, NULL);

    /* --- socket do servidor --- */
    SOCKET servidor = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family      = AF_INET;
    adr.sin_addr.s_addr = INADDR_ANY;
    adr.sin_port        = htons(8085);

    bind(servidor,   (struct sockaddr*)&adr, sizeof(adr));
    listen(servidor, SOMAXCONN);

    printf("=======================================================\n");
    printf("  SERVIDOR DISTRIBUIDO - PORTA 8085                    \n");
    printf("  [Concorrencia] Mutex (escrita) + Semaforo (leitura)  \n");
    printf("  [Paralelismo]  Fila de Log + Worker Thread           \n");
    printf("  [Rede]         TCP/IP  |  Multi-Thread por cliente   \n");
    printf("=======================================================\n\n");

    while (1) {
        SOCKET *p = malloc(sizeof(SOCKET));
        *p = accept(servidor, 0, 0);
        if (*p != INVALID_SOCKET)
            CreateThread(NULL, 0, tratar_cliente, p, 0, NULL);
        else
            free(p);
    }

    servidor_rodando = 0;
    WSACleanup();
    return 0;
}