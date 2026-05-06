#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int id; char nome[50]; char categoria[30]; float preco; int qtd; } Produto;

Produto lista[100];
int total = 0;

void salvar() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) { fwrite(&total, sizeof(int), 1, f); fwrite(lista, sizeof(Produto), total, f); fclose(f); }
}

void carregar() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) { 
        if(fread(&total, sizeof(int), 1, f) != 1) total = 0;
        else fread(lista, sizeof(Produto), total, f);
        fclose(f); 
    }
}

int main() {
    int op, idx = 0;
    carregar();
    do {
        system("cls");
        printf("==================================================\n");
        printf("   PAINEL DO ADMINISTRADOR - GESTAO DE ESTOQUE    \n");
        printf("==================================================\n");
        
        int fim = (idx + 5 > total) ? total : idx + 5;
        if (total > 0) {
            for (int i = idx; i < fim; i++) {
                printf(" [%d] ID: %03d | %-15s | R$ %.2f | Est: %d\n", i + 1, lista[i].id, lista[i].nome, lista[i].preco, lista[i].qtd);
            }
            printf("\n EXIBINDO: %d-%d de %d\n", idx + 1, fim, total);
        } else printf(" >>> ESTOQUE VAZIO <<<\n");

        printf("--------------------------------------------------\n");
        printf(" 1. Cadastrar  2. Nome      3. Categoria  4. Limpar Cat\n 5. Preco      6. Add Unid  7. Rem Unid   8. Prox Pag\n 9. Ant Pag    10. Sair\n Escolha: ");
        if (scanf("%d", &op) != 1) { while (getchar() != '\n'); continue; }

        int sel = -1;
        if (op >= 2 && op <= 7 && total > 0) {
            printf(" Alterar qual item (digite o numero [%d-%d]): ", idx + 1, fim);
            scanf("%d", &sel);
            sel--; // Ajusta para o índice do array
        }

        switch(op) {
            case 1:
                printf("ID: "); scanf("%d", &lista[total].id);
                printf("Nome: "); scanf(" %[^\n]s", lista[total].nome);
                printf("Cat: "); scanf(" %[^\n]s", lista[total].categoria);
                printf("Preco: "); scanf("%f", &lista[total].preco);
                printf("Qtd: "); scanf("%d", &lista[total].qtd);
                total++; salvar(); break;
            case 2: if(sel >= 0) { printf("Novo Nome: "); scanf(" %[^\n]s", lista[sel].nome); salvar(); } break;
            case 3: if(sel >= 0) { printf("Nova Cat: "); scanf(" %[^\n]s", lista[sel].categoria); salvar(); } break;
            case 4: if(sel >= 0) { strcpy(lista[sel].categoria, "Sem Cat"); salvar(); } break;
            case 5: if(sel >= 0) { printf("Novo Preco: "); scanf("%f", &lista[sel].preco); salvar(); } break;
            case 6: if(sel >= 0) { int n; printf("Soma: "); scanf("%d", &n); lista[sel].qtd += n; salvar(); } break;
            case 7: if(sel >= 0) { int n; printf("Subtrai: "); scanf("%d", &n); if(n <= lista[sel].qtd) lista[sel].qtd -= n; salvar(); } break;
            case 8: if (idx + 5 < total) idx += 5; break;
            case 9: if (idx - 5 >= 0) idx -= 5; break;
        }
    } while(op != 10);
    return 0;
}
