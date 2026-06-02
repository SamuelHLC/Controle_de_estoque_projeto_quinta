/*
 * servidor.c — Nucleo de processamento distribuido
 * Sincronizacao: pthread_mutex_t (escrita) + sem_t (leitura)
 *
 * COMPORTAMENTO:
 *  - Servidor sobe completamente silencioso
 *  - Log so aparece quando ha acao real
 *  - Labels corretos: "cliente" para op 0/2, "admin" para op 10-15
 *  - req[4] na compra: {op, produto_id, quantidade, usuario_id}
 *    Log mostra: cliente=IP usuario=#N
 *
 * PARALELISMO:
 *  - Fila circular FILA_MAX=256
 *  - Worker thread em background
 *  - Thread de conexao enfileira e retorna imediatamente
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
#include <ifaddrs.h>

typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

#define FILA_MAX 256

typedef struct {
    int           socket_cliente;
    int           produto_id;
    int           quantidade;
    int           usuario_id;
    unsigned long tid_origem;
    char          ip_origem[INET_ADDRSTRLEN];
} PedidoFila;

typedef struct {
    PedidoFila itens[FILA_MAX];
    int        inicio, fim, tamanho;
} FilaCompras;

FilaCompras     fila_compras = {0};
pthread_mutex_t mutex_fila   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond_fila    = PTHREAD_COND_INITIALIZER;

Produto         lista[100];
int             total = 0;

pthread_mutex_t mutex_estoque = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_log     = PTHREAD_MUTEX_INITIALIZER;
sem_t           semaforo_leitura;

FILE *arq_log                      = NULL;
char  ip_servidor[INET_ADDRSTRLEN] = "desconhecido";

/* ══════════════════════════════════════════════════════════════════════
 * HELPERS
 * ══════════════════════════════════════════════════════════════════════ */
static int recv_completo(int s, void *buf, int tam) {
    int lido = 0; char *ptr = (char*)buf;
    while (lido < tam) {
        int n = recv(s, ptr + lido, tam - lido, 0);
        if (n <= 0) return lido == 0 ? -1 : lido;
        lido += n;
    }
    return lido;
}

static int send_completo(int s, const void *buf, int tam) {
    int env = 0; const char *ptr = (const char*)buf;
    while (env < tam) {
        int n = send(s, ptr + env, tam - env, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        env += n;
    }
    return env;
}

static void descobrir_ip(char *out, int len) {
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) == -1) { strncpy(out, "127.0.0.1", len); return; }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        char tmp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, tmp, sizeof(tmp));
        if (strcmp(tmp, "127.0.0.1") != 0) {
            strncpy(out, tmp, len);
            freeifaddrs(ifap);
            return;
        }
    }
    strncpy(out, "127.0.0.1", len);
    freeifaddrs(ifap);
}

/* ══════════════════════════════════════════════════════════════════════
 * LOG
 * ══════════════════════════════════════════════════════════════════════ */
void log_evento(const char *nivel, const char *msg) {
    time_t agora = time(NULL);
    struct tm tb; struct tm *t = localtime_r(&agora, &tb);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
    pthread_mutex_lock(&mutex_log);
    printf("[%s][%s][servidor=%s] %s\n", ts, nivel, ip_servidor, msg);
    fflush(stdout);
    if (arq_log) { fprintf(arq_log, "[%s][%s][servidor=%s] %s\n", ts, nivel, ip_servidor, msg); fflush(arq_log); }
    pthread_mutex_unlock(&mutex_log);
}

void log_fmt(const char *nivel, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    log_evento(nivel, buf);
}

/* ══════════════════════════════════════════════════════════════════════
 * PERSISTENCIA
 * ══════════════════════════════════════════════════════════════════════ */
static int cmp_id(const void *a, const void *b) { return ((Produto*)a)->id - ((Produto*)b)->id; }

void carregar_dados() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) {
        if (fread(&total, sizeof(int), 1, f) != 1) total = 0;
        else fread(lista, sizeof(Produto), total, f);
        fclose(f);
        qsort(lista, total, sizeof(Produto), cmp_id);
    } else { total = 0; }
}

void salvar_dados() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) { fwrite(&total, sizeof(int), 1, f); fwrite(lista, sizeof(Produto), total, f); fclose(f); }
}

