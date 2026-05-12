/*
 * servidor.c — Nucleo de processamento distribuido
 * Sincronizacao: pthread_mutex_t (escrita) + sem_t (leitura)
 * Log: Todas as operacoes sao registradas em servidor.log
 *
 * MIGRADO: WinSock2 + Windows.h  →  POSIX sockets + pthreads
 *
 * CORRECOES:
 *  [1] LOG ESPURIO — carregar_dados() removido das threads, chamado
 *      apenas uma vez no main().
 *
 *  [2] RACE CONDITION leitura — mutex adquirido antes de ler lista[].
 *
 *  [3] MENSAGEM AMBIGUA — mensagens distintas para produto nao encontrado
 *      vs estoque insuficiente.
 *
 *  [4] HEALTHCHECK ESPURIO — op invalido descartado silenciosamente.
 *
 *  [5] ADMIN ACESSO DIRETO AO ARQUIVO — o admin agora se comunica com o
 *      servidor via socket usando novos opcodes, eliminando o conflito de
 *      dois processos gravando no mesmo arquivo simultaneamente:
 *        op=0  leitura (cliente lista produtos)
 *        op=2  compra  (cliente compra produto)
 *        op=10 admin: listar produtos
 *        op=11 admin: cadastrar produto
 *        op=12 admin: alterar nome
 *        op=13 admin: alterar preco
 *        op=14 admin: adicionar unidades
 *        op=15 admin: remover produto
 *
 *  Todos os logs originais foram preservados integralmente.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ── Estrutura compartilhada ───────────────────────────────────────── */
typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

/* ── Globais ───────────────────────────────────────────────────────── */
Produto lista[100];
int     total = 0;

pthread_mutex_t mutex_estoque    = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_log        = PTHREAD_MUTEX_INITIALIZER;
sem_t           semaforo_leitura;

FILE *arq_log = NULL;

/* ══════════════════════════════════════════════════════════════════════
 * LOG — thread-safe via pthread_mutex_t
 * ══════════════════════════════════════════════════════════════════════ */
void log_evento(const char *nivel, const char *msg) {
    time_t agora = time(NULL);
    struct tm *t  = localtime(&agora);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    pthread_mutex_lock(&mutex_log);
    printf("[%s][%s] %s\n", ts, nivel, msg);
    fflush(stdout);
    if (arq_log) {
        fprintf(arq_log, "[%s][%s] %s\n", ts, nivel, msg);
        fflush(arq_log);
    }
    pthread_mutex_unlock(&mutex_log);
}

void log_fmt(const char *nivel, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_evento(nivel, buf);
}

/* ══════════════════════════════════════════════════════════════════════
 * PERSISTENCIA
 * ══════════════════════════════════════════════════════════════════════ */
int comparar_id(const void *a, const void *b) {
    return ((Produto*)a)->id - ((Produto*)b)->id;
}

/* Chamado APENAS no main(), uma unica vez antes de qualquer thread */
void carregar_dados() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) {
        if (fread(&total, sizeof(int), 1, f) != 1) total = 0;
        else fread(lista, sizeof(Produto), total, f);
        fclose(f);
        qsort(lista, total, sizeof(Produto), comparar_id);
        log_fmt("DISK", "Estoque carregado: %d produto(s)", total);
    } else {
        total = 0;
        log_evento("WARN", "estoque.dat nao encontrado — iniciando vazio");
    }
}

