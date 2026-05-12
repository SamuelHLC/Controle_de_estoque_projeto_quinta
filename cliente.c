/*
 * cliente.c — Interface de usuario para consultas e compras
 * Log: cliente_<PID>.log  (cada instancia Docker gera seu proprio log)
 *
 * MIGRADO: WinSock2 + Windows.h  →  POSIX sockets + pthreads
 *
 * No Docker voce pode subir N replicas com:
 *   docker-compose up --scale cliente-pdv=3
 * Cada container tera seu proprio PID e seu proprio arquivo de log.
 *
 * CORRECOES/ADICOES:
 *  [1] MODO SIMULACAO adicionado: ao iniciar, o cliente pergunta o modo:
 *        1 = Modo Interativo (comportamento original inalterado)
 *        2 = Modo Simulacao  (N usuarios simultaneos via pthreads)
 *      No Modo Simulacao voce informa:
 *        - Numero de usuarios simultaneos (threads)
 *        - ID do produto que todos tentarao comprar
 *        - Quantidade que CADA usuario tenta comprar
 *      Cada thread abre sua propria conexao TCP com o servidor,
 *      envia a requisicao de compra e registra o resultado no log.
 *      Ao final exibe e loga o resumo (confirmadas/recusadas/erros/tempo).
 *
 *  [2] Log agora e thread-safe via mutex_log_cli — necessario pois no
 *      modo simulacao multiplas threads escrevem no mesmo arquivo.
 *
 *  [3] Cast corrigido em inet_addr(): comparacao com (in_addr_t)INADDR_NONE
 *      evita warning/comportamento indefinido em alguns compiladores.
 *
 *  Todos os logs originais foram preservados integralmente.
 *  Novos niveis de log:
 *    [SIM] — controle da simulacao (inicio, parametros, resumo final)
 *    [THR] — cada thread de usuario simulado (tentativa + resultado)
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
/* FIX [2]: mutex para log thread-safe no modo simulacao */
pthread_mutex_t mutex_log_cli = PTHREAD_MUTEX_INITIALIZER;

/* ══════════════════════════════════════════════════════════════════════
 * LOG — mutex garante atomicidade no modo simulacao (multi-thread)
 * ══════════════════════════════════════════════════════════════════════ */
void log_evento(const char *nivel, const char *msg) {
    time_t agora = time(NULL);
    struct tm *t  = localtime(&agora);
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

/* ── Resolve host do servidor via variavel de ambiente ───────────────
 * No docker-compose SERVIDOR_HOST=servidor-estoque
 * O Docker resolve "servidor-estoque" para o IP interno do container.
 * Localmente sem Docker, cai em 127.0.0.1.
 * ─────────────────────────────────────────────────────────────────── */
static void obter_endereco(struct sockaddr_in *adr) {
    const char *host = getenv("SERVIDOR_HOST");
    const char *port = getenv("SERVIDOR_PORT");

    if (!host || strlen(host) == 0) host = "127.0.0.1";
    int porta = (port && strlen(port) > 0) ? atoi(port) : 8085;

    memset(adr, 0, sizeof(*adr));
    adr->sin_family = AF_INET;
    adr->sin_port   = htons(porta);

    /* FIX [3]: cast para (in_addr_t) evita warning na comparacao */
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
    if (s < 0) {
        log_evento("ERRO", "Falha ao criar socket");
        *t = 0;
        return;
    }

    struct sockaddr_in adr;
    obter_endereco(&adr);

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int r[3] = {0, 0, 0};
        send(s, (char*)r, sizeof(r), 0);

        int recebidos = recv(s, (char*)t, sizeof(int), 0);

        if (recebidos > 0 && *t > 0) {
            int tam        = sizeof(Produto) * (*t);
            int total_lido = 0;
            char *ptr      = (char*)l;

            while (total_lido < tam) {
                int n = recv(s, ptr + total_lido, tam - total_lido, 0);
                if (n <= 0) break;
                total_lido += n;
            }
            log_fmt("NET", "Estoque recebido: %d produto(s) (%d bytes)", *t, total_lido);
        } else {
            *t = 0;
            log_evento("WARN", "Servidor retornou estoque vazio ou erro na recepcao");
        }
    } else {
        log_evento("ERRO", "Nao foi possivel conectar ao servidor");
        *t = 0;
    }

    close(s);
}

