#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PRODUTOS 100

typedef struct {
    int id;
    char nome[50];
    char categoria[30];
    float preco;
    int qtd;
} Produto;

Produto lista[MAX_PRODUTOS];
int total_produtos = 0;

void salvar_todos() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) {
        fwrite(&total_produtos, sizeof(int), 1, f);
        fwrite(lista, sizeof(Produto), total_produtos, f);
        fclose(f);
        printf("\n>>> Sucesso: Dados salvos com seguranca!\n");
    }
}

void carregar_todos() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) {
        if (fread(&total_produtos, sizeof(int), 1, f) != 1) total_produtos = 0;
        else fread(lista, sizeof(Produto), total_produtos, f);
        fclose(f);
    } else {
        total_produtos = 0;
    }
}

int main() {
    int opcao, indice_atual = 0;
    carregar_todos();

    do {
        system("cls");
        printf("\n==================================================\n");
        printf("   PAINEL DO ADMINISTRADOR - GESTAO DE ESTOQUE    \n");
        printf("==================================================\n");
        
        if (total_produtos > 0) {
            Produto *p = &lista[indice_atual];
            printf(" EXIBINDO: %d de %d\n", indice_atual + 1, total_produtos);
            printf(" ID: %03d | Produto: %-20s\n", p->id, p->nome);
            printf(" Categoria: %-20s | Preco: R$ %.2f\n", p->categoria, p->preco);
            printf(" Estoque Atual: %d unidades\n", p->qtd);
        } else {
            printf(" >>> NENHUM PRODUTO CADASTRADO <<<\n");
        }

        printf("--------------------------------------------------\n");
        printf(" 1. Cadastrar Novo Produto\n 2. Alterar Nome\n 3. Alterar Categoria\n 4. Remover Categoria\n 5. Alterar Preco\n 6. Adicionar Unidades\n 7. Remover Unidades\n 8. Proximo\n 9. Anterior\n 10. Sair\n Escolha: ");
        
        if (scanf("%d", &opcao) != 1) { while (getchar() != '\n'); continue; }

        switch(opcao) {
            case 1:
                if (total_produtos < MAX_PRODUTOS) {
                    printf("ID: "); scanf("%d", &lista[total_produtos].id);
                    printf("Nome: "); scanf(" %[^\n]s", lista[total_produtos].nome);
                    printf("Categoria: "); scanf(" %[^\n]s", lista[total_produtos].categoria);
                    printf("Preco: "); scanf("%f", &lista[total_produtos].preco);
                    printf("Qtd: "); scanf("%d", &lista[total_produtos].qtd);
                    total_produtos++; indice_atual = total_produtos - 1;
                    salvar_todos(); system("pause");
                } break;
            case 8: if (indice_atual < total_produtos - 1) indice_atual++; break;
            case 9: if (indice_atual > 0) indice_atual--; break;
        }
    } while(opcao != 10);
    return 0;
}