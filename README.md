# Projeto Integrador: Controle de Estoque Distribuído (V9.0)

Este projeto é um sistema de gestão de estoque desenvolvido em **Linguagem C**, com **Arquitetura Distribuída Real** baseada no modelo Cliente-Servidor via **Sockets TCP/IP POSIX**, rodando em containers Docker isolados que simulam múltiplas máquinas se comunicando em rede.

---

## 🏗️ Arquitetura do Sistema

```
┌─────────────────────────────────────────────────────────────────┐
│                  rede-estoque (Docker Bridge)                    │
│                                                                 │
│  ┌──────────────┐          ┌────────────────────────────────┐   │
│  │  cliente-pdv │──TCP──▶  │        servidor-estoque        │   │
│  │  (container) │          │          porta 8085            │   │
│  └──────────────┘          │                                │   │
│  ┌──────────────┐          │  Thread conexao                │   │
│  │  cliente-pdv │──TCP──▶  │    └─▶ enfileira pedido        │   │
│  │  (container) │          │                                │   │
│  └──────────────┘          │  Fila de Compras (FIFO)        │   │
│  ┌──────────────┐          │    └─▶ mutex_fila + cond_fila  │   │
│  │  cliente-pdv │──TCP──▶  │                                │   │
│  │  (container) │          │  Worker Thread (background)    │   │
│  └──────────────┘          │    └─▶ consome fila            │   │
│                            │    └─▶ mutex_estoque           │   │
│  ┌──────────────┐          │    └─▶ processa + responde     │   │
│  │  admin-painel│──TCP──▶  │                                │   │
│  │  (container) │          │  Leitura via sem_t (semaforo)  │   │
│  └──────────────┘          └────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                          │
                     Volume /logs
                  (pasta logs/ no Windows)
```

---

## 🚀 Funcionalidades Técnicas

- **Comunicação via Sockets TCP/IP POSIX** — separação real entre processos independentes na porta 8085
- **Fila de Compras FIFO** — pedidos de compra enfileirados numa fila circular (`FilaCompras`, capacidade 256) protegida por `mutex_fila` e `pthread_cond_t`
- **Thread Worker Dedicada** — `worker_compras` consome a fila em background de forma contínua e independente das threads de conexão (processamento desacoplado)
- **Padrão Produtor/Consumidor** — threads de conexão produzem pedidos na fila; o worker consome e processa sem bloquear novas conexões
- **Mutex de Estoque** — `pthread_mutex_t mutex_estoque` protege o vetor de produtos durante escrita (compras e CRUD do admin)
- **Mutex de Fila** — `pthread_mutex_t mutex_fila` protege a fila circular contra race conditions entre threads produtoras e o worker consumidor
- **Mutex de Log** — `pthread_mutex_t mutex_log` garante que múltiplas threads escrevam no log sem corromper as mensagens
- **Semáforo de Leitura** — `sem_t semaforo_leitura` controla acesso simultâneo de leitura (máximo 2 leitores ao mesmo tempo)
- **Variável de Condição** — `pthread_cond_t cond_fila` acorda o worker quando um novo pedido é enfileirado
- **Concorrência com Threads** — `pthread_create` + `pthread_detach` para atender múltiplos clientes em paralelo
- **Tratamento de Race Condition** — todas as regiões críticas protegidas, testáveis via Valgrind Helgrind
- **Admin via Socket** — o painel admin se comunica com o servidor via TCP (opcodes 10–15), eliminando conflito de acesso direto ao arquivo
- **Simulação Distribuída Real** — cada usuário simulado roda em seu próprio container Docker com PID, memória e log independentes (via `simular.ps1`)
- **Ordenação QuickSort** — algoritmo O(n log n) para organização dos produtos por ID
- **Logs Thread-Safe** — todas as operações registradas com timestamp em arquivos persistentes
- **Ambiente Distribuído Real** — cada serviço roda em seu próprio container Linux isolado na rede `rede-estoque`

---

## 🔄 Fluxo de Compra — Fila + Worker (Paralelismo Real)

```
Cliente (container)
    │
    │  TCP op=2 (comprar)
    ▼
Thread de Conexão
    │
    │  enfileira pedido na FilaCompras
    │  mutex_fila adquirido → pedido inserido → cond_fila sinalizado → mutex liberado
    │
    ▼  retorna imediatamente (não bloqueia)
Thread encerrada

          ▼  (em background, independente)

Worker Thread (sempre rodando)
    │
    │  pthread_cond_wait → acorda quando há pedido
    │  desenfileira pedido
    │  mutex_estoque adquirido
    │  processa compra → decrementa estoque → salva
    │  mutex_estoque liberado
    │  envia resposta ao cliente via socket
    │  fecha socket
    ▼
    aguarda próximo pedido
```

