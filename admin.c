/*
 * admin.c — Painel administrativo de estoque
 * Log: Todas as operacoes de CRUD sao registradas em admin.log
 *
 * O admin se comunica com o servidor via socket TCP (opcodes 10-15),
 * eliminando qualquer acesso direto ao arquivo.
 *
 * CORRECOES V2 (bugs encontrados na revisao):
 *  [B1] localtime() substituido por localtime_r() — thread-safe
 *  [B2] recv() com loop em todas as chamadas (recv_completo)
 *  [B3] send() com loop em todas as chamadas (send_completo)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
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

Produto lista[100];
int     total = 0;

FILE *arq_log = NULL;

/* ══════════════════════════════════════════════════════════════════════
 * HELPERS
 * ══════════════════════════════════════════════════════════════════════ */

/* FIX [B2]: recv com loop — garante todos os bytes */
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
 * LOG
 * ══════════════════════════════════════════════════════════════════════ */
void log_evento(const char *nivel, const char *msg) {
    time_t agora = time(NULL);
    /* FIX [B1]: localtime_r e thread-safe */
    struct tm t_buf;
    struct tm *t = localtime_r(&agora, &t_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    printf("[%s][%s] %s\n", ts, nivel, msg);
    fflush(stdout);

    if (arq_log) {
        fprintf(arq_log, "[%s][%s] %s\n", ts, nivel, msg);
        fflush(arq_log);
    }
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
 * CONEXAO COM O SERVIDOR
 * ══════════════════════════════════════════════════════════════════════ */
static void obter_endereco(struct sockaddr_in *adr) {
    const char *host = getenv("SERVIDOR_HOST");
    const char *port = getenv("SERVIDOR_PORT");

    if (!host || strlen(host) == 0) host = "servidor-estoque";
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
            log_fmt("WARN", "Nao resolveu host — usando 127.0.0.1");
        }
    }
}

static int conectar() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { log_evento("ERRO", "Falha ao criar socket"); return -1; }
    struct sockaddr_in adr;
    obter_endereco(&adr);
    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) < 0) {
        log_evento("ERRO", "Falha ao conectar ao servidor");
        close(s);
        return -1;
    }
    return s;
}

/* ── Envia req[3] e recebe resposta de texto ─────────────────────── */
static int cmd_simples(int op, int id, int qtd, char *resp_out) {
    int s = conectar();
    if (s < 0) return 0;
    int req[3] = {op, id, qtd};
    send_completo(s, req, sizeof(req));
    if (resp_out) {
        memset(resp_out, 0, 30);
        /* FIX [B2]: recv_completo garante os 30 bytes da resposta */
        recv_completo(s, resp_out, 30);
    }
    close(s);
    return 1;
}

/* ── Busca lista de produtos do servidor ────────────────────────── */
static void buscar_lista() {
    log_evento("NET", "Solicitando lista de produtos ao servidor...");
    int s = conectar();
    if (s < 0) { total = 0; return; }

    int req[3] = {10, 0, 0};
    send_completo(s, req, sizeof(req));

    if (recv_completo(s, &total, sizeof(int)) <= 0) {
        total = 0; close(s); return;
    }

    if (total > 0) {
        if (recv_completo(s, lista, sizeof(Produto) * total) <= 0) {
            total = 0;
            log_evento("WARN", "Falha ao receber lista completa do servidor");
        }
    }
    close(s);
    log_fmt("NET", "Lista recebida: %d produto(s)", total);
}

/* RF_04: QuickSort por ID */
int comparar_id(const void *a, const void *b) {
    return ((Produto*)a)->id - ((Produto*)b)->id;
}

