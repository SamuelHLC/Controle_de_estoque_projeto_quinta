/*
 * ============================================================
 *  PAINEL DO ADMINISTRADOR - CONTROLE DE ESTOQUE v7.0
 *  Projeto Integrador de Computacao Paralela - UNIEURO
 * ============================================================
 *  - Leitura/escrita direta no arquivo estoque.dat (binario)
 *  - Thread assincrona de notificacao (simulando worker)
 *  - QuickSort para ordenacao (RF_04)
 *  - Validacao de entrada em todos os campos
 * ============================================================
 *  COMPILAR: gcc admin.c -o admin
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

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

Produto lista[100];
int total = 0;

/* ─────────────────────────────────────────────
   FILA DE NOTIFICACOES (PARALELISMO NO ADMIN)
   - Cada alteracao no estoque enfileira uma
     notificacao processada em background
   ───────────────────────────────────────────── */
#define NOTIF_MAX 64
typedef struct { char msg[128]; } Notificacao;

Notificacao fila_notif[NOTIF_MAX];
int  notif_head = 0, notif_tail = 0, notif_count = 0;
HANDLE mutex_notif;
HANDLE sem_notif;
int    admin_rodando = 1;

void enfileirar_notif(const char *msg) {
    WaitForSingleObject(mutex_notif, INFINITE);
    if (notif_count < NOTIF_MAX) {
        strncpy(fila_notif[notif_tail].msg, msg, 127);
        fila_notif[notif_tail].msg[127] = '\0';
        notif_tail = (notif_tail + 1) % NOTIF_MAX;
        notif_count++;
        ReleaseSemaphore(sem_notif, 1, NULL);
    }
    ReleaseMutex(mutex_notif);
}

/* Worker: consome notificacoes em background e grava em log */
DWORD WINAPI worker_notificacao(LPVOID lpParam) {
    (void)lpParam;
    FILE *f = fopen("log_admin.txt", "a");
    while (admin_rodando || notif_count > 0) {
        if (WaitForSingleObject(sem_notif, 400) == WAIT_TIMEOUT) continue;

        WaitForSingleObject(mutex_notif, INFINITE);
        if (notif_count == 0) { ReleaseMutex(mutex_notif); continue; }
        Notificacao n = fila_notif[notif_head];
        notif_head = (notif_head + 1) % NOTIF_MAX;
        notif_count--;
        ReleaseMutex(mutex_notif);

        /* grava no arquivo de log do admin */
        if (f) { fprintf(f, "[ADMIN-LOG] %s\n", n.msg); fflush(f); }
    }
    if (f) fclose(f);
    return 0;
}

/* ─────────────────────────────────────────────
   PERSISTENCIA
   ───────────────────────────────────────────── */
void salvar() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) {
        fwrite(&total, sizeof(int),     1,     f);
        fwrite(lista,  sizeof(Produto), total, f);
        fclose(f);
    }
}

void carregar() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) {
        if (fread(&total, sizeof(int), 1, f) != 1) total = 0;
        else fread(lista, sizeof(Produto), total, f);
        fclose(f);
    }
}

/* ─────────────────────────────────────────────
   QUICKSORT (RF_04)
   ───────────────────────────────────────────── */
int comparar_id(const void *a, const void *b) {
    return ((Produto*)a)->id - ((Produto*)b)->id;
}

/* ─────────────────────────────────────────────
   VALIDACAO: verifica ID duplicado
   ───────────────────────────────────────────── */
int id_existe(int id) {
    for (int i = 0; i < total; i++)
        if (lista[i].id == id) return 1;
    return 0;
}

/* limpa buffer de entrada */
void limpar_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* ─────────────────────────────────────────────
   MAIN
   ───────────────────────────────────────────── */
