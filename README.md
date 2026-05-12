Projeto Integrador: Controle de Estoque Distribuído (V7.0)
Este projeto é um sistema de gestão de estoque desenvolvido em Linguagem C, com Arquitetura Distribuída Real baseada no modelo Cliente-Servidor via Sockets TCP/IP POSIX, rodando em containers Docker isolados que simulam múltiplas máquinas se comunicando em rede.
---
🏗️ Arquitetura do Sistema
```
┌─────────────────────────────────────────────────┐
│              rede-estoque (Docker Bridge)        │
│                                                 │
│  ┌──────────────┐        ┌──────────────────┐   │
│  │  cliente-pdv │──TCP──▶│ servidor-estoque │   │
│  │  (container) │        │   porta 8085     │   │
│  └──────────────┘        │  mutex + sem     │   │
│  ┌──────────────┐        └──────────────────┘   │
│  │  cliente-pdv │──TCP──▶        ▲              │
│  │  (container) │        ┌───────┴──────────┐   │
│  └──────────────┘        │  admin-painel    │   │
│  ┌──────────────┐        │  (via socket)    │   │
│  │  cliente-pdv │──TCP──▶└──────────────────┘   │
│  │  (container) │                               │
│  └──────────────┘                               │
└─────────────────────────────────────────────────┘
                        │
                   Volume /logs
                  (pasta logs/ no Windows)
```
---
🚀 Funcionalidades Técnicas
Comunicação via Sockets TCP/IP POSIX — separação real entre processos independentes na porta 8085
Sincronização Híbrida POSIX — `pthread_mutex_t` para escrita (compras e CRUD) e `sem_t` para leitura (consultas simultâneas)
Concorrência com Threads — `pthread_create` + `pthread_detach` para atender múltiplos clientes em paralelo
Tratamento de Race Condition — regiões críticas protegidas, testáveis via Valgrind Helgrind
Admin via Socket — o painel admin se comunica com o servidor via TCP (opcodes 10–15), eliminando conflito de acesso direto ao arquivo
Ordenação QuickSort — algoritmo O(n log n) para organização dos produtos por ID
Logs Thread-Safe — todas as operações registradas com timestamp em arquivos persistentes
Ambiente Distribuído Real — cada serviço roda em seu próprio container Linux isolado
---
📁 Estrutura de Arquivos
```
Controle_de_estoque_projeto_quinta/
├── servidor.c          # Núcleo: threads, mutex, semáforo, sockets, opcodes CRUD
├── cliente.c           # Interface de compras + modo simulação multi-usuário
├── admin.c             # Painel CRUD via socket TCP (não acessa arquivo diretamente)
├── CMakeLists.txt      # Build moderno com clang + pthread + lrt
├── Dockerfile          # Imagem debian:stable-slim + clang + cmake + valgrind
├── docker-compose.yml  # Orquestração: servidor sobe sozinho, admin/cliente manuais
└── logs/               # Criada automaticamente — logs persistentes
```
---
✅ Pré-requisitos
Docker Desktop instalado e rodando (ícone de baleia na bandeja do Windows)
VSCode (ou qualquer terminal PowerShell)
Nenhum compilador C precisa estar instalado na sua máquina — tudo compila e roda dentro do container.
---
▶️ PASSO A PASSO COMPLETO: Do Zero ao Sistema Rodando
Passo 1 — Abra o terminal do VSCode
```
Ctrl + `
```
Passo 2 — (Apenas na primeira vez ou para resetar tudo) Limpe o ambiente anterior
```powershell
docker stop $(docker ps -aq)
docker rm $(docker ps -aq)
docker volume rm controle_de_estoque_projeto_quinta_dados-estoque
docker network rm controle_de_estoque_projeto_quinta_rede-estoque
Remove-Item -Recurse -Force logs\*
```
Passo 3 — Suba o servidor
```powershell
docker-compose up --build
```
O Docker vai automaticamente:
Baixar a imagem `debian:stable-slim`
Instalar `clang`, `cmake` e `valgrind` no container
Compilar `servidor.c`, `cliente.c` e `admin.c` com clang
Subir apenas o servidor na rede interna `rede-estoque`
> O terminal vai mostrar os logs do servidor em tempo real. **Deixe esse terminal aberto** — ele é o painel de monitoramento do sistema.
---
Passo 4 — Abra um segundo terminal (clique no `+` ao lado do terminal)
4a — Cadastre os produtos pelo admin antes de qualquer compra
```powershell
docker-compose run --rm admin-painel
```
Dentro do menu, use a opção `1` para cadastrar produtos. Ao sair (`10`), os produtos já estão salvos e o servidor os carrega imediatamente na próxima consulta.
> **Por que o admin usa socket?** O admin se comunica com o servidor via TCP (não acessa o arquivo diretamente), garantindo que cadastros e compras nunca conflitem — todas as operações passam pelo mutex do servidor.
4b — Abra o cliente interativo (modo 1 usuário)
```powershell
docker-compose run --rm cliente-pdv
```
Escolha opção 1 — Modo Interativo para navegar e comprar manualmente.
---
Passo 5 — Teste de Simulação Multi-Usuário (Sistemas Distribuídos)
Em um terminal separado, abra um novo cliente e escolha opção 2 — Modo Simulação:
```powershell
docker-compose run --rm cliente-pdv
```
Informe:
Número de usuários simultâneos — recomendado até 90 (veja nota abaixo)
ID do produto a ser comprado
Quantidade que cada usuário tentará comprar
O sistema dispara todas as threads ao mesmo tempo, cada uma abrindo sua própria conexão TCP com o servidor. Ao final exibe o resumo:
```
========== RESULTADO DA SIMULACAO ==========
 Usuarios    : 90
 Produto ID  : 1
 Qtd/usuario : 2
 Confirmadas : 90
 Recusadas   : 0
 Erros       : 0
 Tempo total : 0.2841 s
