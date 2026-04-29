```markdown
# 📦 Controle de Estoque Distribuído (C)

Sistema de gerenciamento de estoque desenvolvido em **Linguagem C**, focado em **Computação Paralela** e **Sistemas Distribuídos**. O projeto utiliza Sockets TCP/IP para comunicação cliente-servidor e mecanismos de sincronização para garantir a integridade dos dados.

## 🚀 Status do Projeto: Versão 6.0 (MVP)
Atualmente, o sistema conta com a lógica de concorrência validada e a arquitetura cliente-servidor modelada. 
*Nota: Algumas funcionalidades de rede estão em fase final de integração.*

## 🛠️ Tecnologias Utilizadas
* **Linguagem:** C
* **Concorrência:** Pthreads (POSIX Threads)
* **Sincronização:** Mutex e Semáforos
* **Rede:** Sockets TCP/IP
* **Algoritmos:** QuickSort Paralelo
* **Persistência:** Arquivos Binários (`.dat`)

## 🏗️ Arquitetura
O sistema segue o modelo **Cliente-Servidor**:
1. **Servidor:** Gerencia o estoque, processa as vendas, controla o acesso simultâneo via Mutex e realiza a persistência em disco.
2. **Cliente:** Interface via terminal (CLI) que envia requisições para o servidor.

## 🔒 Mecanismos de Sincronização
Para evitar **Race Conditions** (Condições de Corrida), o projeto implementa:
- **Mutex:** Garante exclusão mútua em operações de escrita (vendas e cadastros).
- **Semáforos:** Controlam o fluxo de leitura simultânea no estoque.

## 📋 Funcionalidades
- [x] Cadastro de Produtos e Categorias.
- [x] Ordenação eficiente via QuickSort $O(n \log n)$.
- [x] Persistência binária automática.
- [x] Logs de concorrência em tempo real.
- [x] Simulação de múltiplos compradores simultâneos.

## 🚀 Como Executar
1. Clone o repositório:
   ```bash
   git clone [https://github.com/SamuelHLC/Controle_de_estoque_projeto_quinta.git](https://github.com/SamuelHLC/Controle_de_estoque_projeto_quinta.git)
   ```
2. Compile o servidor:
   ```bash
   gcc servidor.c -o servidor -lpthread
   ```
3. Compile o cliente:
   ```bash
   gcc cliente.c -o cliente
   ```
4. Execute o servidor primeiro e, em seguida, instâncias do cliente.

## 👥 Equipe
* Samuel Henrique
* Caio Vinícius
* João Victor
* Marcone Oliveira
```

---

### Dicas rápidas:
1. **Organização:** Se os seus arquivos estiverem em pastas diferentes, ajuste os comandos de `gcc` no README.
2. **Prints:** Se tiver tempo depois, tire um print do terminal rodando e coloque no README. Isso valoriza muito o projeto visualmente.
3. **Link do PDF:** Se quiser, pode até subir o PDF do relatório para o Git e linkar no README.

O que acha? Se quiser que eu mude algum ponto técnico (como o nome de algum arquivo), é só falar!
