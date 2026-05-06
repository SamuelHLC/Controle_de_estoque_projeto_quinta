#include <stdio.h>
#include <winsock2.h>
#include <process.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

// Estrutura para os dados de cada cliente simulado
typedef struct {
    int id_produto;
    int quantidade;
    int cliente_id;
} DadosTeste;

// Função executada por cada thread (Cliente Robô)
unsigned __stdcall executar_cliente_stress(void* data) {
    DadosTeste* dt = (DadosTeste*)data;
    
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    
    // Configurado para a nova porta 8085
    struct sockaddr_in adr;
    adr.sin_family = AF_INET;
    adr.sin_port = htons(8085); 
    adr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s, (struct sockaddr*)&adr, sizeof(adr)) == 0) {
        // req[0] = 2 (Código de Compra), req[1] = ID (Mesa), req[2] = Qtd
        int req[3] = {2, dt->id_produto, dt->quantidade}; 
        
        send(s, (char*)req, sizeof(req), 0);
        
        char resposta_servidor[30];
        int bytes = recv(s, resposta_servidor, 29, 0);
        
        if (bytes > 0) {
            resposta_servidor[bytes] = '\0';
            printf("[QA-TEST] Cliente %02d | Status: %s\n", dt->cliente_id, resposta_servidor);
        }
    } else {
        printf("[QA-TEST] Cliente %02d | Erro: Servidor Offline na porta 8085\n", dt->cliente_id);
    }

    closesocket(s);
    return 0;
}

int main() {
    WSADATA w; 
    WSAStartup(0x0202, &w);

    int num_clientes = 20; 
    HANDLE threads[20];
    DadosTeste dados[20];

    printf("==================================================\n");
    printf("   INICIANDO VALIDACAO CIENTIFICA DE CONCORRENCIA  \n");
    printf("   ALVO: PORTA 8085 | PRODUTO ID 002 (MESA)       \n");
    printf("==================================================\n");

    clock_t inicio = clock();

    for (int i = 0; i < num_clientes; i++) {
        dados[i].id_produto = 2; // ID da Mesa cadastrada
        dados[i].quantidade = 1;
        dados[i].cliente_id = i + 1;
        
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, executar_cliente_stress, &dados[i], 0, NULL);
        
        // Pequena pausa para estabilidade da rede no Windows
        Sleep(20); 
    }

    // Aguarda todos os clientes terminarem
    WaitForMultipleObjects(num_clientes, threads, TRUE, INFINITE);

    clock_t fim = clock();
    double tempo_total = ((double)(fim - inicio)) / CLOCKS_PER_SEC;

    printf("\n==================================================\n");
    printf("              METRICAS DE BENCHMARK               \n");
    printf("==================================================\n");
    printf("Tempo total para %d operacoes: %.4f segundos\n", num_clientes, tempo_total);
    printf("Throughput: %.2f operacoes/segundo\n", num_clientes / tempo_total);
    printf("==================================================\n");

    for (int i = 0; i < num_clientes; i++) CloseHandle(threads[i]);
    WSACleanup();
    
    printf("\nTeste finalizado. Verifique os logs do Servidor.\n");
    printf("Pressione Enter para fechar...");
    getchar();
    return 0;
}