static int proximo_id() {
    int max = 0;
    for (int i = 0; i < total; i++) if (lista[i].id > max) max = lista[i].id;
    return max + 1;
}

/* ══════════════════════════════════════════════════════════════════════
 * FILA
 * ══════════════════════════════════════════════════════════════════════ */
static int fila_enfileirar(PedidoFila p) {
    pthread_mutex_lock(&mutex_fila);
    if (fila_compras.tamanho >= FILA_MAX) {
        pthread_mutex_unlock(&mutex_fila);
        log_fmt("FILA", "Fila cheia — pedido descartado | cliente=%s usuario=#%d",
                p.ip_origem, p.usuario_id);
        return 0;
    }
    fila_compras.itens[fila_compras.fim] = p;
    fila_compras.fim = (fila_compras.fim + 1) % FILA_MAX;
    fila_compras.tamanho++;
    if (p.usuario_id > 0)
        log_fmt("FILA", "Enfileirado | cliente=%s usuario=#%d produto_id=%d qtd=%d | fila=%d/%d",
                p.ip_origem, p.usuario_id, p.produto_id, p.quantidade,
                fila_compras.tamanho, FILA_MAX);
    else
        log_fmt("FILA", "Enfileirado | cliente=%s produto_id=%d qtd=%d | fila=%d/%d",
                p.ip_origem, p.produto_id, p.quantidade,
                fila_compras.tamanho, FILA_MAX);
    pthread_cond_signal(&cond_fila);
    pthread_mutex_unlock(&mutex_fila);
    return 1;
}

static PedidoFila fila_desenfileirar() {
    PedidoFila p = fila_compras.itens[fila_compras.inicio];
    fila_compras.inicio = (fila_compras.inicio + 1) % FILA_MAX;
    fila_compras.tamanho--;
    return p;
}

/* ══════════════════════════════════════════════════════════════════════
 * WORKER — silenciosa ate chegar pedido
 * ══════════════════════════════════════════════════════════════════════ */
void *worker_compras(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&mutex_fila);
        while (fila_compras.tamanho == 0)
            pthread_cond_wait(&cond_fila, &mutex_fila);
        PedidoFila pedido = fila_desenfileirar();
        int restantes     = fila_compras.tamanho;
        pthread_mutex_unlock(&mutex_fila);

        if (pedido.usuario_id > 0)
            log_fmt("WORKER", "Processando | cliente=%s usuario=#%d produto_id=%d qtd=%d | fila restante=%d",
                    pedido.ip_origem, pedido.usuario_id, pedido.produto_id, pedido.quantidade, restantes);
        else
            log_fmt("WORKER", "Processando | cliente=%s produto_id=%d qtd=%d | fila restante=%d",
                    pedido.ip_origem, pedido.produto_id, pedido.quantidade, restantes);

        log_fmt("MUTEX", "Travando estoque | cliente=%s", pedido.ip_origem);
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30] = "Erro: Prod nao encontrado";
        int  ok = 0;

        for (int i = 0; i < total; i++) {
            if (lista[i].id == pedido.produto_id) {
                if (lista[i].qtd >= pedido.quantidade) {
                    lista[i].qtd -= pedido.quantidade;
                    strcpy(resposta, "Compra confirmada!");
                    salvar_dados();
                    if (pedido.usuario_id > 0)
                        log_fmt("WORKER", "Venda OK | cliente=%s usuario=#%d produto='%s' qtd=%d saldo=%d",
                                pedido.ip_origem, pedido.usuario_id,
                                lista[i].nome, pedido.quantidade, lista[i].qtd);
                    else
                        log_fmt("WORKER", "Venda OK | cliente=%s produto='%s' qtd=%d saldo=%d",
                                pedido.ip_origem, lista[i].nome, pedido.quantidade, lista[i].qtd);
                } else {
                    strcpy(resposta, "Erro: Estoque insuficiente");
                    if (pedido.usuario_id > 0)
                        log_fmt("WORKER", "Venda RECUSADA | cliente=%s usuario=#%d produto='%s' pedido=%d disponivel=%d",
                                pedido.ip_origem, pedido.usuario_id,
                                lista[i].nome, pedido.quantidade, lista[i].qtd);
                    else
                        log_fmt("WORKER", "Venda RECUSADA | cliente=%s produto='%s' pedido=%d disponivel=%d",
                                pedido.ip_origem, lista[i].nome, pedido.quantidade, lista[i].qtd);
                }
                ok = 1; break;
            }
        }
        if (!ok) log_fmt("WORKER", "Produto id=%d nao encontrado | cliente=%s", pedido.produto_id, pedido.ip_origem);

        pthread_mutex_unlock(&mutex_estoque);
        log_fmt("MUTEX", "Estoque liberado | cliente=%s", pedido.ip_origem);
        send_completo(pedido.socket_cliente, resposta, 30);
        close(pedido.socket_cliente);
        log_fmt("WORKER", "Resposta enviada | cliente=%s resp='%s'", pedido.ip_origem, resposta);
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * THREAD DE CONEXAO
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    int  socket;
    char ip[INET_ADDRSTRLEN];
    int  req[4];   /* [op, id, qtd, usuario_id] */
    int  eh_admin;
} ConexaoCtx;

