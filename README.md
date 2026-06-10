# Projeto Integrador: Controle de Estoque Distribuído (V10.0)

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
                     Volume ./logs
              (criada automaticamente pelo Docker)
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
- **Admin via Socket** — o painel admin se comunica com o servidor via TCP (opcodes 10–16), eliminando conflito de acesso direto ao arquivo
- **Simulação Distribuída Real** — cada usuário simulado roda em seu próprio container Docker com IP, PID, memória e log independentes (via `simular.ps1`)
- **Ordenação QuickSort** — algoritmo O(n log n) para organização dos produtos por ID
- **Logs Thread-Safe** — todas as operações registradas com timestamp, IP e nível em arquivos gerados automaticamente
- **Servidor Silencioso** — o servidor não emite nenhuma mensagem ao subir; o log só aparece quando há uma ação real

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
    │  mutex_fila adquirido -> pedido inserido -> cond_fila sinalizado -> mutex liberado
    │
    ▼  retorna imediatamente (não bloqueia)
Thread encerrada

          ▼  (em background, independente)

Worker Thread (sempre rodando)
    │
    │  pthread_cond_wait -> acorda quando há pedido
    │  desenfileira pedido
    │  mutex_estoque adquirido  [log: MUTEX Travando estoque]
    │  processa compra -> decrementa estoque -> salva
    │  mutex_estoque liberado   [log: MUTEX Estoque liberado]
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
├── admin.c             # Painel CRUD via socket TCP (opcodes 10–16)
├── CMakeLists.txt      # Build moderno com clang + pthread + lrt
├── Dockerfile          # Imagem debian:stable-slim + clang + cmake + valgrind
├── docker-compose.yml  # Orquestração: servidor sobe sozinho, admin/cliente manuais
├── simular.ps1         # Script PowerShell: cria 1 container real por usuário simulado
└── logs/               # Criada automaticamente pelo Docker — logs persistentes
```

---

## ✅ Pré-requisitos

- Docker Desktop instalado e rodando (ícone de baleia na bandeja do Windows)
- VSCode (ou qualquer terminal PowerShell)
- Nenhum compilador C precisa estar instalado na sua máquina — tudo compila e roda dentro do container

---

## ▶️ PASSO A PASSO COMPLETO: Do Zero ao Sistema Rodando

### Passo 1 — Abra a pasta do projeto no VSCode

Abra o VSCode, vá em `File > Open Folder` e selecione a pasta `Controle_de_estoque_projeto_quinta`.

Em seguida abra o terminal integrado:

```
Ctrl + `
```

> ⚠️ Todos os comandos abaixo devem ser rodados **dentro da pasta do projeto**. Confirme que o terminal mostra o caminho correto antes de continuar:
> `PS C:\Users\SeuNome\Controle_de_estoque_projeto_quinta>`

---

### Passo 2 — (Apenas na primeira vez ou para resetar tudo) Limpe o ambiente anterior

> ⚠️ Este comando **preserva os produtos cadastrados** (volume `dados-estoque` mantido). Só limpa containers, imagens e rede.

```powershell
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker network prune -f
```

---

### Passo 3 — Suba o servidor

```powershell
docker compose up --build
```

O Docker vai automaticamente:
- Baixar a imagem `debian:stable-slim`
- Instalar `clang`, `cmake` e `valgrind` no container
- Compilar `servidor.c`, `cliente.c` e `admin.c` com clang
- Criar a pasta `logs/` no projeto (mapeada como volume)
- Subir apenas o servidor na rede interna `rede-estoque`

> Deixe esse terminal aberto — ele é o painel de monitoramento do sistema em tempo real.

O servidor sobe **silencioso** — nenhuma mensagem é exibida até que algum cliente se conecte ou uma ação real aconteça.

---

### Passo 4 — Abra um segundo terminal (clique no `+` ao lado do terminal)

**4a — Cadastre os produtos pelo admin antes de qualquer compra:**

```powershell
docker compose run --rm admin-painel
```

Dentro do menu use a opção `1` para cadastrar produtos. Ao sair (`0`), os produtos ficam salvos no volume Docker e o servidor os carrega automaticamente na próxima inicialização.

**4b — Abra o cliente interativo:**

```powershell
docker compose run --rm cliente-pdv
```

O cliente abre direto na loja — navegue pelos produtos e faça compras manualmente.

---

### Passo 5 — Simulação Distribuída Real (1 container por usuário)

#### 5a — Desbloqueie a execução de scripts no Windows (apenas uma vez)

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

Se aparecer o erro `não está assinado digitalmente`, rode também:

```powershell
Unblock-File .\simular.ps1
```

#### 5b — Execute a simulação

```powershell
.\simular.ps1
```

O script abre um menu interativo onde você escolhe quantos usuários subir, o produto e a quantidade — individualmente ou em lote. Cada usuário é um container Docker independente com IP, PID e log próprios. Os containers **ficam ativos após a compra** para inspeção.

