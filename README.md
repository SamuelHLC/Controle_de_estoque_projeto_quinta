🚀 PASSO A PASSO PARA EXECUÇÃO DO SISTEMA DISTRIBUÍDO
Para que o sistema funcione, precisamos de dois terminais abertos: um para o Servidor (que guarda os dados) e outro(s) para o Cliente (que faz os pedidos).

1. Compilação (Gerar os programas)
Abra o terminal na pasta do projeto e digite estes dois comandos:

Para o Servidor:

DOS
gcc servidor.c -o servidor -lws2_32 -lpthread
Para o Cliente:

DOS
gcc cliente.c -o cliente -lws2_32
2. Execução (Rodar o Sistema)
A) Iniciar o Servidor:
No primeiro terminal, digite:

DOS
servidor.exe
O servidor vai imprimir: "--- SERVIDOR DISTRIBUÍDO ONLINE (Porta 8080) ---". Ele deve ficar aberto o tempo todo.

B) Iniciar o Cliente:
Abra um segundo terminal (ou peça para o João abrir no PC dele se estiverem na mesma rede) e digite:

DOS
cliente.exe
O cliente vai perguntar se você quer consultar ou comprar. Ao escolher, ele manda o dado via Socket, o servidor processa e devolve a resposta.