void *tratar_conexao(void *arg) {
    ConexaoCtx *ctx = (ConexaoCtx*)arg;
    int  s        = ctx->socket;
    char ip[INET_ADDRSTRLEN];
    int  req[4];
    int  eh_admin = ctx->eh_admin;
    strncpy(ip, ctx->ip, INET_ADDRSTRLEN);
    req[0]=ctx->req[0]; req[1]=ctx->req[1]; req[2]=ctx->req[2]; req[3]=ctx->req[3];
    free(ctx);

    const char *label = eh_admin ? "admin" : "cliente";
    pthread_t   tid   = pthread_self();

    /* Nao loga thread para consultas rotineiras (op=0 leitura, op=10 listagem admin) */
    if (req[0] != 0 && req[0] != 10)
        log_fmt("CONN", "Thread %lu | %s=%s | op=%d",
                (unsigned long)tid, label, ip, req[0]);

    /* ── LEITURA (op=0) ────────────────────────────────────────────── */
    if (req[0] == 0) {
        int sval = 0; sem_getvalue(&semaforo_leitura, &sval);
        /* Loga apenas se houver contencao real (sem vaga disponivel) */
        if (sval == 0)
            log_fmt("SEM", "Leitura aguardando vaga | cliente=%s | sem vagas disponiveis", ip);
        sem_wait(&semaforo_leitura);
        pthread_mutex_lock(&mutex_estoque);
        int t_local = total; Produto copia[100];
        memcpy(copia, lista, sizeof(Produto) * total);
        pthread_mutex_unlock(&mutex_estoque);
        send_completo(s, &t_local, sizeof(int));
        if (t_local > 0) send_completo(s, copia, sizeof(Produto) * t_local);
        sem_post(&semaforo_leitura);
        close(s);
    }

    /* ── COMPRA (op=2) — req[4]={2, produto_id, quantidade, usuario_id} */
    else if (req[0] == 2) {
        PedidoFila pedido;
        pedido.socket_cliente = s;
        pedido.produto_id     = req[1];
        pedido.quantidade     = req[2];
        pedido.usuario_id     = req[3];
        pedido.tid_origem     = (unsigned long)tid;
        strncpy(pedido.ip_origem, ip, INET_ADDRSTRLEN);
        if (!fila_enfileirar(pedido)) {
            char err[30] = "Erro: Servidor sobrecarregado";
            send_completo(s, err, 30); close(s);
        }
    }

    /* ── ADMIN: LISTAR (op=10) ─────────────────────────────────────── */
    else if (req[0] == 10) {
        pthread_mutex_lock(&mutex_estoque);
        int t_local = total; Produto copia[100];
        memcpy(copia, lista, sizeof(Produto) * total);
        pthread_mutex_unlock(&mutex_estoque);
        send_completo(s, &t_local, sizeof(int));
        if (t_local > 0) send_completo(s, copia, sizeof(Produto) * t_local);
        close(s);
    }

    /* ── ADMIN: CADASTRAR (op=11) ──────────────────────────────────── */
    else if (req[0] == 11) {
        Produto np; memset(&np, 0, sizeof(np));
        if (recv_completo(s, &np, sizeof(Produto)) <= 0) { close(s); return NULL; }
        log_fmt("MUTEX", "Travando estoque | admin=%s | op=cadastrar", ip);
        pthread_mutex_lock(&mutex_estoque);
        char resposta[30];
        if (total < 100) {
            np.id = proximo_id();
            lista[total++] = np; salvar_dados();
            log_fmt("ADMIN", "Thread %lu | admin=%s | CADASTRADO id=%d nome='%s' preco=%.2f qtd=%d",
                    (unsigned long)tid, ip, np.id, np.nome, np.preco, np.qtd);
            snprintf(resposta, sizeof(resposta), "OK: Cadastrado ID=%d", np.id);
        } else { strcpy(resposta, "Erro: Estoque cheio"); }
        send_completo(s, resposta, 30);
        pthread_mutex_unlock(&mutex_estoque);
        log_fmt("MUTEX", "Estoque liberado | admin=%s | op=cadastrar", ip);
        close(s);
    }

    /* ── ADMIN: ALTERAR NOME (op=12) ───────────────────────────────── */
    else if (req[0] == 12) {
        char novo[50]; memset(novo, 0, sizeof(novo));
        if (recv_completo(s, novo, sizeof(novo)) <= 0) { close(s); return NULL; }
        log_fmt("MUTEX", "Travando estoque | admin=%s | op=alterar-nome", ip);
        pthread_mutex_lock(&mutex_estoque);
        char resp[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu | admin=%s | nome '%s'->'%s'",
                        (unsigned long)tid, ip, lista[i].nome, novo);
                strncpy(lista[i].nome, novo, 49); lista[i].nome[49]='\0';
                salvar_dados(); strcpy(resp, "OK: Nome alterado"); break;
            }
        }
        send_completo(s, resp, 30);
        pthread_mutex_unlock(&mutex_estoque);
        log_fmt("MUTEX", "Estoque liberado | admin=%s | op=alterar-nome", ip);
        close(s);
    }

    /* ── ADMIN: ALTERAR PRECO (op=13) ──────────────────────────────── */
    else if (req[0] == 13) {
        float novo;
        if (recv_completo(s, &novo, sizeof(float)) <= 0) { close(s); return NULL; }
        log_fmt("MUTEX", "Travando estoque | admin=%s | op=alterar-preco", ip);
        pthread_mutex_lock(&mutex_estoque);
        char resp[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu | admin=%s | preco %.2f->%.2f",
                        (unsigned long)tid, ip, lista[i].preco, novo);
                lista[i].preco = novo; salvar_dados(); strcpy(resp, "OK: Preco alterado"); break;
            }
        }
        send_completo(s, resp, 30);
        pthread_mutex_unlock(&mutex_estoque);
        log_fmt("MUTEX", "Estoque liberado | admin=%s | op=alterar-preco", ip);
        close(s);
    }

    /* ── ADMIN: ADICIONAR UNIDADES (op=14) ─────────────────────────── */
    else if (req[0] == 14) {
        log_fmt("MUTEX", "Travando estoque | admin=%s | op=adicionar-unidades", ip);
        pthread_mutex_lock(&mutex_estoque);
        char resp[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                lista[i].qtd += req[2]; salvar_dados();
                log_fmt("ADMIN", "Thread %lu | admin=%s | SOMADO id=%d +%d total=%d",
                        (unsigned long)tid, ip, lista[i].id, req[2], lista[i].qtd);
                strcpy(resp, "OK: Unidades adicionadas"); break;
            }
        }
        send_completo(s, resp, 30);
        pthread_mutex_unlock(&mutex_estoque);
        log_fmt("MUTEX", "Estoque liberado | admin=%s | op=adicionar-unidades", ip);
        close(s);
    }

    /* ── ADMIN: REMOVER (op=15) ────────────────────────────────────── */
    else if (req[0] == 15) {
        log_fmt("MUTEX", "Travando estoque | admin=%s | op=remover", ip);
        pthread_mutex_lock(&mutex_estoque);
        char resp[30] = "Erro: Prod nao encontrado";
        for (int i = 0; i < total; i++) {
            if (lista[i].id == req[1]) {
                log_fmt("ADMIN", "Thread %lu | admin=%s | REMOVIDO id=%d nome='%s'",
                        (unsigned long)tid, ip, lista[i].id, lista[i].nome);
                for (int j = i; j < total-1; j++) lista[j] = lista[j+1];
                total--; salvar_dados(); strcpy(resp, "OK: Produto removido"); break;
            }
        }
        send_completo(s, resp, 30);
        pthread_mutex_unlock(&mutex_estoque);
        log_fmt("MUTEX", "Estoque liberado | admin=%s | op=remover", ip);
        close(s);
    }

    if (req[0] != 0 && req[0] != 10)
        log_fmt("CONN", "Thread %lu | %s=%s | concluida", (unsigned long)tid, label, ip);
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * LOOP DE ACCEPT — thread background
 * req[4] lido antes de criar thread — opcode invalido descartado
 * ══════════════════════════════════════════════════════════════════════ */
