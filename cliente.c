/*
 * cliente.c — Interface de usuario para consultas e compras
 * Log: cliente_<PID>.log
 *
 * CORRECOES V3:
 *  [V3-1] IP proprio do container exibido ao iniciar e em cada log
 *  [V3-2] IP do servidor exibido ao conectar
 *  [V3-3] Containers de simulacao ficam rodando apos a compra
 *  [V3-4] USUARIO_ID enviado junto na requisicao de compra (op=2)
 *         Servidor loga: cliente=IP usuario=#N
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
#include <ifaddrs.h>

typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

FILE           *arq_log       = NULL;
pthread_mutex_t mutex_log_cli = PTHREAD_MUTEX_INITIALIZER;

char ip_proprio[INET_ADDRSTRLEN]         = "desconhecido";
char ip_servidor_remoto[INET_ADDRSTRLEN] = "desconhecido";

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

static void descobrir_ip_proprio(char *out, int len) {
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
    pthread_mutex_lock(&mutex_log_cli);
    printf("[%s][%s][IP:%s] %s\n", ts, nivel, ip_proprio, msg);
    fflush(stdout);
    if (arq_log) { fprintf(arq_log, "[%s][%s][IP:%s] %s\n", ts, nivel, ip_proprio, msg); fflush(arq_log); }
    pthread_mutex_unlock(&mutex_log_cli);
}

void log_fmt(const char *nivel, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    log_evento(nivel, buf);
}

/* ══════════════════════════════════════════════════════════════════════
 * ENDERECO DO SERVIDOR
 * ══════════════════════════════════════════════════════════════════════ */
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
        strncpy(ip_servidor_remoto, host, INET_ADDRSTRLEN);
    } else {
        struct hostent *he = gethostbyname(host);
        if (he) {
            memcpy(&adr->sin_addr, he->h_addr_list[0], he->h_length);
            inet_ntop(AF_INET, he->h_addr_list[0], ip_servidor_remoto, INET_ADDRSTRLEN);
        } else {
            adr->sin_addr.s_addr = inet_addr("127.0.0.1");
            strncpy(ip_servidor_remoto, "127.0.0.1", INET_ADDRSTRLEN);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * BUSCAR ESTOQUE
 * ══════════════════════════════════════════════════════════════════════ */
void buscar_estoque(Produto *l, int *t) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { *t = 0; return; }
    struct sockaddr_in adr; obter_endereco(&adr);
    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int r[4] = {0, 0, 0, 0};
        send_completo(s, r, sizeof(r));
        if (recv_completo(s, t, sizeof(int)) <= 0) { *t = 0; close(s); return; }
        if (*t > 0) {
            if (recv_completo(s, l, sizeof(Produto) * (*t)) <= 0) {
                *t = 0;
                log_evento("WARN", "Falha ao receber lista completa");
            }
        }
    } else {
        log_fmt("ERRO", "Nao foi possivel conectar | cliente=%s -> servidor=%s:8085",
                ip_proprio, ip_servidor_remoto);
        *t = 0;
    }
    close(s);
}

/* ══════════════════════════════════════════════════════════════════════
 * MODO AUTOMATICO
 * Envia req[4] = {op, produto_id, quantidade, usuario_id}
 * Servidor loga: cliente=IP usuario=#N
 * ══════════════════════════════════════════════════════════════════════ */
