---

# Projeto Integrador: Controle de Estoque Distribuído (V6.0)

Este projeto consiste em um sistema de gestão de estoque desenvolvido em **Linguagem C**, utilizando uma **Arquitetura Distribuída Real** baseada no modelo Cliente-Servidor via Sockets TCP/IP[cite: 1].

## 🚀 Evolução Técnica (Versão 6.0)
A versão atual foca na transição para um ambiente distribuído, separando o núcleo de processamento (Servidor) das interfaces de operação (Cliente e Admin)[cite: 1].

### Principais Funcionalidades
*   **Comunicação via Sockets TCP/IP:** Separação real entre processos independentes conectando-se pela porta 8080[cite: 1].
*   **Sincronização Híbrida:** Uso de **Mutex** para escrita (compras) e **Semáforos** para leitura (consultas simultâneas)[cite: 1].
*   **Tratamento de Race Condition:** Bloqueio de regiões críticas para evitar inconsistências no estoque[cite: 1].
*   **Ordenação QuickSort:** Implementação do algoritmo $O(n \log n)$ para organização dos produtos[cite: 1].

---

## 🛠️ Guia de Instalação e Execução

Para rodar este projeto no **VS Code**, é necessário utilizar o compilador **GCC (MinGW)** e "linkar" a biblioteca de rede do Windows (`ws2_32`)[cite: 1].

### 1. Compilação
Abra o terminal do VS Code e execute os seguintes comandos:

```bash
# Compilar o Servidor (Requer linkagem de rede)
gcc servidor.c -o servidor -lws2_32

# Compilar o Cliente (Requer linkagem de rede)
gcc cliente.c -o cliente -lws2_32

# Compilar o Administrador
gcc admin.c -o admin
```

### 2. Ordem de Execução (Importante)
Para o funcionamento correto da arquitetura, siga esta sequência:

1.  **Módulo Admin:** Execute o `./admin.exe` primeiro para cadastrar alguns produtos iniciais. Isso criará o arquivo `estoque.dat`[cite: 1].
2.  **Servidor:** Execute o `./servidor.exe`. Ele ficará em modo "Listening", aguardando conexões na porta 8080[cite: 1].
3.  **Clientes:** Abra múltiplos terminais no VS Code e execute `./cliente.exe` em cada um deles. Isso simula múltiplos usuários acessando o sistema simultaneamente[cite: 1].

### 3. Como Validar os Requisitos
*   **Concorrência (RF_09):** Observe o terminal do **Servidor**. Ele imprimirá logs como `[MTX] Aguardando Mutex` e `[MTX] Em Seção Crítica` quando dois clientes tentarem comprar o mesmo item ao mesmo tempo[cite: 1].
*   **Desempenho:** Ao realizar uma compra no **Cliente**, o sistema exibirá o **Tempo de Resposta** da transação em segundos[cite: 1].
*   **Persistência:** Feche o servidor e abra-o novamente; os dados do estoque estarão preservados conforme o último estado salvo no arquivo binário[cite: 1].

---

## 🏗️ Estrutura de Arquivos
*   `servidor.c`: Núcleo de processamento e gerenciamento de concorrência[cite: 1].
*   `cliente.c`: Interface de usuário para consultas e compras distribuídas[cite: 1].
*   `admin.c`: Ferramenta de gestão de inventário e persistência inicial[cite: 1].
*   `estoque.dat`: Base de dados binária (gerada automaticamente)[cite: 1].

## 👥 Equipe
*   Samuel Henrique
*   João Victor Fernandes
*   Caio Vinícius da Silva Vilanova
*   Marcone Oliveira da Silva

**Professor:** Jorge Osvaldo Alves de Lima Torres[cite: 1]
