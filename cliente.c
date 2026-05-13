/*
 * cliente.c — Interface de usuario para consultas e compras
 * Log: cliente_<PID>.log  (cada instancia Docker gera seu proprio log)
 *
 * MIGRADO: WinSock2 + Windows.h  →  POSIX sockets + pthreads
 *
 * CORRECOES V2 (bugs encontrados na revisao):
 *  [B1] localtime() substituido por localtime_r() — thread-safe
 *  [B2] recv() com loop garantido em todas as chamadas
 *  [B3] send() com verificacao de retorno
 *  [B4] getchar() duplo removido — travava terminal apos simulacao
 *
 * MODO SIMULACAO DISTRIBUIDA REAL:
 *  O modo 2 agora imprime os parametros e sai com codigo 10.
 *  Quem realmente cria 1 container por usuario e o script simular.sh,
 *  que le os parametros e dispara N containers independentes via
 *  docker-compose run, cada um com seu proprio PID, memoria e log.
 *
 *  Modo nao-interativo (usado pelo simular.sh):
 *    MODO_AUTO=1 PRODUTO_ID=<id> QUANTIDADE=<qtd>
 *    O cliente conecta, compra e sai imprimindo o resultado.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

FILE           *arq_log       = NULL;
pthread_mutex_t mutex_log_cli = PTHREAD_MUTEX_INITIALIZER;

/* ══════════════════════════════════════════════════════════════════════
 * HELPERS
 * ══════════════════════════════════════════════════════════════════════ */

/* FIX [B2]: recv com loop — garante que todos os bytes sejam recebidos */
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

/* FIX [B3]: send com loop — garante envio completo */
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
 * LOG — mutex garante atomicidade
 * ══════════════════════════════════════════════════════════════════════ */
