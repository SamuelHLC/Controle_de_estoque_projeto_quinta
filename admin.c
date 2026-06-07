/*
 * admin.c — Painel administrativo de estoque
 * Log: Todas as operacoes de CRUD sao registradas em admin.log
 *
 * O admin se comunica com o servidor via socket TCP (opcodes 10-15),
 * eliminando qualquer acesso direto ao arquivo.
 *
 * CORRECOES V3:
 *  [V3-1] IP proprio do container exibido ao iniciar e em cada log
 *  [V3-2] IP do servidor exibido ao conectar
 *  [V3-3] ID automatico — campo removido do cadastro (servidor gera)
 *  [V3-4] Categorias fixas com menu numerado — sem digitacao livre
 *  [V3-5] Menu corrigido: opcoes 1-8 e 0 (sair), sem buracos
 *  [V3-6] Opcao invalida nao gera conexao ao servidor
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
#include <ifaddrs.h>

typedef struct {
    int   id;
    char  nome[50];
    char  category[30];
    float preco;
    int   qtd;
} Produto;

/* [V3-4] Categorias fixas — evita inconsistencias de escrita */
static const char *CATEGORIAS[] = {
    "Eletronicos",
    "Informatica",
    "Eletrodomesticos",
    "Moveis",
    "Vestuario",
    "Alimentos",
    "Brinquedos",
    "Ferramentas",
    "Livros",
    "Outros"
};
#define NUM_CATEGORIAS 10

Produto lista[100];
int     total = 0;

FILE *arq_log = NULL;

/* IP proprio deste container */
char ip_proprio[INET_ADDRSTRLEN] = "desconhecido";
/* IP do servidor ao qual conectamos */
char ip_servidor_remoto[INET_ADDRSTRLEN] = "desconhecido";

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

/* ── Descobre IP da interface de rede do container ───────────────── */
static void descobrir_ip_proprio(char *out, int out_len) {
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) == -1) {
        strncpy(out, "127.0.0.1", out_len);
        return;
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
        char tmp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa->sin_addr, tmp, sizeof(tmp));
        if (strcmp(tmp, "127.0.0.1") != 0) {
            strncpy(out, tmp, out_len);
            freeifaddrs(ifap);
            return;
        }
    }
    strncpy(out, "127.0.0.1", out_len);
    freeifaddrs(ifap);
}

/* ══════════════════════════════════════════════════════════════════════
 * LOG
 * ══════════════════════════════════════════════════════════════════════ */