/* ══════════════════════════════════════════════════════════════════════
 * MODO SIMULACAO — FIX [1]
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    int usuario_id;
    int produto_id;
    int quantidade;
} SimArgs;

static int sim_confirmadas = 0;
static int sim_recusadas   = 0;
static int sim_erros       = 0;
static pthread_mutex_t mutex_sim_cont = PTHREAD_MUTEX_INITIALIZER;

void *thread_usuario_simulado(void *arg) {
    SimArgs *a = (SimArgs*)arg;

    log_fmt("THR", "Usuario #%d: iniciando compra | produto_id=%d qtd=%d",
            a->usuario_id, a->produto_id, a->quantidade);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_fmt("THR", "Usuario #%d: ERRO ao criar socket", a->usuario_id);
        pthread_mutex_lock(&mutex_sim_cont);
        sim_erros++;
        pthread_mutex_unlock(&mutex_sim_cont);
        free(a);
        return NULL;
    }

    struct sockaddr_in adr;
    obter_endereco(&adr);

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        int req[3] = {2, a->produto_id, a->quantidade};
        send(s, (char*)req, sizeof(req), 0);

        char res[30];
        memset(res, 0, sizeof(res));
        int bytes = recv(s, res, 29, 0);
        if (bytes > 0) res[bytes] = '\0';

        clock_gettime(CLOCK_MONOTONIC, &t1);
        double tempo = (t1.tv_sec - t0.tv_sec)
                     + (t1.tv_nsec - t0.tv_nsec) / 1e9;

        log_fmt("THR", "Usuario #%d: resposta='%s' | produto_id=%d qtd=%d | tempo=%.4fs",
                a->usuario_id, res, a->produto_id, a->quantidade, tempo);

        pthread_mutex_lock(&mutex_sim_cont);
        if      (strstr(res, "confirmada")) sim_confirmadas++;
        else if (strstr(res, "Erro"))       sim_recusadas++;
        else                                sim_erros++;
        pthread_mutex_unlock(&mutex_sim_cont);
    } else {
        log_fmt("THR", "Usuario #%d: ERRO ao conectar ao servidor", a->usuario_id);
        pthread_mutex_lock(&mutex_sim_cont);
        sim_erros++;
        pthread_mutex_unlock(&mutex_sim_cont);
    }

    close(s);
    free(a);
    return NULL;
}

void executar_simulacao() {
    int n_usuarios, produto_id, quantidade;

    printf("\n========== MODO SIMULACAO ==========\n");
    printf(" Numero de usuarios simultaneos : "); scanf("%d", &n_usuarios);
    printf(" ID do produto a comprar        : "); scanf("%d", &produto_id);
    printf(" Quantidade por usuario         : "); scanf("%d", &quantidade);

    if (n_usuarios <= 0 || produto_id <= 0 || quantidade <= 0) {
        printf(" Valores invalidos. Simulacao cancelada.\n");
        log_evento("SIM", "Simulacao cancelada — valores invalidos informados");
        return;
    }

    sim_confirmadas = 0;
    sim_recusadas   = 0;
    sim_erros       = 0;

    log_fmt("SIM", "Simulacao iniciada | usuarios=%d produto_id=%d qtd_cada=%d",
            n_usuarios, produto_id, quantidade);

    pthread_t *threads = malloc(sizeof(pthread_t) * n_usuarios);
    if (!threads) {
        log_evento("ERRO", "Falha ao alocar array de threads da simulacao");
        return;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < n_usuarios; i++) {
        SimArgs *a    = malloc(sizeof(SimArgs));
        a->usuario_id = i + 1;
        a->produto_id = produto_id;
        a->quantidade = quantidade;

        log_fmt("SIM", "Criando thread usuario #%d", i + 1);

        if (pthread_create(&threads[i], NULL, thread_usuario_simulado, a) != 0) {
            log_fmt("ERRO", "Falha ao criar thread usuario #%d", i + 1);
            free(a);
            threads[i] = 0;
        }
    }

    for (int i = 0; i < n_usuarios; i++) {
        if (threads[i]) pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double tempo_total = (t1.tv_sec - t0.tv_sec)
                       + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    free(threads);

    printf("\n========== RESULTADO DA SIMULACAO ==========\n");
    printf(" Usuarios    : %d\n",     n_usuarios);
    printf(" Produto ID  : %d\n",     produto_id);
    printf(" Qtd/usuario : %d\n",     quantidade);
    printf(" Confirmadas : %d\n",     sim_confirmadas);
    printf(" Recusadas   : %d\n",     sim_recusadas);
    printf(" Erros       : %d\n",     sim_erros);
    printf(" Tempo total : %.4f s\n", tempo_total);
    printf("=============================================\n");

    log_fmt("SIM",
            "Simulacao concluida | usuarios=%d prod=%d qtd=%d "
            "confirmadas=%d recusadas=%d erros=%d tempo=%.4fs",
            n_usuarios, produto_id, quantidade,
            sim_confirmadas, sim_recusadas, sim_erros, tempo_total);

    printf(" Pressione ENTER para continuar...");
    int c; while ((c = getchar()) != '\n' && c != EOF);
    getchar();
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

    log_evento("INIT", "========================================");
    log_fmt("INIT",    "  CLIENTE INICIADO — PID %d            ", (int)pid);
    log_evento("INIT", "========================================");

    int modo;
    system("clear");
    printf("========== LOJA VIRTUAL — PID %-6d ==========\n", (int)pid);
    printf("\n Selecione o modo de operacao:\n");
    printf("  1. Modo Interativo  (navegar e comprar manualmente)\n");
    printf("  2. Modo Simulacao   (N usuarios simultaneos)\n");
    printf("  3. Sair\n");
    printf(" Escolha: ");

    if (scanf("%d", &modo) != 1) modo = 3;
    log_fmt("INIT", "Modo selecionado: %d", modo);

    if (modo == 2) {
        executar_simulacao();
        if (arq_log) fclose(arq_log);
        return 0;
    }

    if (modo != 1) {
        log_evento("INIT", "Cliente encerrado pelo usuario");
        if (arq_log) fclose(arq_log);
        return 0;
    }

    /* ── MODO INTERATIVO (comportamento original intacto) ─────────── */
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

        if (op == 1 && total > 0) {
            int q;
            printf(" Quantidade desejada: "); scanf("%d", &q);

            log_fmt("BUY", "Tentativa de compra | id=%d nome='%s' qtd=%d",
                    lista[idx].id, lista[idx].nome, q);

            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);

            int s = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in adr2;
            obter_endereco(&adr2);

            if (connect(s, (struct sockaddr*)&adr2, sizeof(adr2)) == 0) {
                int req[3] = {2, lista[idx].id, q};
                send(s, (char*)req, sizeof(req), 0);

                char res[30];
                memset(res, 0, sizeof(res));
                int bytes = recv(s, res, 29, 0);
                if (bytes > 0) res[bytes] = '\0';

                clock_gettime(CLOCK_MONOTONIC, &t1);
                double tempo = (t1.tv_sec - t0.tv_sec)
                             + (t1.tv_nsec - t0.tv_nsec) / 1e9;

                printf("\n STATUS: %s\n TEMPO DE RESPOSTA: %.4f segundos\n", res, tempo);
                log_fmt("BUY",
                        "Resposta: '%s' | id=%d | qtd=%d | tempo=%.4fs",
                        res, lista[idx].id, q, tempo);
            } else {
                log_evento("ERRO", "Falha ao conectar para compra");
            }

            close(s);

            printf(" Pressione ENTER para continuar...");
            int c; while ((c = getchar()) != '\n' && c != EOF);
            getchar();
        }
        else if (op == 2 && idx < total - 1) {
            idx++;
            log_fmt("NAV", "Navegando para produto %d (idx=%d)", idx + 1, idx);
        }
        else if (op == 3 && idx > 0) {
            idx--;
            log_fmt("NAV", "Navegando para produto %d (idx=%d)", idx + 1, idx);
        }
        else if (op == 4) {
            log_evento("INIT", "Cliente encerrado pelo usuario");
            break;
        }
    }

    if (arq_log) fclose(arq_log);
    return 0;
}