static void pausar() {
    printf(" Pressione ENTER para continuar...");
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════ */
int main() {
    const char *log_dir = getenv("LOG_DIR");
    char log_path[256];
    if (log_dir && strlen(log_dir) > 0)
        snprintf(log_path, sizeof(log_path), "%s/admin.log", log_dir);
    else
        snprintf(log_path, sizeof(log_path), "admin.log");

    arq_log = fopen(log_path, "a");

    log_evento("INIT", "========================================");
    log_evento("INIT", "  PAINEL ADMINISTRADOR — INICIADO       ");
    log_evento("INIT", "========================================");

    int op, idx = 0;

    do {
        buscar_lista();
        if (total > 0) qsort(lista, total, sizeof(Produto), comparar_id);

        system("clear");
        printf("==================================================\n");
        printf("   PAINEL DO ADMINISTRADOR - CONTROLE DE ESTOQUE  \n");
        printf("==================================================\n");

        int fim = (idx + 5 > total) ? total : idx + 5;
        if (total > 0) {
            for (int i = idx; i < fim; i++) {
                printf(" [%d] ID: %03d | %-20s | %-15s | R$ %7.2f | Est: %d\n",
                       i + 1, lista[i].id, lista[i].nome,
                       lista[i].category, lista[i].preco, lista[i].qtd);
            }
            printf("\n EXIBINDO: %d-%d de %d\n", idx + 1, fim, total);
        } else {
            printf(" >>> ESTOQUE VAZIO - CADASTRE UM PRODUTO <<<\n");
        }

        printf("--------------------------------------------------\n");
        printf(" 1. Cadastrar Novo Produto\n");
        printf(" 2. Alterar Nome\n");
        printf(" 3. Alterar Preco\n");
        printf(" 4. Adicionar Unidades\n");
        printf(" 5. Remover Produto\n");
        printf(" 6. Proxima Pagina\n");
        printf(" 7. Pagina Anterior\n");
        printf(" 10. Sair\n");
        printf(" Escolha: ");

        if (scanf("%d", &op) != 1) {
            int c; while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }
        /* Consome \n do scanf */
        { int c; while ((c = getchar()) != '\n' && c != EOF); }

        int sel = -1;
        if (op >= 2 && op <= 5 && total > 0) {
            printf(" Selecione o numero do item ([%d-%d]): ", idx + 1, fim);
            scanf("%d", &sel);
            { int c; while ((c = getchar()) != '\n' && c != EOF); }
            sel--;
        }

        char resp[30];

        switch (op) {

            /* ── RF_02: Cadastrar Produto ────────────────────────── */
            case 1: {
                Produto np;
                memset(&np, 0, sizeof(np));
                printf("ID (numerico): ");   scanf("%d",      &np.id);
                { int c; while ((c = getchar()) != '\n' && c != EOF); }
                printf("Nome: ");            scanf(" %49[^\n]",  np.nome);
                printf("Categoria: ");       scanf(" %29[^\n]",  np.category);
                printf("Preco: ");           scanf("%f",       &np.preco);
                printf("Qtd Inicial: ");     scanf("%d",       &np.qtd);
                { int c; while ((c = getchar()) != '\n' && c != EOF); }

                int s = conectar();
                if (s >= 0) {
                    int req[3] = {11, 0, 0};
                    send_completo(s, req, sizeof(req));
                    send_completo(s, &np, sizeof(Produto));
                    memset(resp, 0, sizeof(resp));
                    recv_completo(s, resp, 30);
                    close(s);
                    printf(" %s\n", resp);
                    log_fmt("CRUD", "Cadastro enviado ao servidor | id=%d nome='%s' resp='%s'",
                            np.id, np.nome, resp);
                }
                pausar();
                break;
            }

            /* ── Alterar Nome ─────────────────────────────────────── */
            case 2:
                if (sel >= 0 && sel < total) {
                    char novo[50];
                    memset(novo, 0, sizeof(novo));
                    printf("Novo Nome: "); scanf(" %49[^\n]", novo);
                    { int c; while ((c = getchar()) != '\n' && c != EOF); }
                    int s2 = conectar();
                    if (s2 >= 0) {
                        int req[3] = {12, lista[sel].id, 0};
                        send_completo(s2, req, sizeof(req));
                        send_completo(s2, novo, sizeof(novo));
                        memset(resp, 0, sizeof(resp));
                        recv_completo(s2, resp, 30);
                        close(s2);
                        log_fmt("CRUD", "Nome ALTERADO | id=%d | '%s' -> '%s' | resp='%s'",
                                lista[sel].id, lista[sel].nome, novo, resp);
                    }
                } break;

            /* ── Alterar Preco ────────────────────────────────────── */
            case 3:
                if (sel >= 0 && sel < total) {
                    float novo;
                    printf("Novo Preco: "); scanf("%f", &novo);
                    { int c; while ((c = getchar()) != '\n' && c != EOF); }
                    int s2 = conectar();
                    if (s2 >= 0) {
                        int req[3] = {13, lista[sel].id, 0};
                        send_completo(s2, req, sizeof(req));
                        send_completo(s2, &novo, sizeof(float));
                        memset(resp, 0, sizeof(resp));
                        recv_completo(s2, resp, 30);
                        close(s2);
                        log_fmt("CRUD", "Preco ALTERADO | id=%d | %.2f -> %.2f | resp='%s'",
                                lista[sel].id, lista[sel].preco, novo, resp);
                    }
                } break;

            /* ── Adicionar Unidades ───────────────────────────────── */
            case 4:
                if (sel >= 0 && sel < total) {
                    int n;
                    printf("Quantidade a somar: "); scanf("%d", &n);
                    { int c; while ((c = getchar()) != '\n' && c != EOF); }
                    cmd_simples(14, lista[sel].id, n, resp);
                    log_fmt("CRUD", "Estoque SOMADO | id=%d nome='%s' | +%d | resp='%s'",
                            lista[sel].id, lista[sel].nome, n, resp);
                } break;

            /* ── Remover Produto ──────────────────────────────────── */
            case 5:
                if (sel >= 0 && sel < total) {
                    cmd_simples(15, lista[sel].id, 0, resp);
                    log_fmt("CRUD", "Produto REMOVIDO | id=%d nome='%s' | resp='%s'",
                            lista[sel].id, lista[sel].nome, resp);
                    printf(" %s\n", resp);
                    pausar();
                } break;

            case 6: if (idx + 5 < total) idx += 5; break;
            case 7: if (idx - 5 >= 0)    idx -= 5; break;
        }

    } while (op != 10);

    log_evento("INIT", "Admin encerrado pelo operador");
    if (arq_log) fclose(arq_log);
    return 0;
}