void log_evento(const char *nivel, const char *msg) {
    time_t agora = time(NULL);
    struct tm t_buf;
    struct tm *t = localtime_r(&agora, &t_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    printf("[%s][%s][IP:%s] %s\n", ts, nivel, ip_proprio, msg);
    fflush(stdout);

    if (arq_log) {
        fprintf(arq_log, "[%s][%s][IP:%s] %s\n", ts, nivel, ip_proprio, msg);
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
        strncpy(ip_servidor_remoto, host, INET_ADDRSTRLEN);
    } else {
        struct hostent *he = gethostbyname(host);
        if (he) {
            memcpy(&adr->sin_addr, he->h_addr_list[0], he->h_length);
            inet_ntop(AF_INET, he->h_addr_list[0],
                      ip_servidor_remoto, INET_ADDRSTRLEN);
        } else {
            adr->sin_addr.s_addr = inet_addr("127.0.0.1");
            strncpy(ip_servidor_remoto, "127.0.0.1", INET_ADDRSTRLEN);
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
        log_fmt("ERRO", "Falha ao conectar ao servidor (%s:8085)", ip_servidor_remoto);
        close(s);
        return -1;
    }
    log_fmt("NET", "Conectado | admin=%s -> servidor=%s:8085",
            ip_proprio, ip_servidor_remoto);
    return s;
}

static int cmd_simples(int op, int id, int qtd, char *resp_out) {
    int s = conectar();
    if (s < 0) return 0;
    int req[4] = {op, id, qtd, 0};
    send_completo(s, req, sizeof(req));
    if (resp_out) {
        memset(resp_out, 0, 30);
        recv_completo(s, resp_out, 30);
    }
    close(s);
    return 1;
}

static void buscar_lista() {
    log_fmt("NET", "Solicitando lista | admin=%s -> servidor=%s",
            ip_proprio, ip_servidor_remoto);
    int s = conectar();
    if (s < 0) { total = 0; return; }

    int req[4] = {10, 0, 0, 0};
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

int comparar_id(const void *a, const void *b) {
    return ((Produto*)a)->id - ((Produto*)b)->id;
}

static void pausar() {
    printf(" Pressione ENTER para continuar...");
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* [V3-4] Exibe menu de categorias e retorna a string escolhida */
static void escolher_categoria(char *out, int out_len) {
    printf("\n CATEGORIAS DISPONIVEIS:\n");
    for (int i = 0; i < NUM_CATEGORIAS; i++)
        printf("  %2d. %s\n", i + 1, CATEGORIAS[i]);
    printf(" Escolha (1-%d): ", NUM_CATEGORIAS);

    int sel = 0;
    if (scanf("%d", &sel) != 1 || sel < 1 || sel > NUM_CATEGORIAS) {
        printf(" Categoria invalida — usando 'Outros'\n");
        sel = NUM_CATEGORIAS;
    }
    { int c; while ((c = getchar()) != '\n' && c != EOF); }
    strncpy(out, CATEGORIAS[sel - 1], out_len - 1);
    out[out_len - 1] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════ */
int main() {
    /* Descobre IP proprio antes de qualquer log */
    descobrir_ip_proprio(ip_proprio, sizeof(ip_proprio));

    /* Resolve endereco do servidor para popular ip_servidor_remoto */
    struct sockaddr_in adr_tmp;
    obter_endereco(&adr_tmp);

    const char *log_dir = getenv("LOG_DIR");
    char log_path[256];
    if (log_dir && strlen(log_dir) > 0)
        snprintf(log_path, sizeof(log_path), "%s/admin.log", log_dir);
    else
        snprintf(log_path, sizeof(log_path), "admin.log");

    arq_log = fopen(log_path, "a");

    log_evento("INIT", "========================================");
    log_fmt   ("INIT", "  PAINEL ADMINISTRADOR — IP: %s", ip_proprio);
    log_fmt   ("INIT", "  SERVIDOR ALVO: %s:8085", ip_servidor_remoto);
    log_evento("INIT", "========================================");

    int op;

    do {
        buscar_lista();
        if (total > 0) qsort(lista, total, sizeof(Produto), comparar_id);

        printf("\033[2J\033[H"); fflush(stdout);
        printf("==================================================\n");
        printf("  PAINEL DO ADMINISTRADOR — IP: %-15s  \n", ip_proprio);
        printf("  SERVIDOR: %-15s:8085             \n", ip_servidor_remoto);
        printf("==================================================\n");

        if (total > 0) {
            for (int i = 0; i < total; i++) {
                printf(" [%d] ID: %03d | %-20s | %-18s | R$ %7.2f | Est: %d\n",
                       i + 1, lista[i].id, lista[i].nome,
                       lista[i].category, lista[i].preco, lista[i].qtd);
            }
            printf("\n TOTAL: %d produto(s)\n", total);
        } else {
            printf(" >>> ESTOQUE VAZIO - CADASTRE UM PRODUTO <<<\n");
        }

        printf("--------------------------------------------------\n");
        printf(" 1. Cadastrar Novo Produto\n");
        printf(" 2. Alterar Nome\n");
        printf(" 3. Alterar Preco\n");
        printf(" 4. Adicionar Unidades\n");
        printf(" 5. Remover Produto\n");
        printf(" 6. Atualizar\n");
        printf(" 0. Sair\n");
        printf(" Escolha: ");

        if (scanf("%d", &op) != 1) {
            int c; while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }
        { int c; while ((c = getchar()) != '\n' && c != EOF); }

        /* Opcao invalida */
        if (op != 0 && op != 1 && op != 2 && op != 3 &&
            op != 4 && op != 5 && op != 6) {
            printf(" Opcao invalida! Use as opcoes do menu.\n");
            log_fmt("WARN", "Opcao invalida digitada: %d — sem conexao ao servidor", op);
            pausar();
            continue;
        }

        int sel = -1;
        if (op >= 2 && op <= 5 && total > 0) {
            printf(" Selecione o numero do item ([1-%d]): ", total);
            if (scanf("%d", &sel) != 1) {
                int c; while ((c = getchar()) != '\n' && c != EOF);
                sel = -1;
            } else {
                int c; while ((c = getchar()) != '\n' && c != EOF);
                sel--;
            }
            if (sel < 0 || sel >= total) {
                printf(" Selecao invalida.\n");
                pausar();
                continue;
            }
        }

        char resp[30];

        switch (op) {

            /* ── RF_02: Cadastrar Produto ─────────────────────────── */
            /* [V3-3] ID removido — gerado automaticamente pelo servidor */
            case 1: {
                Produto np;
                memset(&np, 0, sizeof(np));
                np.id = 0;  /* servidor ignora e gera o proprio */

                /* Nome — nao aceita vazio */
                do {
                    printf("Nome: ");
                    memset(np.nome, 0, sizeof(np.nome));
                    scanf(" %49[^\n]", np.nome);
                    { int c; while ((c = getchar()) != '\n' && c != EOF); }
                    if (strlen(np.nome) == 0)
                        printf(" Nome invalido. Digite ao menos um caractere.\n");
                } while (strlen(np.nome) == 0);

                escolher_categoria(np.category, sizeof(np.category));

                /* Preco — maior que zero */
                np.preco = 0;
                do {
                    printf("Preco (> 0): ");
                    if (scanf("%f", &np.preco) != 1) np.preco = 0;
                    { int c; while ((c = getchar()) != '\n' && c != EOF); }
                    if (np.preco <= 0)
                        printf(" Preco invalido. Digite um valor maior que 0.\n");
                } while (np.preco <= 0);

                /* Quantidade — maior que zero */
                np.qtd = 0;
                do {
                    printf("Qtd Inicial (> 0): ");
                    if (scanf("%d", &np.qtd) != 1) np.qtd = 0;
                    { int c; while ((c = getchar()) != '\n' && c != EOF); }
                    if (np.qtd <= 0)
                        printf(" Quantidade invalida. Digite um numero maior que 0.\n");
                } while (np.qtd <= 0);

                int s = conectar();
                if (s >= 0) {
                    int req[4] = {11, 0, 0, 0};
                    send_completo(s, req, sizeof(req));
                    send_completo(s, &np, sizeof(Produto));
                    memset(resp, 0, sizeof(resp));
                    recv_completo(s, resp, 30);
                    close(s);
                    printf(" %s\n", resp);
                    log_fmt("CRUD", "Cadastro | admin=%s -> servidor=%s | nome='%s' cat='%s' resp='%s'",
                            ip_proprio, ip_servidor_remoto, np.nome, np.category, resp);
                }
                pausar();
                break;
            }

            /* ── Alterar Nome ─────────────────────────────────────── */
            case 2:
                if (sel >= 0 && sel < total) {
                    char novo[50];
                    do {
                        memset(novo, 0, sizeof(novo));
                        printf("Novo Nome: ");
                        scanf(" %49[^\n]", novo);
                        { int c; while ((c = getchar()) != '\n' && c != EOF); }
                        if (strlen(novo) == 0)
                            printf(" Nome invalido. Digite ao menos um caractere.\n");
                    } while (strlen(novo) == 0);
                    int s2 = conectar();
                    if (s2 >= 0) {
                        int req[4] = {12, lista[sel].id, 0, 0};
                        send_completo(s2, req, sizeof(req));
                        send_completo(s2, novo, sizeof(novo));
                        memset(resp, 0, sizeof(resp));
                        recv_completo(s2, resp, 30);
                        close(s2);
                        log_fmt("CRUD", "Nome | admin=%s | id=%d '%s'->'%s' resp='%s'",
                                ip_proprio, lista[sel].id, lista[sel].nome, novo, resp);
                    }
                } break;

            /* ── Alterar Preco ────────────────────────────────────── */
            case 3:
                if (sel >= 0 && sel < total) {
                    float novo = 0;
                    do {
                        printf("Novo Preco (> 0): ");
                        if (scanf("%f", &novo) != 1) novo = 0;
                        { int c; while ((c = getchar()) != '\n' && c != EOF); }
                        if (novo <= 0)
                            printf(" Preco invalido. Digite um valor maior que 0.\n");
                    } while (novo <= 0);
                    int s2 = conectar();
                    if (s2 >= 0) {
                        int req[4] = {13, lista[sel].id, 0, 0};
                        send_completo(s2, req, sizeof(req));
                        send_completo(s2, &novo, sizeof(float));
                        memset(resp, 0, sizeof(resp));
                        recv_completo(s2, resp, 30);
                        close(s2);
                        log_fmt("CRUD", "Preco | admin=%s | id=%d %.2f->%.2f resp='%s'",
                                ip_proprio, lista[sel].id, lista[sel].preco, novo, resp);
                    }
                } break;

            /* ── Adicionar Unidades ───────────────────────────────── */
            case 4:
                if (sel >= 0 && sel < total) {
                    int n = 0;
                    do {
                        printf("Quantidade a somar (> 0): ");
                        if (scanf("%d", &n) != 1) n = 0;
                        { int c; while ((c = getchar()) != '\n' && c != EOF); }
                        if (n <= 0)
                            printf(" Quantidade invalida. Digite um numero maior que 0.\n");
                    } while (n <= 0);
                    cmd_simples(14, lista[sel].id, n, resp);
                    log_fmt("CRUD", "Estoque | admin=%s | id=%d nome='%s' +%d resp='%s'",
                            ip_proprio, lista[sel].id, lista[sel].nome, n, resp);
                } break;

            /* ── Remover Produto ──────────────────────────────────── */
            case 5:
                if (sel >= 0 && sel < total) {
                    cmd_simples(15, lista[sel].id, 0, resp);
                    log_fmt("CRUD", "Remocao | admin=%s | id=%d nome='%s' resp='%s'",
                            ip_proprio, lista[sel].id, lista[sel].nome, resp);
                    printf(" %s\n", resp);
                    pausar();
                } break;

            /* ── Atualizar ────────────────────────────────────────── */
            case 6:
                log_fmt("ADMIN", "Lista atualizada manualmente | admin=%s", ip_proprio);
                break;
        }

    } while (op != 0);

    log_fmt("INIT", "Admin encerrado | IP: %s", ip_proprio);
    if (arq_log) fclose(arq_log);
    return 0;
}