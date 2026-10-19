# 🎮 Trabalhos de Sistemas Operacionais - Comunicação via Sockets & Threads

Este repositório contém as implementações de jogos multiplayer em C utilizando **Sockets (WinSock2)** e **Threads (POSIX Threads)** para a disciplina de Sistemas Operacionais.

---

## 📌 Conteúdo do Repositório

O repositório é composto por duas aplicações de jogos em rede utilizando a arquitetura cliente-servidor / peer-to-peer:

1. **Jogo de Dados com Rodada Surpresa** (Servidor/Cliente WinSock com Concorrência)
2. **Cabo de Guerra Multiplayer** (Sockets + Mutex + CondVars com `pthread`)

---

## 🚀 1. Jogo de Dados (Rodada Surpresa)

Aplicação baseada em modelo Servidor/Cliente para 2 jogadores competirem em partidas de rodadas com rolagem de dados e reviravoltas na última rodada.

### 🎯 Funcionalidades
- **Conexão Multithread:** Cada cliente é gerenciado por uma thread individual no servidor (`clientHandler`).
- **Sincronização:** Controle de turnos e estado do jogo protegido por `Mutex` nativo do Windows (`CreateMutex`).
- **Rodada Surpresa (Round 3):** Evento dinâmico onde um jogador sorteado pode escolher trocar sua pontuação com o oponente.

---

## 🪢 2. Cabo de Guerra Multiplayer

Jogo em tempo real onde dois jogadores competem puxando uma corda virtual no console do Windows.

### 🎯 Funcionalidades
- **Interface Visual no Console:** Renderização da corda e status em tempo real utilizando cores personalizadas (`SetConsoleTextAttribute`).
- **Comunicação por Protocolo de Texto:** Envio e decodificação de pacotes padronizados (`H` - Handshake, `G` - Configuração, `P` - Jogada, `C` - Chat, `S` - Sair).
- **Chat Integrado:** Envio de mensagens curtas em tempo real durante a partida (`M mensagem`).
- **Sincronização Concorrente:** Uso de `pthread_mutex` e `pthread_cond` para coordenação dos turnos e thread receptora de dados em background.
- **Mecanica do Dado:** Valores de 1 a 5, com jogada crítica (valor 6) avançando 9 posições.

---

## 🛠️ Tecnologias Utilizadas

- **Linguagem:** C (C99 / C11)
- **Biblioteca de Sockets:** `WinSock2` (`ws2tcpip.h` / `winsock2.h`)
- **Concorrência & Multithread:**
  - `pthread` (POSIX Threads)
  - `Windows API` (`CreateMutex`, `WaitForSingleObject`)
- **SO Suportado:** Windows

---

## 💻 Como Compilar e Executar

### 🛠️ Pré-requisitos
- Compilador GCC (MinGW / Dev-C++ / Code::Blocks no Windows)

---

### ⚙️ Opção 1: Compilação via Linha de Comando (GCC/MinGW)

Para compilar os arquivos via terminal/PowerShell, certifique-se de incluir as flags de linkagem da WinSock e pthreads:

```bash
# Compilar o Jogo de Dados (Servidor)
gcc servidor_dados.c -o servidor_dados.exe -lws2_32

# Compilar o Jogo Cabo de Guerra
gcc cabo_de_CAULE_simples.c -o cabo_de_guerra.exe -lws2_32 -lpthread



O Jogador 1 seleciona a opção 1 - Criar sala e informa a porta (padrão 8888).

O Jogador 2 seleciona a opção 2 - Entrar em sala e informa o IP do Jogador 1.

//--------------------------------------------------------------------------

<ENTER>: Puxa a corda (rola o dado).

M sua mensagem: Envia uma mensagem no chat.

S: Sai da partida.

✒️ Autores
Vitor Hugo Rodrigues Garcia - 

