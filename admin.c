#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CORRIGIDO: campo renomeado de "categoria" para "category" para ser identico ao servidor.c
typedef struct { 
    int id; 
    char nome[50]; 
    char category[30];   // era: char categoria[30]
    float preco; 
    int qtd; 
} Produto;

Produto lista[100];
int total = 0;

// RNF_02: Persistência em disco (binário)
void salvar() {
    FILE *f = fopen("estoque.dat", "wb");
    if (f) { 
        fwrite(&total, sizeof(int), 1, f); 
        fwrite(lista, sizeof(Produto), total, f); 
        fclose(f); 
    }
}

void carregar() {
    FILE *f = fopen("estoque.dat", "rb");
    if (f) { 
        if(fread(&total, sizeof(int), 1, f) != 1) total = 0;
        else fread(lista, sizeof(Produto), total, f);
        fclose(f); 
    }
}

// RF_04: Implementação do QuickSort para ordenação
int comparar_id(const void *a, const void *b) {
    return ((Produto*)a)->id - ((Produto*)b)->id;
}

int main() {
    int op, idx = 0;
    carregar();
    
    do {
        system("cls");
        printf("==================================================\n");
        printf("   PAINEL DO ADMINISTRADOR - CONTROLE DE ESTOQUE  \n");
        printf("==================================================\n");
        
        // RF_03: Visualizar Estoque
        int fim = (idx + 5 > total) ? total : idx + 5;
        if (total > 0) {
            qsort(lista, total, sizeof(Produto), comparar_id);
            for (int i = idx; i < fim; i++) {
                printf(" [%d] ID: %03d | %-15s | R$ %.2f | Est: %d\n", 
                        i + 1, lista[i].id, lista[i].nome, lista[i].preco, lista[i].qtd);
            }
            printf("\n EXIBINDO: %d-%d de %d\n", idx + 1, fim, total);
        } else printf(" >>> ESTOQUE VAZIO - CADASTRE UM PRODUTO <<<\n");

        printf("--------------------------------------------------\n");
        printf(" 1. Cadastrar Novo Produto\n");
        printf(" 2. Alterar Nome\n");
        printf(" 3. Alterar Preco\n");
        printf(" 4. Adicionar Unidades\n");
        printf(" 5. Proxima Pagina\n");
        printf(" 6. Pagina Anterior\n");
        printf(" 10. Sair\n");
        printf(" Escolha: ");
        
        if (scanf("%d", &op) != 1) { 
            while (getchar() != '\n'); 
            continue; 
        }

        int sel = -1;
        if (op >= 2 && op <= 4 && total > 0) {
            printf(" Selecione o numero do item ([%d-%d]): ", idx + 1, fim);
            scanf("%d", &sel);
            sel--; // Ajuste para índice zero
        }

        switch(op) {
            case 1: // RF_02: Cadastrar Produto
                if (total < 100) {
                    printf("ID (numerico): "); scanf("%d", &lista[total].id);
                    printf("Nome: "); scanf(" %[^\n]", lista[total].nome);
                    printf("Categoria: "); scanf(" %[^\n]", lista[total].category); // CORRIGIDO: era lista[total].categoria
                    printf("Preco: "); scanf("%f", &lista[total].preco);            // CORRIGIDO: era &total[lista].preco (bug de sintaxe)
                    printf("Qtd Inicial: "); scanf("%d", &lista[total].qtd);
                    total++;
                    salvar();
                    printf("Produto cadastrado com sucesso!\n");
                } else printf("Limite do sistema atingido!\n");
                system("pause");
                break;

            case 2:
                if(sel >= 0 && sel < total) { 
                    printf("Novo Nome: "); scanf(" %[^\n]", lista[sel].nome); 
                    salvar(); 
                } break;

            case 3:
                if(sel >= 0 && sel < total) { 
                    printf("Novo Preco: "); scanf("%f", &lista[sel].preco); 
                    salvar(); 
                } break;

            case 4:
                if(sel >= 0 && sel < total) { 
                    int n; printf("Quantidade a somar: "); scanf("%d", &n); 
                    lista[sel].qtd += n; 
                    salvar(); 
                } break;

            case 5: if (idx + 5 < total) idx += 5; break;
            case 6: if (idx - 5 >= 0) idx -= 5; break;
        }
    } while(op != 10);

    return 0;
}