#### 5c — Identifique qual container pertence a qual usuário

Pelo log (mostra o número do usuário diretamente):

```powershell
docker logs <id_do_container>
```

Pelo IP de cada container na rede interna:

```powershell
docker inspect --format "{{.Name}} -- {{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}" $(docker ps -q)
```

Exemplo de saída:
```
/cliente-pdv-run-b5bc -- 172.28.0.4
/cliente-pdv-run-3774 -- 172.28.0.5
/servidor-estoque     -- 172.28.0.2
```

O IP exibido aqui é o mesmo que aparece em todos os logs do servidor, permitindo rastrear cada container com precisão.

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
| `1` Cadastrar Novo Produto | Envia opcode 11 ao servidor; ID gerado automaticamente via mutex_estoque |
| `2` Alterar Nome | Envia opcode 12; log registra valor antes e depois |
| `3` Alterar Preço | Envia opcode 13; log registra valor antes e depois |
| `4` Alterar Categoria | Envia opcode 16; altera a categoria do produto via mutex_estoque |
| `5` Adicionar Unidades | Envia opcode 14; incremento seguro via mutex_estoque |
| `6` Remover Produto | Envia opcode 15; remoção com deslocamento do vetor |
| `7` Atualizar | Recarrega a lista do servidor sem nenhuma ação de CRUD |
| `0` Sair | Encerramento com log de auditoria |

---

### 🛒 FUNCIONALIDADE 2 — Cliente PDV — Modo Interativo

```powershell
docker compose run --rm cliente-pdv
```

Ao abrir, o cliente busca os produtos do servidor e exibe o menu de categorias. Categorias com produtos mostram a quantidade disponível; categorias sem produtos aparecem em cinza. Se não houver nenhum produto cadastrado, o cliente avisa e aguarda.

| Opção | O que testa |
|---|---|
| `1` Comprar | Conexão TCP -> enfileira na fila -> worker processa via mutex_estoque |
| `2` Próximo | Navegação entre produtos da categoria selecionada |
| `3` Anterior | Navegação reversa |
| `4` Trocar Categoria | Volta ao menu de categorias para escolher outra |
| `5` Sair | Encerramento com log |

Produtos com estoque zerado aparecem como `ESTOQUE: 0 — INDISPONIVEL` e não permitem compra. Ao tentar comprar quantidade maior que o disponível, o sistema pergunta se deseja comprar a quantidade disponível.

---

### 🔀 FUNCIONALIDADE 3 — Simulação Distribuída Real (Múltiplos Containers)

```powershell
.\simular.ps1
```

Cada usuário é um container Docker independente. O script busca os produtos do servidor antes de simular, mostrando as categorias disponíveis com quantidade de produtos. Categorias sem produtos aparecem em cinza. Produtos com estoque zerado são bloqueados. Se a quantidade solicitada for maior que o estoque, o sistema pergunta se deseja usar a quantidade disponível. Acompanhe os logs do servidor em tempo real — você verá as threads enfileirando pedidos, o mutex travando e liberando o estoque, e o worker processando em background simultaneamente.

---

### 🔒 FUNCIONALIDADE 4 — Mutex, Semáforo, Fila e Worker em Ação

Acompanhe os logs do servidor enquanto faz compras ou roda a simulação:

```powershell
docker compose logs -f servidor-estoque
```

**Leitura de produtos — semáforo controlando acesso simultâneo:**
```
[SEM] Leitura aguardando vaga | cliente=172.28.0.4 | vagas disponiveis=2/2
[SEM] Leitura autorizada      | cliente=172.28.0.4 | vagas restantes=1/2
[SEM] Leitura concluida       | cliente=172.28.0.4 | vagas liberadas=2/2
```

**Compra — fila + mutex de estoque + worker:**
```
[FILA]   Enfileirado | cliente=172.28.0.4 usuario=#1 produto_id=1 qtd=4 | fila=1/256
[FILA]   Enfileirado | cliente=172.28.0.5 usuario=#2 produto_id=1 qtd=4 | fila=2/256
[WORKER] Processando | cliente=172.28.0.4 usuario=#1 produto_id=1 qtd=4 | fila restante=1
[MUTEX]  Travando estoque | cliente=172.28.0.4
[WORKER] Venda OK | cliente=172.28.0.4 usuario=#1 produto='Mesa' qtd=4 saldo=794
[MUTEX]  Estoque liberado | cliente=172.28.0.4
[WORKER] Processando | cliente=172.28.0.5 usuario=#2 produto_id=1 qtd=4 | fila restante=0
[MUTEX]  Travando estoque | cliente=172.28.0.5
[WORKER] Venda OK | cliente=172.28.0.5 usuario=#2 produto='Mesa' qtd=4 saldo=790
[MUTEX]  Estoque liberado | cliente=172.28.0.5
```