---

## 📁 Estrutura de Arquivos

```
Controle_de_estoque_projeto_quinta/
├── servidor.c          # Núcleo: threads, mutex, semáforo, fila, worker, sockets
├── cliente.c           # Interface de compras — modo interativo e modo automático
├── admin.c             # Painel CRUD via socket TCP (opcodes 10–15)
├── CMakeLists.txt      # Build moderno com clang + pthread + lrt
├── Dockerfile          # Imagem debian:stable-slim + clang + cmake + valgrind
├── docker-compose.yml  # Orquestração: servidor sobe sozinho, admin/cliente manuais
├── simular.ps1         # Script PowerShell: cria 1 container real por usuário simulado
└── logs/               # Criada automaticamente — logs persistentes
```

---

## ✅ Pré-requisitos

- Docker Desktop instalado e rodando (ícone de baleia na bandeja do Windows)
- VSCode (ou qualquer terminal PowerShell)
- Nenhum compilador C precisa estar instalado na sua máquina — tudo compila e roda dentro do container

---

## ▶️ PASSO A PASSO COMPLETO: Do Zero ao Sistema Rodando

### Passo 1 — Abra o terminal do VSCode

```
Ctrl + `
```

### Passo 2 — (Apenas na primeira vez ou para resetar tudo) Limpe o ambiente anterior

> ⚠️ Este comando **não apaga os produtos cadastrados** (volumes preservados). Só limpa containers, imagens e rede.

```powershell
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker network prune -f
```

Se quiser apagar tudo incluindo os produtos:

```powershell
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker volume prune -f; docker network prune -f
```

### Passo 3 — Suba o servidor

```powershell
docker compose up --build
```

O Docker vai automaticamente:
- Baixar a imagem `debian:stable-slim`
- Instalar `clang`, `cmake` e `valgrind` no container
- Compilar `servidor.c`, `cliente.c` e `admin.c` com clang
- Subir apenas o servidor na rede interna `rede-estoque`

> Deixe esse terminal aberto — ele é o painel de monitoramento do sistema em tempo real.

Ao subir, o servidor exibe:
```
[INIT] Thread worker iniciada — fila de compras ativa (FILA_MAX=256)
[INIT] Arquitetura: conexao → fila → worker (desacoplado)
[INIT] Aguardando conexoes na porta 8085...
```

---

### Passo 4 — Abra um segundo terminal (clique no `+` ao lado do terminal)

**4a — Cadastre os produtos pelo admin antes de qualquer compra:**

```powershell
docker compose run --rm admin-painel
```

Dentro do menu use a opção `1` para cadastrar produtos. Ao sair (`10`), os produtos ficam salvos e o servidor os carrega automaticamente.

**4b — Abra o cliente interativo:**

```powershell
docker compose run --rm cliente-pdv
```

O cliente abre direto na loja — navegue pelos produtos e faça compras manualmente.

---

### Passo 5 — Simulação Distribuída Real (1 container por usuário)

#### 5a — Desbloqueie a execução de scripts no Windows (apenas uma vez)

> Se o Windows bloquear a execução do `simular.ps1` com o erro `não pode ser carregado porque a execução de scripts foi desabilitada`, rode este comando:

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned -Force
```

#### 5b — Execute a simulação

```powershell
.\simular.ps1
```

Este script cria **1 container Docker independente por usuário simulado**. Cada container tem:
- PID próprio (processo Linux isolado)
- Memória completamente separada
- Log individual em `logs/cliente_<PID>.log`
- Conexão TCP própria com o servidor

Para mudar o número de usuários, produto ou quantidade, edite o arquivo `simular.ps1` diretamente no VSCode:

```powershell
$jobs = 1..30        # ← número de usuários simulados
-e PRODUTO_ID=1      # ← ID do produto a comprar
-e QUANTIDADE=3      # ← quantidade que cada usuário tenta comprar
```

#### 5c — Verifique os logs gerados pela simulação

```powershell
dir logs
```

Cada `cliente_XXX.log` é prova de um container independente que existiu com seu próprio PID.

---

## 🖥️ Testando Todas as Funcionalidades

> Cada comando abaixo deve ser rodado em um **terminal separado**. Use o `+` no VSCode para abrir quantos terminais precisar.

---

### 🔧 FUNCIONALIDADE 1 — Painel Admin (CRUD de Produtos)

```powershell
docker compose run --rm admin-painel
```

