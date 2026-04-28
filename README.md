📦 Sistema de Controle de Estoque Distribuído (v6.0)
Este projeto consiste em um sistema de gerenciamento de estoque robusto, desenvolvido em Linguagem C. A versão atual (6.0) foca na transição de um sistema local para uma Arquitetura Distribuída, utilizando comunicação via rede e controle rigoroso de concorrência.

🎯 GAPs Resolvidos (Parecer 6.0)
GAP 01 (Distribuição): Migração de memória compartilhada para comunicação Cliente-Servidor via Sockets TCP/IP.

GAP 02/03 (Métricas e Testes): Implementação de logs de performance e testes de estresse com múltiplas conexões.

GAP 05 (Rastreabilidade): Mapeamento direto entre Requisitos Funcionais (RF) e funções do código.

🏗️ Arquitetura do Sistema
O sistema opera com dois processos independentes que podem rodar em máquinas distintas:

Servidor (servidor.c): Responsável pelo processamento lógico, persistência em arquivo binário e garantia de atomicidade via Mutex.

Cliente (cliente.c): Interface de comando que encapsula as requisições e as envia via socket para o servidor.

🛠️ Tecnologias e Bibliotecas
Rede: winsock2.h (Windows Sockets API).

Concorrência: pthread.h (POSIX Threads para controle de exclusão mútua).

Linguagem: C (Padrão ISO C11).

🚀 Passo a Passo: Compilação e Execução
1. Requisitos
Compilador GCC (MinGW para Windows).

Biblioteca de sockets vinculada na compilação (-lws2_32).

2. Compilação
Abra o terminal na pasta do projeto e execute:

Bash
# Compilar o Servidor
gcc servidor.c -o servidor -lws2_32 -lpthread

# Compilar o Cliente
gcc cliente.c -o cliente -lws2_32
3. Execução
Sempre inicie o servidor primeiro.

No Terminal 1:

DOS
servidor.exe
No Terminal 2 (e outros):

DOS
cliente.exe
