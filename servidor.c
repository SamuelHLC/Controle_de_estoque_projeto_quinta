/*
 * servidor.c — Nucleo de processamento distribuido
 * Sincronizacao: pthread_mutex_t (escrita) + sem_t (leitura)
 * Log: Todas as operacoes sao registradas em servidor.log
 *
 * CORRECOES ORIGINAIS:
 *  [1] LOG ESPURIO — carregar_dados() removido das threads
 *  [2] RACE CONDITION leitura — mutex adquirido antes de ler lista[]
 *  [3] MENSAGEM AMBIGUA — msgs distintas para prod nao encontrado vs insuficiente
 *  [4] HEALTHCHECK ESPURIO — op invalido descartado silenciosamente
 *  [5] ADMIN ACESSO DIRETO — admin usa socket TCP (opcodes 10-15)
 *
 * CORRECOES V2:
 *  [B1] localtime_r() — thread-safe
 *  [B2] recv() com loop em todos os opcodes
 *  [B3] send() com loop em todas as chamadas
 *  [B4] recv() do produto inteiro protegido com loop
 *
 * PARALELISMO V3 — FILA + WORKER:
 *  [P1] Fila circular de pedidos de compra (PedidoFila) com capacidade FILA_MAX
 *  [P2] Thread worker dedicada que consome a fila em background de forma continua
 *  [P3] Thread de conexao enfileira o pedido e retorna imediatamente (nao bloqueia)
 *  [P4] Worker processa, decrementa estoque e envia resposta diretamente ao cliente
 *  [P5] Sincronizacao da fila: mutex_fila + cond_fila (padrao produtor/consumidor)
 *
 *  Fluxo de compra (op=2):
 *    Thread conexao  →  enfileira pedido  →  libera para proxima conexao
 *    Worker thread   →  acorda            →  processa estoque → responde cliente
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

/* ── Estrutura de produto ──────────────────────────────────────────── */
typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

/* ══════════════════════════════════════════════════════════════════════
 * FILA DE PEDIDOS DE COMPRA
 * Padrao produtor/consumidor: threads de conexao produzem,
 * worker consome em background — execucao desacoplada da requisicao
 * ══════════════════════════════════════════════════════════════════════ */
#define FILA_MAX 256

typedef struct {
    int           socket_cliente;   /* socket aberto para responder    */
    int           produto_id;       /* id do produto a comprar         */
    int           quantidade;       /* quantidade solicitada           */
    unsigned long tid_origem;       /* thread que enfileirou (log)     */
} PedidoFila;

typedef struct {
    PedidoFila itens[FILA_MAX];
    int        inicio;
    int        fim;
    int        tamanho;
} FilaCompras;

FilaCompras     fila_compras  = {0};
pthread_mutex_t mutex_fila    = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond_fila     = PTHREAD_COND_INITIALIZER;

/* ── Globais de estoque ────────────────────────────────────────────── */
Produto         lista[100];
int             total = 0;

pthread_mutex_t mutex_estoque = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_log     = PTHREAD_MUTEX_INITIALIZER;
sem_t           semaforo_leitura;

FILE *arq_log = NULL;

/* ══════════════════════════════════════════════════════════════════════
 * HELPERS
 * ══════════════════════════════════════════════════════════════════════ */
static int recv_completo(int s, void *buf, int tam) {
    int lido = 0;
    char *ptr = (char*)buf;
    while (lido < tam) {
        int n = recv(s, ptr + lido, tam - lido, 0);
        if (n <= 0) return lido == 0 ? -1 : lido;
        lido += n;
    }
    return lido;
}