| Opção | O que testa |
|---|---|
| `1` Cadastrar Novo Produto | Envia opcode 11 ao servidor; verificação de ID duplicado via mutex_estoque |
| `2` Alterar Nome | Envia opcode 12; log registra valor antes e depois |
| `3` Alterar Preço | Envia opcode 13; log registra valor antes e depois |
| `4` Adicionar Unidades | Envia opcode 14; incremento seguro via mutex_estoque |
| `5` Remover Produto | Envia opcode 15; remoção com deslocamento do vetor |
| `6` / `7` Próxima/Anterior | Paginação de 5 em 5 com QuickSort por ID |
| `10` Sair | Encerramento com log de auditoria |

---

### 🛒 FUNCIONALIDADE 2 — Cliente PDV — Modo Interativo

```powershell
docker compose run --rm cliente-pdv
```

| Opção | O que testa |
|---|---|
| `1` Comprar | Conexão TCP → enfileira na fila → worker processa via mutex_estoque |
| `2` Próximo | Navegação entre produtos recebidos via socket + semáforo de leitura |
| `3` Anterior | Navegação reversa |
| `4` Sair | Encerramento com log |

Para testar estoque insuficiente, tente comprar quantidade maior do que o disponível. O worker retorna `Erro: Estoque insuficiente`.

---

### 🔀 FUNCIONALIDADE 3 — Simulação Distribuída Real (Múltiplos Containers)

```powershell
.\simular.ps1
```

Cada usuário é um container Docker independente. Acompanhe os logs do servidor em tempo real — você verá as threads enfileirando pedidos e o worker processando em background simultaneamente.

Exemplo de resultado:

```
════════════════════════════════════════════════════
         RESULTADO DA SIMULACAO DISTRIBUIDA
════════════════════════════════════════════════════
 Usuarios    : 30
 Produto ID  : 1
 Qtd/usuario : 3
 Confirmadas : 30
 Recusadas   : 0
 Erros       : 0
 Tempo total : 12.3456s
════════════════════════════════════════════════════
```

---

### 🔒 FUNCIONALIDADE 4 — Mutex, Semáforo, Fila e Worker em Ação

Acompanhe os logs do servidor enquanto faz compras ou roda a simulação:

```powershell
docker compose logs -f servidor-estoque
```

**Leitura de produtos — semáforo controlando acesso simultâneo:**
```
[SEM] Thread X: aguardando semaforo de leitura...
[SEM] Thread X: semaforo adquirido — leitura iniciada
[SEM] Thread X: leitura concluida — liberando semaforo (3 produto(s))
```

**Compra — fila + worker + mutex de estoque:**
```
[FILA]   Thread X: enfileirando compra | produto_id=1 qtd=2
[FILA]   Pedido enfileirado | produto_id=1 qtd=2 | fila=3/256
[WORKER] Processando pedido | produto_id=1 qtd=2 | fila restante=2
[WORKER] Venda OK | produto 'Mesa' (id=1) | qtd=2 | saldo=98
[WORKER] Resposta enviada e socket fechado
```

**Estoque insuficiente — consistência garantida pelo mutex:**
```
[WORKER] Venda RECUSADA | produto 'Mesa' (id=1) | pedido=10 | disponivel=3
```

**CRUD do admin — mutex de estoque protegendo escrita:**
```
[ADMIN] Thread X: CADASTRADO | id=1 nome='Mesa' preco=250.00 qtd=100
[ADMIN] Thread X: nome ALTERADO | 'Mesa' -> 'Mesa Escritorio'
[ADMIN] Thread X: SOMADO | id=1 | +50 | total=150
[ADMIN] Thread X: REMOVIDO | id=1 nome='Mesa Escritorio'
```

**Resumo dos mecanismos de sincronização:**

| Mecanismo | Tipo POSIX | Onde é usado |
|---|---|---|
| `mutex_estoque` | `pthread_mutex_t` | Protege leitura/escrita do vetor de produtos |
| `mutex_fila` | `pthread_mutex_t` | Protege a fila circular de pedidos de compra |
| `mutex_log` | `pthread_mutex_t` | Garante logs sem corrupção entre threads |
| `semaforo_leitura` | `sem_t` | Controla até 2 leituras simultâneas |
| `cond_fila` | `pthread_cond_t` | Acorda o worker quando há pedido na fila |

---

### 📋 FUNCIONALIDADE 5 — Logs Persistentes

Os logs ficam na pasta `logs\` do projeto:

```powershell
# Log do servidor (conexões, threads, mutex, semáforo, fila, worker, vendas)
type logs\servidor.log

# Log do admin (todas as operações CRUD)
type logs\admin.log