static int modo_automatico() {
    const char *s_id  = getenv("PRODUTO_ID");
    const char *s_qtd = getenv("QUANTIDADE");
    const char *s_usr = getenv("USUARIO_ID");

    int produto_id = s_id  ? atoi(s_id)  : 0;
    int quantidade = s_qtd ? atoi(s_qtd) : 0;
    int usuario_id = s_usr ? atoi(s_usr) : 0;

    if (produto_id <= 0 || quantidade <= 0) {
        log_fmt("AUTO", "Usuario #%d | IP:%s | parametros invalidos — container em standby",
                usuario_id, ip_proprio);
        while (1) sleep(60);
    }

    log_fmt("AUTO", "Usuario #%d | IP:%s | produto_id=%d qtd=%d | conectando a %s:8085...",
            usuario_id, ip_proprio, produto_id, quantidade, ip_servidor_remoto);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_fmt("AUTO", "Usuario #%d | IP:%s | ERRO ao criar socket", usuario_id, ip_proprio);
        printf("ERRO\n"); fflush(stdout);
        while (1) sleep(60);
    }

    struct sockaddr_in adr; obter_endereco(&adr);
    char resultado[64] = "ERRO";

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        /* [V3-4] req[4]: op=2, produto_id, quantidade, usuario_id */
        int req[4] = {2, produto_id, quantidade, usuario_id};
        send_completo(s, req, sizeof(req));

        char res[30]; memset(res, 0, sizeof(res));
        if (recv_completo(s, res, 30) > 0) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double tempo = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

            log_fmt("AUTO", "Usuario #%d | IP:%s | resposta='%s' | tempo=%.4fs",
                    usuario_id, ip_proprio, res, tempo);

            if (strstr(res, "confirmada"))     snprintf(resultado, sizeof(resultado), "CONFIRMADA");
            else if (strstr(res, "insuficiente")) snprintf(resultado, sizeof(resultado), "RECUSADA");
            else                                   snprintf(resultado, sizeof(resultado), "ERRO");

            printf("%s\n", resultado);
            fflush(stdout);
        }
    } else {
        log_fmt("AUTO", "Usuario #%d | IP:%s | ERRO ao conectar a %s:8085",
                usuario_id, ip_proprio, ip_servidor_remoto);
        printf("ERRO\n"); fflush(stdout);
    }

    close(s);

    log_fmt("AUTO", "Usuario #%d | IP:%s | compra concluida — container em standby",
            usuario_id, ip_proprio);
    while (1) sleep(60);

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════ */
int main() {
    pid_t pid = getpid();
    descobrir_ip_proprio(ip_proprio, sizeof(ip_proprio));
    struct sockaddr_in adr_tmp; obter_endereco(&adr_tmp);

    const char *log_dir = getenv("LOG_DIR");
    char nome_log[256];
    if (log_dir && strlen(log_dir) > 0)
        snprintf(nome_log, sizeof(nome_log), "%s/cliente_%d.log", log_dir, (int)pid);
    else
        snprintf(nome_log, sizeof(nome_log), "cliente_%d.log", (int)pid);
    arq_log = fopen(nome_log, "a");

    /* Modo automatico */
    const char *modo_auto = getenv("MODO_AUTO");
    if (modo_auto && strcmp(modo_auto, "1") == 0) {
        log_evento("INIT", "========================================");
        log_fmt   ("INIT", "  CLIENTE AUTO — IP: %s", ip_proprio);
        log_fmt   ("INIT", "  SERVIDOR: %s:8085", ip_servidor_remoto);
        log_fmt   ("INIT", "  PID: %d", (int)pid);
        log_evento("INIT", "========================================");
        int r = modo_automatico();
        if (arq_log) fclose(arq_log);
        return r;
    }

    /* Modo interativo */
    log_evento("INIT", "========================================");
    log_fmt   ("INIT", "  CLIENTE INICIADO — IP: %s", ip_proprio);
    log_fmt   ("INIT", "  SERVIDOR: %s:8085", ip_servidor_remoto);
    log_fmt   ("INIT", "  PID: %d", (int)pid);
    log_evento("INIT", "========================================");

    Produto lista[100];
    int total = 0, idx = 0, op;

    while (1) {
        buscar_estoque(lista, &total);
        if (total > 0 && idx >= total) idx = total - 1;
        if (total == 0) idx = 0;

        /* Se nao ha produtos, aguarda 2s e tenta novamente automaticamente */
        if (total == 0) {
            printf("\n===== LOJA VIRTUAL | IP: %-15s PID: %-6d =====\n", ip_proprio, (int)pid);
            printf(" SERVIDOR: %s:8085\n", ip_servidor_remoto);
            printf("------------------------------------------------------\n");
            printf("\n >>> SEM PRODUTOS NO SERVIDOR — tentando novamente em 2s... <<<\n");
            fflush(stdout);
            sleep(2);
            continue;
        }

        printf("\n===== LOJA VIRTUAL | IP: %-15s PID: %-6d =====\n", ip_proprio, (int)pid);
        printf(" SERVIDOR: %s:8085\n", ip_servidor_remoto);
        printf("------------------------------------------------------\n");
        printf(" PRODUTO  : [%03d] %s\n",  lista[idx].id, lista[idx].nome);
        printf(" CATEGORIA: %s\n",          lista[idx].category);
        printf(" PRECO    : R$ %.2f | ESTOQUE: %d\n", lista[idx].preco, lista[idx].qtd);
        printf("------------------------------------------------------\n");
        printf(" Exibindo %d de %d\n", idx + 1, total);

        printf("\n 1. Comprar\n 2. Proximo\n 3. Anterior\n 4. Sair\n Escolha: ");
        fflush(stdout);

        char linha[32]; op = 0;
        if (fgets(linha, sizeof(linha), stdin)) op = atoi(linha);

        if (op == 1 && total > 0) {
            int q = 0;
            do {
                printf(" Quantidade desejada (> 0): "); fflush(stdout);
                char qlinha[32];
                if (fgets(qlinha, sizeof(qlinha), stdin)) q = atoi(qlinha);
                if (q <= 0)
                    printf(" Quantidade invalida. Digite um numero maior que 0.\n");
            } while (q <= 0);

            log_fmt("BUY", "Tentativa | cliente=%s -> servidor=%s | id=%d nome='%s' qtd=%d",
                    ip_proprio, ip_servidor_remoto, lista[idx].id, lista[idx].nome, q);

            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);

            int s = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in adr2; obter_endereco(&adr2);

            if (connect(s, (struct sockaddr*)&adr2, sizeof(adr2)) == 0) {
                /* Modo interativo: usuario_id = 0 (nao e simulacao) */
                int req[4] = {2, lista[idx].id, q, 0};
                send_completo(s, req, sizeof(req));

                char res[30]; memset(res, 0, sizeof(res));
                if (recv_completo(s, res, 30) > 0) {
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    double tempo = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
                    printf("\n STATUS: %s\n TEMPO DE RESPOSTA: %.4f segundos\n", res, tempo);
                    log_fmt("BUY", "Resposta | cliente=%s | '%s' | id=%d qtd=%d tempo=%.4fs",
                            ip_proprio, res, lista[idx].id, q, tempo);
                }
            } else {
                log_fmt("ERRO", "Falha ao conectar | cliente=%s -> servidor=%s",
                        ip_proprio, ip_servidor_remoto);
            }
            close(s);
            printf(" Pressione ENTER para continuar..."); fflush(stdout);
            char tmp[8]; fgets(tmp, sizeof(tmp), stdin);
        }
        else if (op == 2 && idx < total - 1) { idx++; }
        else if (op == 3 && idx > 0)         { idx--; }
        else if (op == 4) {
            log_fmt("INIT", "Cliente encerrado | IP: %s", ip_proprio);
            break;
        }
    }

    if (arq_log) fclose(arq_log);
    return 0;
}