void *loop_accept(void *arg) {
    int fd_srv = *(int*)arg; free(arg);
    int conexoes = 0;

    while (1) {
        struct sockaddr_in adr_cli;
        socklen_t len = sizeof(adr_cli);
        int fd = accept(fd_srv, (struct sockaddr*)&adr_cli, &len);
        if (fd < 0) continue;

        char ip_cli[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &adr_cli.sin_addr, ip_cli, sizeof(ip_cli));

        /* Le req[4] antes de criar thread */
        int req[4] = {0,0,0,0};
        if (recv_completo(fd, req, sizeof(req)) <= 0) { close(fd); continue; }

        int op = req[0];
        if (op != 0  && op != 2  &&
            op != 10 && op != 11 &&
            op != 12 && op != 13 &&
            op != 14 && op != 15) {
            close(fd); continue;
        }

        int eh_admin = (op >= 10) ? 1 : 0;
        conexoes++;

        /* Loga apenas acoes reais — op=0 (leitura cliente) e op=10 (listagem admin)
           sao consultas rotineiras e nao geram entrada no log */
        if (op != 0 && op != 10) {
            if (req[3] > 0)
                log_fmt("CONN", "Conexao #%d | cliente=%s usuario=#%d | op=%d — criando thread",
                        conexoes, ip_cli, req[3], op);
            else
                log_fmt("CONN", "Conexao #%d | %s=%s | op=%d — criando thread",
                        conexoes, eh_admin ? "admin" : "cliente", ip_cli, op);
        }

        ConexaoCtx *ctx = malloc(sizeof(ConexaoCtx));
        ctx->socket   = fd;
        ctx->eh_admin = eh_admin;
        ctx->req[0]=req[0]; ctx->req[1]=req[1]; ctx->req[2]=req[2]; ctx->req[3]=req[3];
        strncpy(ctx->ip, ip_cli, INET_ADDRSTRLEN);

        pthread_t t;
        if (pthread_create(&t, NULL, tratar_conexao, ctx) == 0) {
            pthread_detach(t);
        } else {
            log_fmt("ERRO", "Falha ao criar thread | %s=%s",
                    eh_admin ? "admin" : "cliente", ip_cli);
            close(fd); free(ctx);
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN — silencioso, sem nenhuma mensagem ao subir
 * ══════════════════════════════════════════════════════════════════════ */
int main() {
    descobrir_ip(ip_servidor, sizeof(ip_servidor));

    const char *log_dir = getenv("LOG_DIR");
    char log_path[256];
    if (log_dir && strlen(log_dir) > 0)
        snprintf(log_path, sizeof(log_path), "%s/servidor.log", log_dir);
    else
        snprintf(log_path, sizeof(log_path), "servidor.log");
    arq_log = fopen(log_path, "a");

    carregar_dados();
    sem_init(&semaforo_leitura, 0, 2);

    pthread_t tw;
    if (pthread_create(&tw, NULL, worker_compras, NULL) != 0) return 1;
    pthread_detach(tw);

    int fd_srv = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_srv < 0) return 1;
    int opt = 1;
    setsockopt(fd_srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family      = AF_INET;
    adr.sin_addr.s_addr = INADDR_ANY;
    adr.sin_port        = htons(8085);

    if (bind(fd_srv, (struct sockaddr*)&adr, sizeof(adr)) < 0) return 1;
    listen(fd_srv, SOMAXCONN);

    int *p = malloc(sizeof(int)); *p = fd_srv;
    pthread_t ta;
    if (pthread_create(&ta, NULL, loop_accept, p) != 0) return 1;
    pthread_join(ta, NULL);

    sem_destroy(&semaforo_leitura);
    if (arq_log) fclose(arq_log);
    return 0;
}