=============================================
```
> **Nota sobre o limite de conexões simultâneas:** O kernel Linux dentro do container Docker suporta uma fila máxima de ~128 conexões simultâneas (`SOMAXCONN`). Com 100 usuários ou mais disparando ao mesmo tempo, alguns podem receber "ERRO ao conectar" antes mesmo de o servidor aceitar a conexão — isso é um limite do sistema operacional, não do código. Até **90 usuários simultâneos** o sistema opera sem nenhum erro.
---
🖥️ Testando Todas as Funcionalidades
> Cada comando abaixo deve ser rodado em um **terminal separado**. Use o `+` no VSCode para abrir quantos terminais precisar.
---
🔧 FUNCIONALIDADE 1 — Painel Admin (CRUD de Produtos)
```powershell
docker-compose run --rm admin-painel
```
Opção	O que testa
`1` Cadastrar Novo Produto	Envia opcode 11 ao servidor; verificação de ID duplicado
`2` Alterar Nome	Envia opcode 12; log registra valor antes e depois
`3` Alterar Preço	Envia opcode 13; log registra valor antes e depois
`4` Adicionar Unidades	Envia opcode 14; incremento seguro via mutex
`5` Remover Produto	Envia opcode 15; remoção com deslocamento do vetor
`6` / `7` Próxima/Anterior	RF_03: paginação de 5 em 5 com QuickSort por ID
`10` Sair	Encerramento com log de auditoria
---
🛒 FUNCIONALIDADE 2 — Cliente PDV — Modo Interativo
```powershell
docker-compose run --rm cliente-pdv
```
Escolha opção 1.
Opção	O que testa
`1` Comprar	Conexão TCP, mutex de escrita, decremento de estoque, tempo de resposta
`2` Próximo	Navegação entre produtos recebidos via socket
`3` Anterior	Navegação reversa
`4` Sair	Encerramento com log
Para testar estoque insuficiente, tente comprar quantidade maior do que o disponível. O servidor retorna `Erro: Estoque insuficiente`.
---
🔀 FUNCIONALIDADE 3 — Modo Simulação (Múltiplos Usuários Simultâneos)
```powershell
docker-compose run --rm cliente-pdv
```
Escolha opção 2 e informe os parâmetros. Acompanhe os logs do servidor no terminal esquerdo em tempo real — você verá múltiplas threads competindo pelo mutex simultaneamente.
---
🔒 FUNCIONALIDADE 4 — Mutex e Semáforo em Ação
Acompanhe os logs do servidor enquanto faz compras:
```powershell
docker-compose logs -f servidor-estoque
```
Você verá:
```
[MTX] Thread X: aguardando mutex de escrita...
[MTX] Thread X: mutex adquirido — regiao critica
[SALE] Thread X: venda OK | produto 'Mesa' (id=1) | qtd=2 | saldo=898
[MTX] Thread X: liberando mutex
[SEM] Thread X: aguardando semaforo de leitura...
[SEM] Thread X: semaforo adquirido — leitura iniciada
[SEM] Thread X: leitura concluida — liberando semaforo (1 produto(s) enviados)
```
---
📋 FUNCIONALIDADE 5 — Logs Persistentes
Os logs ficam na pasta `logs\` do projeto no Windows:
```powershell
# Log do servidor (conexões, threads, mutex, semáforos, vendas)
type logs\servidor.log

# Log do admin (todas as operações CRUD)
type logs\admin.log