/* Chamado sempre dentro do mutex_estoque */
void salvar_dados() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) {
        fwrite(&total, sizeof(int),     1,     f);
        fwrite(lista,  sizeof(Produto), total, f);
        fclose(f);
        log_fmt("DISK", "Estoque salvo: %d produto(s)", total);
    } else {
        log_evento("ERRO", "Falha ao abrir estoque.dat para escrita");
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * THREAD DE CLIENTE/ADMIN
 * ══════════════════════════════════════════════════════════════════════ */
void *tratar_cliente(void *arg) {
    int s = *(int*)arg;
    free(arg);

    pthread_t tid = pthread_self();
    log_fmt("CONN", "Thread %lu: nova conexao aceita", (unsigned long)tid);

    int req[3];
    if (recv(s, (char*)req, sizeof(req), 0) <= 0) {
        log_fmt("WARN", "Thread %lu: falha ao receber requisicao", (unsigned long)tid);
        close(s);
        return NULL;
    }

    /* FIX [4]: descarta conexoes com op invalido silenciosamente */
    if (req[0] != 0  && req[0] != 2  &&
        req[0] != 10 && req[0] != 11 &&
        req[0] != 12 && req[0] != 13 &&
        req[0] != 14 && req[0] != 15) {
        close(s);
        return NULL;
    }

    log_fmt("REQ", "Thread %lu: op=%d id=%d qtd=%d",
            (unsigned long)tid, req[0], req[1], req[2]);

    /* ── LEITURA CLIENTE (op == 0) ─────────────────────────────────── */
    if (req[0] == 0) {
        log_fmt("SEM", "Thread %lu: aguardando semaforo de leitura...", (unsigned long)tid);
        sem_wait(&semaforo_leitura);
        log_fmt("SEM", "Thread %lu: semaforo adquirido — leitura iniciada", (unsigned long)tid);

        /* FIX [2]: mutex antes de ler lista[], copia local antes do send */
        pthread_mutex_lock(&mutex_estoque);
        int t_local = total;
        Produto copia[100];
        memcpy(copia, lista, sizeof(Produto) * total);
        pthread_mutex_unlock(&mutex_estoque);

        send(s, (char*)&t_local, sizeof(int), 0);
        if (t_local > 0)
            send(s, (char*)copia, sizeof(Produto) * t_local, 0);

        log_fmt("SEM", "Thread %lu: leitura concluida — liberando semaforo (%d produto(s) enviados)",
                (unsigned long)tid, t_local);
        sem_post(&semaforo_leitura);
    }

    /* ── COMPRA CLIENTE (op == 2) ──────────────────────────────────── */
    else if (req[0] == 2) {
        log_fmt("MTX", "Thread %lu: aguardando mutex de escrita (id=%d qtd=%d)...",
                (unsigned long)tid, req[1], req[2]);
        pthread_mutex_lock(&mutex_estoque);
        log_fmt("MTX", "Thread %lu: mutex adquirido — regiao critica", (unsigned long)tid);

        char resposta[30] = "Erro: Prod nao encontrado";
        int processado = 0;

        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                if (lista[i].qtd >= req[2]) {
                    lista[i].qtd -= req[2];
                    strcpy(resposta, "Compra confirmada!");
                    salvar_dados();
                    log_fmt("SALE",
                            "Thread %lu: venda OK | produto '%s' (id=%d) | qtd=%d | saldo=%d",
                            (unsigned long)tid, lista[i].nome, lista[i].id, req[2], lista[i].qtd);
                } else {
                    strcpy(resposta, "Erro: Estoque insuficiente");
                    log_fmt("SALE",
                            "Thread %lu: venda RECUSADA | produto '%s' (id=%d) | pedido=%d | disponivel=%d",
                            (unsigned long)tid, lista[i].nome, lista[i].id, req[2], lista[i].qtd);
                }
                processado = 1;
                break;
            }
        }

        if (!processado)
            log_fmt("WARN", "Thread %lu: produto id=%d nao encontrado",
                    (unsigned long)tid, req[1]);

        send(s, resposta, 30, 0);
        log_fmt("MTX", "Thread %lu: liberando mutex", (unsigned long)tid);
        pthread_mutex_unlock(&mutex_estoque);
    }

    /* ── ADMIN: LISTAR (op == 10) ──────────────────────────────────── */
    else if (req[0] == 10) {
        log_fmt("ADMIN", "Thread %lu: listagem de produtos solicitada", (unsigned long)tid);
        pthread_mutex_lock(&mutex_estoque);
        int t_local = total;
        Produto copia[100];
        memcpy(copia, lista, sizeof(Produto) * total);
        pthread_mutex_unlock(&mutex_estoque);

        send(s, (char*)&t_local, sizeof(int), 0);
        if (t_local > 0)
            send(s, (char*)copia, sizeof(Produto) * t_local, 0);

        log_fmt("ADMIN", "Thread %lu: %d produto(s) enviados ao admin", (unsigned long)tid, t_local);
    }

    /* ── ADMIN: CADASTRAR (op == 11) ───────────────────────────────── */
    else if (req[0] == 11) {
        Produto np;
        memset(&np, 0, sizeof(np));
        if (recv(s, (char*)&np, sizeof(Produto), 0) <= 0) {
            log_fmt("WARN", "Thread %lu: falha ao receber dados do produto", (unsigned long)tid);
            close(s);
            return NULL;
        }

        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "OK: Produto cadastrado";
        int dup = 0;
        for (int i = 0; i < total; i++)
            if (lista[i].id == np.id) { dup = 1; break; }

        if (dup) {
            strcpy(resposta, "Erro: ID duplicado");
            log_fmt("ADMIN", "Thread %lu: cadastro recusado — id=%d duplicado", (unsigned long)tid, np.id);
        } else if (total < 100) {
            lista[total++] = np;
            salvar_dados();
            log_fmt("ADMIN", "Thread %lu: produto CADASTRADO | id=%d nome='%s' cat='%s' preco=%.2f qtd=%d",
                    (unsigned long)tid, np.id, np.nome, np.category, np.preco, np.qtd);
        } else {
            strcpy(resposta, "Erro: Estoque cheio");
            log_fmt("ADMIN", "Thread %lu: cadastro recusado — limite 100 atingido", (unsigned long)tid);
        }
        send(s, resposta, 30, 0);
        pthread_mutex_unlock(&mutex_estoque);
    }

    /* ── ADMIN: ALTERAR NOME (op == 12) ────────────────────────────── */
    else if (req[0] == 12) {
        char novo_nome[50];
        memset(novo_nome, 0, sizeof(novo_nome));
        if (recv(s, novo_nome, sizeof(novo_nome), 0) <= 0) {
            close(s); return NULL;
        }

        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu: nome ALTERADO | id=%d | '%s' -> '%s'",
                        (unsigned long)tid, lista[i].id, lista[i].nome, novo_nome);
                strncpy(lista[i].nome, novo_nome, 49);
                salvar_dados();
                strcpy(resposta, "OK: Nome alterado");
                break;
            }
        }
        send(s, resposta, 30, 0);
        pthread_mutex_unlock(&mutex_estoque);
    }

    /* ── ADMIN: ALTERAR PRECO (op == 13) ───────────────────────────── */
    else if (req[0] == 13) {
        float novo_preco;
        if (recv(s, (char*)&novo_preco, sizeof(float), 0) <= 0) {
            close(s); return NULL;
        }

        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu: preco ALTERADO | id=%d | %.2f -> %.2f",
                        (unsigned long)tid, lista[i].id, lista[i].preco, novo_preco);
                lista[i].preco = novo_preco;
                salvar_dados();
                strcpy(resposta, "OK: Preco alterado");
                break;
            }
        }
        send(s, resposta, 30, 0);
        pthread_mutex_unlock(&mutex_estoque);
    }

    /* ── ADMIN: ADICIONAR UNIDADES (op == 14) ──────────────────────── */
    else if (req[0] == 14) {
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                lista[i].qtd += req[2];
                salvar_dados();
                log_fmt("ADMIN", "Thread %lu: estoque SOMADO | id=%d nome='%s' | +%d | total=%d",
                        (unsigned long)tid, lista[i].id, lista[i].nome, req[2], lista[i].qtd);
                strcpy(resposta, "OK: Unidades adicionadas");
                break;
            }
        }
        send(s, resposta, 30, 0);
        pthread_mutex_unlock(&mutex_estoque);
    }

    /* ── ADMIN: REMOVER PRODUTO (op == 15) ─────────────────────────── */
    else if (req[0] == 15) {
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu: produto REMOVIDO | id=%d nome='%s'",
                        (unsigned long)tid, lista[i].id, lista[i].nome);
                for (int j = i; j < total - 1; j++)
                    lista[j] = lista[j + 1];
                total--;
                salvar_dados();
                strcpy(resposta, "OK: Produto removido");
                break;
            }
        }
        send(s, resposta, 30, 0);
        pthread_mutex_unlock(&mutex_estoque);
    }

    close(s);
    log_fmt("CONN", "Thread %lu: conexao encerrada", (unsigned long)tid);
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════ */
int main() {
    const char *log_dir = getenv("LOG_DIR");
    char log_path[256];
    if (log_dir && strlen(log_dir) > 0)
        snprintf(log_path, sizeof(log_path), "%s/servidor.log", log_dir);
    else
        snprintf(log_path, sizeof(log_path), "servidor.log");

    arq_log = fopen(log_path, "a");

    log_evento("INIT", "========================================");
    log_evento("INIT", "  SERVIDOR DISTRIBUIDO — PORTA 8085    ");
    log_evento("INIT", "========================================");

    carregar_dados();

    sem_init(&semaforo_leitura, 0, 2);
    log_evento("INIT", "Mutex e Semaforo POSIX criados (semaforo: 2 leitores simultaneos)");

    int servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor < 0) { log_evento("ERRO", "Falha ao criar socket"); return 1; }

    int opt = 1;
    setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family      = AF_INET;
    adr.sin_addr.s_addr = INADDR_ANY;
    adr.sin_port        = htons(8085);

    if (bind(servidor, (struct sockaddr*)&adr, sizeof(adr)) < 0) {
        log_evento("ERRO", "Falha no bind — porta 8085 em uso?");
        return 1;
    }

    listen(servidor, SOMAXCONN);
    log_evento("INIT", "Aguardando conexoes na porta 8085...");

    int conexoes_totais = 0;
    while (1) {
        int *p_cli = malloc(sizeof(int));
        *p_cli = accept(servidor, NULL, NULL);

        if (*p_cli >= 0) {
            conexoes_totais++;
            log_fmt("CONN", "Conexao #%d aceita — criando thread...", conexoes_totais);

            pthread_t thread;
            if (pthread_create(&thread, NULL, tratar_cliente, p_cli) == 0) {
                pthread_detach(thread);
            } else {
                log_evento("ERRO", "Falha ao criar thread — conexao descartada");
                close(*p_cli);
                free(p_cli);
            }
        } else {
            free(p_cli);
        }
    }

    sem_destroy(&semaforo_leitura);
    if (arq_log) fclose(arq_log);
    return 0;
}