int main() {
    /* inicia primitivas e worker de notificacao */
    mutex_notif = CreateMutex(NULL, FALSE, NULL);
    sem_notif   = CreateSemaphore(NULL, 0, NOTIF_MAX, NULL);
    CreateThread(NULL, 0, worker_notificacao, NULL, 0, NULL);

    int op, idx = 0;
    carregar();

    do {
        system("cls");
        printf("==================================================\n");
        printf("  PAINEL DO ADMINISTRADOR - CONTROLE DE ESTOQUE   \n");
        printf("  [Worker de notificacao ativo em background]      \n");
        printf("==================================================\n");

        /* RF_03: exibe estoque paginado, ordenado por ID */
        if (total > 0) {
            qsort(lista, total, sizeof(Produto), comparar_id);
            int fim = (idx + 5 > total) ? total : idx + 5;
            for (int i = idx; i < fim; i++) {
                printf(" [%d] ID:%03d | %-20s | Cat:%-12s | R$%7.2f | Est:%3d\n",
                       i - idx + 1,
                       lista[i].id, lista[i].nome,
                       lista[i].category,
                       lista[i].preco, lista[i].qtd);
            }
            printf("\n Pagina: itens %d-%d de %d\n", idx + 1, fim, total);
        } else {
            printf("\n >>> ESTOQUE VAZIO - CADASTRE UM PRODUTO <<<\n");
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

        if (scanf("%d", &op) != 1) { limpar_buffer(); continue; }

        int fim_atual = (idx + 5 > total) ? total : idx + 5;
        int sel = -1;

        /* operacoes que precisam de selecao de item */
        if ((op >= 2 && op <= 5) && total > 0) {
            printf(" Numero do item na lista [1-%d]: ", fim_atual - idx);
            if (scanf("%d", &sel) != 1) { limpar_buffer(); sel = -1; }
            else sel = idx + sel - 1; /* converte para indice global */
        }

        char notif_buf[128];

        switch (op) {

            /* ── CADASTRAR (RF_02) ───────────────── */
            case 1:
                if (total >= 100) {
                    printf(" LIMITE DE 100 PRODUTOS ATINGIDO!\n");
                    system("pause"); break;
                }
                {
                    int novo_id;
                    printf(" ID (numerico, unico): "); scanf("%d", &novo_id);
                    if (id_existe(novo_id)) {
                        printf(" [ERRO] ID %d ja existe!\n", novo_id);
                        system("pause"); break;
                    }
                    lista[total].id = novo_id;
                    limpar_buffer();
                    printf(" Nome: ");     scanf(" %[^\n]", lista[total].nome);
                    printf(" Categoria: ");scanf(" %[^\n]", lista[total].category);
                    printf(" Preco (R$): ");scanf("%f", &lista[total].preco);
                    if (lista[total].preco < 0) lista[total].preco = 0;
                    printf(" Qtd Inicial: ");scanf("%d", &lista[total].qtd);
                    if (lista[total].qtd < 0) lista[total].qtd = 0;
                    total++;
                    salvar();
                    printf(" [OK] Produto cadastrado!\n");

                    /* notificacao assincrona */
                    snprintf(notif_buf, sizeof(notif_buf),
                             "CADASTRO | ID=%03d | %s | R$%.2f | Qtd=%d",
                             lista[total-1].id, lista[total-1].nome,
                             lista[total-1].preco, lista[total-1].qtd);
                    enfileirar_notif(notif_buf);
                }
                system("pause"); break;

            /* ── ALTERAR NOME ────────────────────── */
            case 2:
                if (sel >= 0 && sel < total) {
                    limpar_buffer();
                    printf(" Novo Nome: "); scanf(" %[^\n]", lista[sel].nome);
                    salvar();
                    snprintf(notif_buf, sizeof(notif_buf),
                             "ALTERACAO NOME | ID=%03d -> %s", lista[sel].id, lista[sel].nome);
                    enfileirar_notif(notif_buf);
                    printf(" [OK] Nome atualizado.\n");
                    system("pause");
                } break;

            /* ── ALTERAR PRECO ───────────────────── */
            case 3:
                if (sel >= 0 && sel < total) {
                    printf(" Novo Preco: "); scanf("%f", &lista[sel].preco);
                    if (lista[sel].preco < 0) lista[sel].preco = 0;
                    salvar();
                    snprintf(notif_buf, sizeof(notif_buf),
                             "ALTERACAO PRECO | ID=%03d -> R$%.2f", lista[sel].id, lista[sel].preco);
                    enfileirar_notif(notif_buf);
                    printf(" [OK] Preco atualizado.\n");
                    system("pause");
                } break;

            /* ── ADICIONAR UNIDADES ──────────────── */
            case 4:
                if (sel >= 0 && sel < total) {
                    int n;
                    printf(" Quantidade a somar: "); scanf("%d", &n);
                    if (n > 0) {
                        lista[sel].qtd += n;
                        salvar();
                        snprintf(notif_buf, sizeof(notif_buf),
                                 "REPOSICAO | ID=%03d | +%d unidades | Novo total=%d",
                                 lista[sel].id, n, lista[sel].qtd);
                        enfileirar_notif(notif_buf);
                        printf(" [OK] Estoque atualizado.\n");
                    } else {
                        printf(" [ERRO] Quantidade invalida.\n");
                    }
                    system("pause");
                } break;

            /* ── REMOVER PRODUTO ─────────────────── */
            case 5:
                if (sel >= 0 && sel < total) {
                    char confirm;
                    printf(" Remover '%s'? (s/n): ", lista[sel].nome);
                    limpar_buffer();
                    scanf("%c", &confirm);
                    if (confirm == 's' || confirm == 'S') {
                        snprintf(notif_buf, sizeof(notif_buf),
                                 "REMOCAO | ID=%03d | %s", lista[sel].id, lista[sel].nome);
                        enfileirar_notif(notif_buf);
                        /* remove deslocando array */
                        for (int i = sel; i < total - 1; i++)
                            lista[i] = lista[i + 1];
                        total--;
                        if (idx >= total && idx > 0) idx -= 5;
                        salvar();
                        printf(" [OK] Produto removido.\n");
                    }
                    system("pause");
                } break;

            case 6: if (idx + 5 < total) idx += 5; break;
            case 7: if (idx - 5 >= 0)    idx -= 5; break;
        }
    } while (op != 10);

    admin_rodando = 0;
    Sleep(600); /* aguarda worker gravar ultimas notificacoes */
    return 0;
}