# Listar todos os logs gerados (1 arquivo por container cliente)
dir logs\
```

---

### 🔬 FUNCIONALIDADE 6 — Valgrind Memcheck (Vazamentos de Memória)

```powershell
docker exec -it servidor-estoque bash
```

```bash
valgrind --leak-check=full --track-origins=yes --log-file=/logs/memcheck.log /app/build/servidor
exit
```

Relatório em `logs\memcheck.log`.

---

### 🔬 FUNCIONALIDADE 7 — Valgrind Helgrind (Race Conditions)

```powershell
docker exec -it servidor-estoque bash
```

```bash
valgrind --tool=helgrind --log-file=/logs/helgrind.log /app/build/servidor
exit
```

Relatório em `logs\helgrind.log`. Com mutex, semáforo e fila implementados corretamente, o Helgrind não reporta race conditions.

---

### 🔍 FUNCIONALIDADE 8 — Inspecionar o Estoque Salvo

```powershell
docker exec -it servidor-estoque bash
cd /app/data && ls -lh
exit
```

---

### 📊 FUNCIONALIDADE 9 — Monitorar CPU e Memória

```powershell
docker stats
```

Mostra consumo de CPU e memória de cada container separadamente, evidenciando a distribuição real.

---

### 🌐 FUNCIONALIDADE 10 — Inspecionar a Rede Interna Docker

```powershell
docker network ls
docker network inspect controle_de_estoque_projeto_quinta_rede-estoque
```

Mostra os IPs internos de cada container, comprovando que se comunicam em rede isolada como máquinas distintas.

---

## 🛑 Encerrando o Sistema

```powershell
# Para os containers mas mantém os dados (logs e estoque.dat)
docker compose down

# Para tudo E apaga os volumes (estoque.dat perdido, logs permanecem)
docker compose down -v

# Limpeza completa sem apagar produtos
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker network prune -f

# Limpeza completa apagando tudo inclusive produtos
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker volume prune -f; docker network prune -f
```

---

## 🔁 Comandos Úteis do Dia a Dia

```powershell
# Limpar o terminal
cls

# Limpar histórico de comandos permanentemente
Clear-History
Remove-Item (Get-PSReadLineOption).HistorySavePath

# Desbloquear execução de scripts PowerShell (caso o Windows bloqueie)
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned -Force

# Subir o servidor (recompila se houver alterações)
docker compose up --build

# Abrir admin
docker compose run --rm admin-painel

# Abrir cliente interativo
docker compose run --rm cliente-pdv

# Rodar simulação com containers reais
.\simular.ps1

# Ver logs do servidor em tempo real
docker compose logs -f servidor-estoque

# Ver todos os containers rodando
docker ps -a

# Reiniciar só o servidor sem derrubar nada
docker compose restart servidor-estoque

# Entrar no shell do servidor
docker exec -it servidor-estoque bash
```

---

## 📋 Resumo dos Logs por Serviço

| Arquivo | Conteúdo |
|---|---|
| `logs/servidor.log` | Conexões, threads, mutex, semáforo, fila, worker, vendas, CRUD do admin |
| `logs/cliente_<PID>.log` | Compras, tempo de resposta TCP — 1 arquivo por container |
| `logs/admin.log` | Cadastro, alterações e remoções com valores antes/depois |
| `logs/memcheck.log` | Relatório Valgrind de vazamentos de memória (gerado manualmente) |
| `logs/helgrind.log` | Relatório Valgrind de race conditions entre threads (gerado manualmente) |

---

## 🧩 Opcodes do Protocolo TCP

| Opcode | Origem | Operação |
|---|---|---|
| `0` | Cliente | Listar produtos (leitura via semáforo) |
| `2` | Cliente | Comprar produto (enfileira → worker processa via mutex) |
| `10` | Admin | Listar produtos |
| `11` | Admin | Cadastrar produto |
| `12` | Admin | Alterar nome |
| `13` | Admin | Alterar preço |
| `14` | Admin | Adicionar unidades |
| `15` | Admin | Remover produto |

---

## 🔧 Detalhes Técnicos

| Windows (versão anterior) | Linux/POSIX (versão atual) |
|---|---|
| `winsock2.h` + `ws2_32.lib` | `sys/socket.h` + `netinet/in.h` |
| `CreateMutex` / `WaitForSingleObject` | `pthread_mutex_t` + `pthread_mutex_lock` |
| `CreateSemaphore` / `ReleaseSemaphore` | `sem_t` + `sem_wait` / `sem_post` |
| `CreateThread` / `DWORD WINAPI` | `pthread_create` + `pthread_detach` |
| `GetCurrentProcessId()` | `getpid()` |
| `system("cls")` / `system("pause")` | `system("clear")` / `getchar()` |
| `closesocket(s)` | `close(s)` |
| Admin acessa arquivo diretamente | Admin usa socket TCP (opcodes 10–15) |
| Simulação via threads num único processo | Simulação via 1 container Docker por usuário |
| Processamento síncrono direto | Fila FIFO + worker thread desacoplado |