**Estoque insuficiente — consistência garantida pelo mutex:**
```
[WORKER] Venda RECUSADA | cliente=172.28.0.4 usuario=#1 produto='Mesa' pedido=10 disponivel=3
```

**CRUD do admin — mutex protegendo escrita:**
```
[MUTEX] Travando estoque | admin=172.28.0.3 | op=cadastrar
[ADMIN] Thread X | admin=172.28.0.3 | CADASTRADO id=1 nome='Mesa' preco=500.00 qtd=800
[MUTEX] Estoque liberado | admin=172.28.0.3 | op=cadastrar
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

A pasta `logs/` e todos os arquivos dentro dela são **gerados automaticamente** pelo Docker e pelos próprios serviços ao longo do uso. Nenhuma pasta ou arquivo precisa ser criado manualmente.

```powershell
# Log do servidor (mutex, semáforo, fila, worker, vendas, CRUD)
type logs\servidor.log

# Log do admin (todas as operações CRUD com valores antes/depois)
type logs\admin.log

# Listar todos os logs gerados (1 arquivo por container cliente simulado)
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

Relatório gerado automaticamente em `logs\memcheck.log`.

---

### 🔬 FUNCIONALIDADE 7 — Valgrind Helgrind (Race Conditions)

```powershell
docker exec -it servidor-estoque bash
```

```bash
valgrind --tool=helgrind --log-file=/logs/helgrind.log /app/build/servidor
exit
```

Relatório gerado automaticamente em `logs\helgrind.log`. Com mutex, semáforo e fila implementados corretamente, o Helgrind não reporta race conditions.

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

## 💾 Persistência dos Dados

Os produtos cadastrados **sobrevivem a reinicializações do servidor**. Isso acontece porque o servidor salva o estoque num arquivo binário chamado `estoque.dat` dentro de um **volume Docker** chamado `dados-estoque`. Toda vez que o servidor sobe, ele executa `carregar_dados()` que lê esse arquivo e carrega os produtos na memória.

O volume Docker existe **independente do container** — mesmo derrubando e subindo o servidor novamente com `docker compose up --build`, o volume permanece intacto com todos os dados.

Para iniciar do zero sem nenhum produto cadastrado:

```powershell
docker compose down -v
```

O `-v` apaga os volumes junto com os containers, removendo o `estoque.dat`.

---

## 🛑 Encerrando o Sistema

```powershell
# Para os containers mas mantém produtos e logs
docker compose down

# Para tudo e apaga o volume de produtos (estoque.dat perdido, logs permanecem)
docker compose down -v

# Limpeza de containers e imagens — preserva produtos e logs
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker network prune -f

# Limpeza completa — apaga containers, imagens e produtos (logs permanecem)
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker volume prune -f; docker network prune -f

# Limpeza TOTAL — apaga tudo: containers, imagens, volumes, produtos E logs
docker stop $(docker ps -aq); docker rm $(docker ps -aq); docker rmi $(docker images -q); docker volume prune -f; docker network prune -f; Remove-Item -Recurse -Force .\logs
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
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
Unblock-File .\simular.ps1

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

# Ver todos os containers (ativos e parados)
docker ps -a

# Ver IP de cada container na rede interna
docker inspect --format "{{.Name}} -- {{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}" $(docker ps -q)

# Reiniciar só o servidor sem derrubar nada
docker compose restart servidor-estoque

# Entrar no shell do servidor
docker exec -it servidor-estoque bash
```

---

## 📋 Resumo dos Logs por Serviço

| Arquivo | Geração | Conteúdo |
|---|---|---|
| `logs/servidor.log` | Automática | Conexões, mutex, semáforo, fila, worker, vendas, CRUD do admin |
| `logs/cliente_<PID>.log` | Automática | Compras e tempo de resposta TCP — 1 arquivo por container simulado |
| `logs/admin.log` | Automática | Cadastro, alterações e remoções com valores antes/depois |
| `logs/memcheck.log` | Manual (Valgrind) | Relatório de vazamentos de memória |
| `logs/helgrind.log` | Manual (Valgrind) | Relatório de race conditions entre threads |

---

## 🧩 Opcodes do Protocolo TCP

| Opcode | Origem | Operação |
|---|---|---|
| `0` | Cliente | Listar produtos (leitura via semáforo — silencioso no log) |
| `2` | Cliente | Comprar produto (enfileira -> worker processa via mutex) |
| `10` | Admin | Listar produtos (silencioso no log — consulta rotineira) |
| `11` | Admin | Cadastrar produto |
| `12` | Admin | Alterar nome |
| `13` | Admin | Alterar preço |
| `14` | Admin | Adicionar unidades |
| `15` | Admin | Remover produto |
| `16` | Admin | Alterar categoria |

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
| Admin acessa arquivo diretamente | Admin usa socket TCP (opcodes 10–16) |
| Simulação via threads num único processo | Simulação via 1 container Docker por usuário |
| Processamento síncrono direto | Fila FIFO + worker thread desacoplado |