# Log do cliente (substitua pelo PID real — veja no nome do arquivo)
type logs\cliente_1.log

# Listar todos os logs gerados
dir logs\
```
---
🔬 FUNCIONALIDADE 6 — Valgrind Memcheck (Vazamentos de Memória)
```powershell
# Abre shell dentro do container do servidor
docker exec -it servidor-estoque bash

# Roda o Memcheck
valgrind --leak-check=full --track-origins=yes --log-file=/logs/memcheck.log /app/build/servidor

# Sai do shell
exit
```
Relatório em `logs\memcheck.log`.
---
🔬 FUNCIONALIDADE 7 — Valgrind Helgrind (Race Conditions)
```powershell
docker exec -it servidor-estoque bash

valgrind --tool=helgrind --log-file=/logs/helgrind.log /app/build/servidor

exit
```
Relatório em `logs\helgrind.log`. Com mutex e semáforo implementados corretamente, o Helgrind não reporta race conditions.
---
🔍 FUNCIONALIDADE 8 — Inspecionar o Estoque Salvo
```powershell
docker exec -it servidor-estoque bash
cd /app/data && ls -lh
exit
```
---
📊 FUNCIONALIDADE 9 — Monitorar CPU e Memória
```powershell
docker stats
```
Mostra consumo de CPU e memória de cada container separadamente, evidenciando a distribuição real.
---
🌐 FUNCIONALIDADE 10 — Inspecionar a Rede Interna Docker
```powershell
docker network ls
docker network inspect controle_de_estoque_projeto_quinta_rede-estoque
```
Mostra os IPs internos de cada container, comprovando que se comunicam em rede isolada como máquinas distintas.
---
🛑 Encerrando o Sistema
```powershell
# Para os containers mas mantém os dados (logs e estoque.dat)
docker-compose down

# Para tudo E apaga os volumes (estoque.dat perdido, logs permanecem)
docker-compose down -v

# Limpeza completa (use para resetar do zero)
docker stop $(docker ps -aq)
docker rm $(docker ps -aq)
docker volume rm controle_de_estoque_projeto_quinta_dados-estoque
docker network rm controle_de_estoque_projeto_quinta_rede-estoque
Remove-Item -Recurse -Force logs\*
```
---
🔁 Comandos Úteis do Dia a Dia
```powershell
# Reconstruir após alterar algum .c
docker-compose up --build

# Abrir admin
docker-compose run --rm admin-painel

# Abrir cliente
docker-compose run --rm cliente-pdv

# Ver logs do servidor em tempo real
docker-compose logs -f servidor-estoque

# Ver todos os containers (rodando ou parados)
docker ps -a

# Reiniciar só o servidor sem derrubar nada
docker-compose restart servidor-estoque

# Entrar no shell de qualquer container
docker exec -it servidor-estoque bash
```
---
📋 Resumo dos Logs por Serviço
Arquivo	Conteúdo
`logs/servidor.log`	Conexões, threads, mutex/semáforo, vendas, operações CRUD do admin
`logs/cliente_<PID>.log`	Navegação, compras, tempo de resposta TCP, resumo de simulações
`logs/admin.log`	Cadastro, alterações e remoções com valores antes/depois
`logs/memcheck.log`	Relatório Valgrind de vazamentos de memória (gerado manualmente)
`logs/helgrind.log`	Relatório Valgrind de race conditions entre threads (gerado manualmente)
---
🔧 Detalhes Técnicos da Migração Windows → Linux
Windows (versão anterior)	Linux/POSIX (versão atual)
`winsock2.h` + `ws2_32.lib`	`sys/socket.h` + `netinet/in.h`
`CreateMutex` / `WaitForSingleObject`	`pthread_mutex_t` + `pthread_mutex_lock`
`CreateSemaphore` / `ReleaseSemaphore`	`sem_t` + `sem_wait` / `sem_post`
`CreateThread` / `DWORD WINAPI`	`pthread_create` + `pthread_detach`
`GetCurrentProcessId()`	`getpid()`
`system("cls")` / `system("pause")`	`system("clear")` / `getchar()`
`closesocket(s)`	`close(s)`
Admin acessa arquivo diretamente	Admin usa socket TCP (opcodes 10–15)
---
🧩 Opcodes do Protocolo TCP
Opcode	Origem	Operação
`0`	Cliente	Listar produtos (leitura via semáforo)
`2`	Cliente	Comprar produto (escrita via mutex)
`10`	Admin	Listar produtos
`11`	Admin	Cadastrar produto
`12`	Admin	Alterar nome
`13`	Admin	Alterar preço
`14`	Admin	Adicionar unidades
`15`	Admin	Remover produto