static int send_completo(int s, const void *buf, int tam) {
    int enviado = 0;
    const char *ptr = (const char*)buf;
    while (enviado < tam) {
        int n = send(s, ptr + enviado, tam - enviado, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        enviado += n;
    }
    return enviado;
}

/* ══════════════════════════════════════════════════════════════════════
 * LOG — thread-safe
 * ══════════════════════════════════════════════════════════════════════ */
void log_evento(const char *nivel, const char *msg) {
    time_t agora = time(NULL);
    struct tm t_buf;
    struct tm *t = localtime_r(&agora, &t_buf);
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
 * FILA — enfileirar (produtor) e desenfileirar (consumidor)
 * ══════════════════════════════════════════════════════════════════════ */
static int fila_enfileirar(PedidoFila pedido) {
    pthread_mutex_lock(&mutex_fila);

    if (fila_compras.tamanho >= FILA_MAX) {
        pthread_mutex_unlock(&mutex_fila);
        log_evento("FILA", "AVISO: fila cheia — pedido descartado");
        return 0;
    }

    fila_compras.itens[fila_compras.fim] = pedido;
    fila_compras.fim     = (fila_compras.fim + 1) % FILA_MAX;
    fila_compras.tamanho++;

    log_fmt("FILA", "Pedido enfileirado | produto_id=%d qtd=%d | fila=%d/%d",
            pedido.produto_id, pedido.quantidade,
            fila_compras.tamanho, FILA_MAX);

    pthread_cond_signal(&cond_fila);   /* acorda o worker */
    pthread_mutex_unlock(&mutex_fila);
    return 1;
}

static PedidoFila fila_desenfileirar() {
    /* mutex_fila ja adquirido pelo chamador */
    PedidoFila pedido        = fila_compras.itens[fila_compras.inicio];
    fila_compras.inicio      = (fila_compras.inicio + 1) % FILA_MAX;
    fila_compras.tamanho--;
    return pedido;
}

/* ══════════════════════════════════════════════════════════════════════
 * WORKER THREAD — processa compras em background (desacoplado)
 * Fica em loop aguardando sinais de cond_fila.
 * Quando ha pedido: consome, processa estoque, responde cliente.
 * ══════════════════════════════════════════════════════════════════════ */
void *worker_compras(void *arg) {
    (void)arg;
    pthread_t tid = pthread_self();
    log_fmt("WORKER", "Thread worker iniciada | tid=%lu | aguardando pedidos na fila...",
            (unsigned long)tid);

    while (1) {
        pthread_mutex_lock(&mutex_fila);

        /* Aguarda ate ter pedido na fila */
        while (fila_compras.tamanho == 0)
            pthread_cond_wait(&cond_fila, &mutex_fila);

        PedidoFila pedido    = fila_desenfileirar();
        int        restantes = fila_compras.tamanho;
        pthread_mutex_unlock(&mutex_fila);

        log_fmt("WORKER", "Processando pedido | produto_id=%d qtd=%d | fila restante=%d",
                pedido.produto_id, pedido.quantidade, restantes);

        /* Processa compra com mutex de estoque */
        pthread_mutex_lock(&mutex_estoque);

        char resposta[30] = "Erro: Prod nao encontrado";
        int  processado   = 0;

        for (int i = 0; i < total; i++) {
            if (lista[i].id == pedido.produto_id) {
                if (lista[i].qtd >= pedido.quantidade) {
                    lista[i].qtd -= pedido.quantidade;
                    strcpy(resposta, "Compra confirmada!");
                    salvar_dados();
                    log_fmt("WORKER",
                            "Venda OK | produto '%s' (id=%d) | qtd=%d | saldo=%d",
                            lista[i].nome, lista[i].id,
                            pedido.quantidade, lista[i].qtd);
                } else {
                    strcpy(resposta, "Erro: Estoque insuficiente");
                    log_fmt("WORKER",
                            "Venda RECUSADA | produto '%s' (id=%d) | pedido=%d | disponivel=%d",
                            lista[i].nome, lista[i].id,
                            pedido.quantidade, lista[i].qtd);
                }
                processado = 1;
                break;
            }
        }

        if (!processado)
            log_fmt("WORKER", "Produto id=%d nao encontrado", pedido.produto_id);

        pthread_mutex_unlock(&mutex_estoque);

        /* Responde ao cliente e fecha o socket */
        send_completo(pedido.socket_cliente, resposta, 30);
        close(pedido.socket_cliente);
        log_fmt("WORKER", "Resposta enviada e socket fechado | '%s'", resposta);
    }

    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * THREAD DE CONEXAO
 * Para compras (op=2): apenas enfileira e retorna — nao bloqueia
 * Para leitura e admin: processa diretamente (operacoes rapidas)
 * ══════════════════════════════════════════════════════════════════════ */
void *tratar_cliente(void *arg) {
    int s = *(int*)arg;
    free(arg);

    pthread_t tid = pthread_self();
    log_fmt("CONN", "Thread %lu: nova conexao aceita", (unsigned long)tid);

    int req[3];
    if (recv_completo(s, req, sizeof(req)) <= 0) {
        log_fmt("WARN", "Thread %lu: falha ao receber requisicao", (unsigned long)tid);
        close(s);
        return NULL;
    }

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
        log_fmt("SEM", "Thread %lu: aguardando semaforo...", (unsigned long)tid);
        sem_wait(&semaforo_leitura);
        log_fmt("SEM", "Thread %lu: semaforo adquirido — leitura iniciada", (unsigned long)tid);

        pthread_mutex_lock(&mutex_estoque);
        int t_local = total;
        Produto copia[100];
        memcpy(copia, lista, sizeof(Produto) * total);
        pthread_mutex_unlock(&mutex_estoque);

        send_completo(s, &t_local, sizeof(int));
        if (t_local > 0)
            send_completo(s, copia, sizeof(Produto) * t_local);

        log_fmt("SEM", "Thread %lu: leitura concluida — liberando semaforo (%d produto(s))",
                (unsigned long)tid, t_local);
        sem_post(&semaforo_leitura);
        close(s);
    }

    /* ── COMPRA CLIENTE (op == 2) — ENFILEIRA E RETORNA ───────────── */
    else if (req[0] == 2) {
        PedidoFila pedido;
        pedido.socket_cliente = s;
        pedido.produto_id     = req[1];
        pedido.quantidade     = req[2];
        pedido.tid_origem     = (unsigned long)tid;

        log_fmt("FILA", "Thread %lu: enfileirando compra | produto_id=%d qtd=%d",
                (unsigned long)tid, req[1], req[2]);

        if (!fila_enfileirar(pedido)) {
            char err[30] = "Erro: Servidor sobrecarregado";
            send_completo(s, err, 30);
            close(s);
        }
        /* socket NAO fechado aqui — worker fecha apos processar */
    }

    /* ── ADMIN: LISTAR (op == 10) ──────────────────────────────────── */
    else if (req[0] == 10) {
        log_fmt("ADMIN", "Thread %lu: listagem solicitada", (unsigned long)tid);
        pthread_mutex_lock(&mutex_estoque);
        int t_local = total;
        Produto copia[100];
        memcpy(copia, lista, sizeof(Produto) * total);
        pthread_mutex_unlock(&mutex_estoque);

        send_completo(s, &t_local, sizeof(int));
        if (t_local > 0)
            send_completo(s, copia, sizeof(Produto) * t_local);
        log_fmt("ADMIN", "Thread %lu: %d produto(s) enviados", (unsigned long)tid, t_local);
        close(s);
    }

    /* ── ADMIN: CADASTRAR (op == 11) ───────────────────────────────── */
    else if (req[0] == 11) {
        Produto np;
        memset(&np, 0, sizeof(np));
        if (recv_completo(s, &np, sizeof(Produto)) <= 0) {
            close(s); return NULL;
        }
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "OK: Produto cadastrado";
        int dup = 0;
        for (int i = 0; i < total; i++)
            if (lista[i].id == np.id) { dup = 1; break; }

        if (dup) {
            strcpy(resposta, "Erro: ID duplicado");
        } else if (total < 100) {
            lista[total++] = np;
            salvar_dados();
            log_fmt("ADMIN", "Thread %lu: CADASTRADO | id=%d nome='%s' preco=%.2f qtd=%d",
                    (unsigned long)tid, np.id, np.nome, np.preco, np.qtd);
        } else {
            strcpy(resposta, "Erro: Estoque cheio");
        }
        send_completo(s, resposta, 30);
        pthread_mutex_unlock(&mutex_estoque);
        close(s);
    }

    /* ── ADMIN: ALTERAR NOME (op == 12) ────────────────────────────── */
    else if (req[0] == 12) {
        char novo_nome[50];
        memset(novo_nome, 0, sizeof(novo_nome));
        if (recv_completo(s, novo_nome, sizeof(novo_nome)) <= 0) {
            close(s); return NULL;
        }
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu: nome ALTERADO | '%s' -> '%s'",
                        (unsigned long)tid, lista[i].nome, novo_nome);
                strncpy(lista[i].nome, novo_nome, 49);
                lista[i].nome[49] = '\0';
                salvar_dados();
                strcpy(resposta, "OK: Nome alterado");
                break;
            }
        }
        send_completo(s, resposta, 30);
        pthread_mutex_unlock(&mutex_estoque);
        close(s);
    }

    /* ── ADMIN: ALTERAR PRECO (op == 13) ───────────────────────────── */
    else if (req[0] == 13) {
        float novo_preco;
        if (recv_completo(s, &novo_preco, sizeof(float)) <= 0) {
            close(s); return NULL;
        }
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu: preco ALTERADO | %.2f -> %.2f",
                        (unsigned long)tid, lista[i].preco, novo_preco);
                lista[i].preco = novo_preco;
                salvar_dados();
                strcpy(resposta, "OK: Preco alterado");
                break;
            }
        }
        send_completo(s, resposta, 30);
        pthread_mutex_unlock(&mutex_estoque);
        close(s);
    }

    /* ── ADMIN: ADICIONAR UNIDADES (op == 14) ──────────────────────── */
    else if (req[0] == 14) {
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                lista[i].qtd += req[2];
                salvar_dados();
                log_fmt("ADMIN", "Thread %lu: SOMADO | id=%d | +%d | total=%d",
                        (unsigned long)tid, lista[i].id, req[2], lista[i].qtd);
                strcpy(resposta, "OK: Unidades adicionadas");
                break;
            }
        }
        send_completo(s, resposta, 30);
        pthread_mutex_unlock(&mutex_estoque);
        close(s);
    }

    /* ── ADMIN: REMOVER PRODUTO (op == 15) ─────────────────────────── */
    else if (req[0] == 15) {
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu: REMOVIDO | id=%d nome='%s'",
                        (unsigned long)tid, lista[i].id, lista[i].nome);
                for (int j = i; j < total - 1; j++)
                    lista[j] = lista[j + 1];
                total--;
                salvar_dados();
                strcpy(resposta, "OK: Produto removido");
                break;
            }
        }
        send_completo(s, resposta, 30);
        pthread_mutex_unlock(&mutex_estoque);
        close(s);
    }

    log_fmt("CONN", "Thread %lu: requisicao concluida", (unsigned long)tid);
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
    log_evento("INIT", "Semaforo POSIX criado (2 leitores simultaneos)");

    /* [P2] Inicia worker — processa fila de compras em background */
    pthread_t thread_worker;
    if (pthread_create(&thread_worker, NULL, worker_compras, NULL) != 0) {
        log_evento("ERRO", "Falha ao criar thread worker — abortando");
        return 1;
    }
    pthread_detach(thread_worker);
    log_evento("INIT", "Thread worker iniciada — fila de compras ativa (FILA_MAX=256)");
    log_evento("INIT", "Arquitetura: conexao → fila → worker (desacoplado)");

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