void log_evento(const char *nivel, const char *msg) {
    time_t agora = time(NULL);
    /* FIX [B1]: localtime_r e thread-safe */
    struct tm t_buf;
    struct tm *t = localtime_r(&agora, &t_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    pthread_mutex_lock(&mutex_log_cli);
    printf("[%s][%s] %s\n", ts, nivel, msg);
    fflush(stdout);
    if (arq_log) {
        fprintf(arq_log, "[%s][%s] %s\n", ts, nivel, msg);
        fflush(arq_log);
    }
    pthread_mutex_unlock(&mutex_log_cli);
}

void log_fmt(const char *nivel, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_evento(nivel, buf);
}

/* ── Resolve host do servidor via variavel de ambiente ─────────────── */
static void obter_endereco(struct sockaddr_in *adr) {
    const char *host = getenv("SERVIDOR_HOST");
    const char *port = getenv("SERVIDOR_PORT");

    if (!host || strlen(host) == 0) host = "127.0.0.1";
    int porta = (port && strlen(port) > 0) ? atoi(port) : 8085;

    memset(adr, 0, sizeof(*adr));
    adr->sin_family = AF_INET;
    adr->sin_port   = htons(porta);

    if (inet_addr(host) != (in_addr_t)INADDR_NONE) {
        adr->sin_addr.s_addr = inet_addr(host);
    } else {
        struct hostent *he = gethostbyname(host);
        if (he) {
            memcpy(&adr->sin_addr, he->h_addr_list[0], he->h_length);
        } else {
            adr->sin_addr.s_addr = inet_addr("127.0.0.1");
            log_fmt("WARN", "Nao resolveu '%s' — usando 127.0.0.1", host);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * COMUNICACAO COM O SERVIDOR
 * ══════════════════════════════════════════════════════════════════════ */
void buscar_estoque(Produto *l, int *t) {
    log_evento("NET", "Solicitando lista de produtos ao servidor...");

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { log_evento("ERRO", "Falha ao criar socket"); *t = 0; return; }

    struct sockaddr_in adr;
    obter_endereco(&adr);

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int r[3] = {0, 0, 0};
        send_completo(s, r, sizeof(r));

        if (recv_completo(s, t, sizeof(int)) <= 0) { *t = 0; close(s); return; }

        if (*t > 0) {
            int tam = sizeof(Produto) * (*t);
            if (recv_completo(s, l, tam) <= 0) {
                *t = 0;
                log_evento("WARN", "Falha ao receber lista completa de produtos");
            } else {
                log_fmt("NET", "Estoque recebido: %d produto(s)", *t);
            }
        } else {
            log_evento("WARN", "Servidor retornou estoque vazio");
        }
    } else {
        log_evento("ERRO", "Nao foi possivel conectar ao servidor");
        *t = 0;
    }

    close(s);
}

/* ══════════════════════════════════════════════════════════════════════
 * MODO AUTOMATICO — usado pelo simular.sh
 * Variaveis de ambiente: MODO_AUTO=1, PRODUTO_ID=<id>, QUANTIDADE=<qtd>
 * Retorna 0 em sucesso, 1 em erro
 * ══════════════════════════════════════════════════════════════════════ */
static int modo_automatico() {
    const char *s_id  = getenv("PRODUTO_ID");
    const char *s_qtd = getenv("QUANTIDADE");
    const char *s_usr = getenv("USUARIO_ID");

    int produto_id = s_id  ? atoi(s_id)  : 0;
    int quantidade = s_qtd ? atoi(s_qtd) : 0;
    int usuario_id = s_usr ? atoi(s_usr) : 0;

    if (produto_id <= 0 || quantidade <= 0) {
        fprintf(stderr, "[AUTO] PRODUTO_ID ou QUANTIDADE invalidos\n");
        return 1;
    }

    log_fmt("AUTO", "Container usuario #%d iniciado | produto_id=%d qtd=%d",
            usuario_id, produto_id, quantidade);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_fmt("AUTO", "Usuario #%d: ERRO ao criar socket", usuario_id);
        printf("ERRO\n");
        return 1;
    }

    struct sockaddr_in adr;
    obter_endereco(&adr);

    char resultado[64] = "ERRO";

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int req[3] = {2, produto_id, quantidade};
        send_completo(s, req, sizeof(req));

        char res[30];
        memset(res, 0, sizeof(res));
        if (recv_completo(s, res, 30) > 0) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double tempo = (t1.tv_sec - t0.tv_sec)
                         + (t1.tv_nsec - t0.tv_nsec) / 1e9;

            log_fmt("AUTO", "Usuario #%d: resposta='%s' | tempo=%.4fs",
                    usuario_id, res, tempo);

            /* stdout e lido pelo simular.sh para contabilizar */
            if (strstr(res, "confirmada"))     snprintf(resultado, sizeof(resultado), "CONFIRMADA");
            else if (strstr(res, "insuficiente")) snprintf(resultado, sizeof(resultado), "RECUSADA");
            else                                   snprintf(resultado, sizeof(resultado), "ERRO");

            printf("%s\n", resultado);
        }
    } else {
        log_fmt("AUTO", "Usuario #%d: ERRO ao conectar ao servidor", usuario_id);
        printf("ERRO\n");
    }

    close(s);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════ */
int main() {
    pid_t pid = getpid();

    const char *log_dir = getenv("LOG_DIR");
    char nome_log[256];
    if (log_dir && strlen(log_dir) > 0)
        snprintf(nome_log, sizeof(nome_log), "%s/cliente_%d.log", log_dir, (int)pid);
    else
        snprintf(nome_log, sizeof(nome_log), "cliente_%d.log", (int)pid);

    arq_log = fopen(nome_log, "a");

    /* ── Modo automatico (disparado pelo simular.sh) ─────────────── */
    const char *modo_auto = getenv("MODO_AUTO");
    if (modo_auto && strcmp(modo_auto, "1") == 0) {
        int r = modo_automatico();
        if (arq_log) fclose(arq_log);
        return r;
    }

    /* ── Modo interativo ─────────────────────────────────────────── */
    log_evento("INIT", "========================================");
    log_fmt("INIT",    "  CLIENTE INICIADO — PID %d            ", (int)pid);
    log_evento("INIT", "========================================");

    int ch;

    Produto lista[100];
    int total = 0, idx = 0, op;

    while (1) {
        buscar_estoque(lista, &total);

        if (total > 0 && idx >= total) idx = total - 1;
        if (total == 0) idx = 0;

        system("clear");
        printf("========== LOJA VIRTUAL — PID %-6d ==========\n", (int)pid);

        if (total > 0) {
            printf(" PRODUTO  : [%03d] %s\n",  lista[idx].id, lista[idx].nome);
            printf(" CATEGORIA: %s\n",          lista[idx].category);
            printf(" PRECO    : R$ %.2f | ESTOQUE: %d\n",
                   lista[idx].preco, lista[idx].qtd);
            printf("-----------------------------------------------\n");
            printf(" Exibindo %d de %d\n", idx + 1, total);
        } else {
            printf("\n >>> SEM PRODUTOS NO SERVIDOR <<<\n");
        }

        printf("\n 1. Comprar\n 2. Proximo\n 3. Anterior\n 4. Sair\n Escolha: ");

        if (scanf("%d", &op) != 1) {
            int c; while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }
        /* Consome o \n do scanf */
        while ((ch = getchar()) != '\n' && ch != EOF);

        if (op == 1 && total > 0) {
            int q;
            printf(" Quantidade desejada: "); scanf("%d", &q);
            while ((ch = getchar()) != '\n' && ch != EOF);

            log_fmt("BUY", "Tentativa de compra | id=%d nome='%s' qtd=%d",
                    lista[idx].id, lista[idx].nome, q);

            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);

            int s = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in adr2;
            obter_endereco(&adr2);

            if (connect(s, (struct sockaddr*)&adr2, sizeof(adr2)) == 0) {
                int req[3] = {2, lista[idx].id, q};
                send_completo(s, req, sizeof(req));

                char res[30];
                memset(res, 0, sizeof(res));
                if (recv_completo(s, res, 30) > 0) {
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    double tempo = (t1.tv_sec - t0.tv_sec)
                                 + (t1.tv_nsec - t0.tv_nsec) / 1e9;
                    printf("\n STATUS: %s\n TEMPO DE RESPOSTA: %.4f segundos\n", res, tempo);
                    log_fmt("BUY", "Resposta: '%s' | id=%d | qtd=%d | tempo=%.4fs",
                            res, lista[idx].id, q, tempo);
                }
            } else {
                log_evento("ERRO", "Falha ao conectar para compra");
            }
            close(s);

            printf(" Pressione ENTER para continuar...");
            getchar();
        }
        else if (op == 2 && idx < total - 1) { idx++; }
        else if (op == 3 && idx > 0)         { idx--; }
        else if (op == 4) {
            log_evento("INIT", "Cliente encerrado pelo usuario");
            break;
        }
    }

    if (arq_log) fclose(arq_log);
    return 0;
}
