/*
 *   CABO DE GUERRA (MULTIPLAYER, 2 PROCESSOS) - versao simplificada
 *
 *   Compilacao (Dev-C++ / MinGW):
 *       gcc cabo_de_CAULE_simples.c -o jogo.exe -lws2_32 -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <pthread.h>

#pragma comment(lib, "ws2_32.lib")

#define PORTA_PADRAO     8888
#define TAM_LINHA        128
#define TAM_NOME         16
#define TAM_TEXTO        60
#define TAM_CABO         10      /* posicoes 0..10, centro = 5 */
#define CENTRO           5
#define RODADAS_PADRAO   4
#define RODADAS_MAX      10
#define VERSAO_PROTOCOLO 3
#define DADO_CRITICO     9
#define TAM_LOG          6
#define TAM_LOG_LINHA    100
#define LARGURA_CORDA    ((TAM_CABO + 1) * 3)

#define COR_PADRAO  7
#define COR_J1      11
#define COR_J2      13
#define COR_OK      10
#define COR_ALERTA  14
#define COR_ERRO    12
#define COR_CINZA   8

enum { FIM_NENHUM = 0, FIM_VENCI, FIM_PERDI, FIM_OPONENTE_SAIU,
       FIM_DESISTI, FIM_CONEXAO_PERDIDA, FIM_ERRO_PROTOCOLO };

enum { ACAO_NENHUMA = 0, ACAO_JOGAR, ACAO_SAIR, ACAO_CHAT, ACAO_INVALIDA };

typedef struct {
    char tipo;
    int  id, valor, pos;
    char status;
    char texto[TAM_TEXTO + 1];
} Mensagem;

/* --- Estado compartilhado (protegido por mutex_jogo) --- */
int  posicao_cabo = CENTRO;   /* 0 = vitoria J1 | 10 = vitoria J2 */
int  vez = 1;
int  meu_id = 0;
int  jogo_ativo = 1;
int  motivo_fim = FIM_NENHUM;
int  placar[3] = {0, 0, 0};
int  rodada_atual = 1;
int  rodadas_para_vencer = RODADAS_PADRAO;
int  primeiro_jogador = 1;
char nome[3][TAM_NOME];
char log_eventos[TAM_LOG][TAM_LOG_LINHA];
int  log_qtd = 0;
int  lendo_teclado = 0;
int  pular_espera = 0;
int  porta = PORTA_PADRAO;
SOCKET sock_conexao = INVALID_SOCKET;

pthread_mutex_t mutex_jogo = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond_jogo  = PTHREAD_COND_INITIALIZER;

/* ========================== Utilitarios ========================== */

void set_cor(WORD cor) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), cor);
}

void trim(char *s) {
    char *ini = s;
    size_t len;
    while (*ini && isspace((unsigned char)*ini)) ini++;
    if (ini != s) memmove(s, ini, strlen(ini) + 1);
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

void sanitizar_texto(char *s) {
    char *w = s;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 32 && c != 127) *w++ = (char)c;
    }
    *w = '\0';
}

bool ler_linha_teclado(char *buf, int tam) {
    if (fgets(buf, tam, stdin) == NULL) return false;
    if (strchr(buf, '\n') == NULL) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    buf[strcspn(buf, "\n")] = '\0';
    trim(buf);
    return true;
}

/* Mostra um prompt e le uma linha. */
bool perguntar(const char *prompt, char *buf, int tam) {
    set_cor(COR_ALERTA);
    printf("%s", prompt);
    set_cor(COR_PADRAO);
    fflush(stdout);
    return ler_linha_teclado(buf, tam);
}

/* Le um numero entre min e max (Enter = padrao). */
bool ler_numero(const char *prompt, int min, int max, int padrao, int *saida) {
    char linha[TAM_LINHA], *fim;
    long v;
    while (1) {
        if (!perguntar(prompt, linha, sizeof(linha))) return false;
        if (linha[0] == '\0') { *saida = padrao; return true; }
        v = strtol(linha, &fim, 10);
        if (*fim == '\0' && v >= min && v <= max) { *saida = (int)v; return true; }
        set_cor(COR_ERRO);
        printf("Valor invalido (%d a %d).\n", min, max);
        set_cor(COR_PADRAO);
    }
}

void esperar_enter(void) {
    char lixo[TAM_LINHA];
    printf("\nPressione ENTER para sair...");
    fflush(stdout);
    ler_linha_teclado(lixo, sizeof(lixo));
}

/* ========================== Regras ========================== */

bool dado_valido(int d) { return (d >= 1 && d <= 5) || d == DADO_CRITICO; }

int rolar_dado(void) {
    int d = (rand() % 6) + 1;
    return (d == 6) ? DADO_CRITICO : d;
}

int calcular_posicao(int pos, int id, int dado) {
    pos += (id == 1) ? -dado : dado;
    if (pos < 0) pos = 0;
    if (pos > TAM_CABO) pos = TAM_CABO;
    return pos;
}

bool venceu_rodada(int id, int pos) {
    return (id == 1 && pos <= 0) || (id == 2 && pos >= TAM_CABO);
}

void log_evento(const char *fmt, ...) {
    char linha[TAM_LOG_LINHA];
    va_list ap;
    int i;

    va_start(ap, fmt);
    vsnprintf(linha, sizeof(linha), fmt, ap);
    va_end(ap);

    if (log_qtd == TAM_LOG) {
        for (i = 1; i < TAM_LOG; i++) strcpy(log_eventos[i - 1], log_eventos[i]);
        log_qtd--;
    }
    strcpy(log_eventos[log_qtd++], linha);
}

/* ========================== Interface ========================== */

void desenhar_cabo(void) {
    int i, pad;

    system("cls");

    /* Placar */
    set_cor(COR_PADRAO);
    printf("== CABO DE GUERRA == Rodada %d (vence quem ganhar %d)\n\n",
           rodada_atual, rodadas_para_vencer);
    printf("  ");
    set_cor(COR_J1);
    printf("%s", nome[1]);
    set_cor(COR_PADRAO);
    printf("  %d x %d  ", placar[1], placar[2]);
    set_cor(COR_J2);
    printf("%s\n\n", nome[2]);

    /* Corda: nomes nas pontas, marca [O] na posicao atual */
    pad = LARGURA_CORDA - (int)strlen(nome[1]) - (int)strlen(nome[2]);
    if (pad < 1) pad = 1;
    printf("  ");
    set_cor(COR_J1);
    printf("%s", nome[1]);
    printf("%*s", pad, "");
    set_cor(COR_J2);
    printf("%s\n  ", nome[2]);

    for (i = 0; i <= TAM_CABO; i++) {
        if (i == posicao_cabo)      { set_cor(COR_ALERTA); printf("[O]"); }
        else if (i == 0)            { set_cor(COR_J1);     printf("###"); }
        else if (i == TAM_CABO)     { set_cor(COR_J2);     printf("###"); }
        else if (i == CENTRO)       { set_cor(COR_CINZA);  printf("-|-"); }
        else                        { set_cor(COR_CINZA);  printf("---"); }
    }
    printf("\n\n");

    /* Eventos */
    set_cor(COR_PADRAO);
    for (i = 0; i < log_qtd; i++) printf("  %.72s\n", log_eventos[i]);
    for (i = log_qtd; i < TAM_LOG; i++) printf("\n");

    /* Acao */
    printf("\n");
    if (jogo_ativo) {
        if (vez == meu_id) {
            set_cor(COR_OK);
            printf("SUA VEZ: ENTER = puxar | M texto = chat | S = sair\n> ");
        } else {
            set_cor(COR_ALERTA);
            printf("Aguardando %s jogar...\n", nome[vez]);
        }
    }
    set_cor(COR_PADRAO);
    fflush(stdout);
}

void mostrar_fim(void) {
    int outro = (meu_id == 1) ? 2 : 1;
    printf("\n");
    switch (motivo_fim) {
        case FIM_VENCI:
            set_cor(COR_OK);
            printf("VOCE VENCEU! (%d x %d)\n", placar[meu_id], placar[outro]);
            break;
        case FIM_PERDI:
            set_cor(COR_ERRO);
            printf("VOCE PERDEU. (%d x %d)\n", placar[meu_id], placar[outro]);
            break;
        case FIM_OPONENTE_SAIU:
            set_cor(COR_ALERTA);
            printf("O oponente saiu. Voce venceu por W.O.!\n");
            break;
        case FIM_DESISTI:
            printf("Voce saiu da Partida.\n");
            break;
        case FIM_CONEXAO_PERDIDA:
            set_cor(COR_ERRO);
            printf("Conexao perdida. Partida encerrada.\n");
            break;
        case FIM_ERRO_PROTOCOLO:
            set_cor(COR_ERRO);
            printf("Erro de comunicacao. Partida encerrada.\n");
            break;
        default: break;
    }
    set_cor(COR_PADRAO);
    fflush(stdout);
}

void encerrar_SURUBA(int motivo) {
    jogo_ativo = 0;
    motivo_fim = motivo;
    desenhar_cabo();
    mostrar_fim();
    pthread_cond_broadcast(&cond_jogo);
}

void encerrar_pela_thread(int motivo) {
    pular_espera = lendo_teclado;
    encerrar_SURUBA(motivo);
    if (lendo_teclado) {
        printf("\nPressione ENTER para sair...");
        fflush(stdout);
    }
}

void finalizar_rodada(int vencedor) {
    int outro_do_primeiro = (primeiro_jogador == 1) ? 2 : 1;

    placar[vencedor]++;
    log_evento("%s venceu a rodada %d!", nome[vencedor], rodada_atual);

    if (placar[vencedor] >= rodadas_para_vencer) {
        encerrar_SURUBA(vencedor == meu_id ? FIM_VENCI : FIM_PERDI);
        return;
    }

    rodada_atual++;
    posicao_cabo = CENTRO;
    vez = (rodada_atual % 2 == 1) ? primeiro_jogador : outro_do_primeiro;
    log_evento("Rodada %d - comeca %s.", rodada_atual, nome[vez]);
    desenhar_cabo();
    pthread_cond_broadcast(&cond_jogo);
}

/* ========================== Rede ========================== */

bool enviar_msg(char tipo, int id, int valor, int pos, char status, const char *texto) {
    char msg[TAM_LINHA];
    int total, enviados = 0, r;

    snprintf(msg, sizeof(msg), "%c:%d:%d:%d:%c:%.*s\n",
             tipo, id, valor, pos, status, TAM_TEXTO, texto ? texto : "");
    total = (int)strlen(msg);
    while (enviados < total) {
        r = send(sock_conexao, msg + enviados, total - enviados, 0);
        if (r == SOCKET_ERROR || r == 0) return false;
        enviados += r;
    }
    return true;
}

bool receber_linha(char *buf, int tam) {
    int n = 0;
    char ch;
    while (1) {
        int r = recv(sock_conexao, &ch, 1, 0);
        if (r <= 0) return false;
        if (ch == '\n') { buf[n] = '\0'; return true; }
        if (ch != '\r' && n < tam - 1) buf[n++] = ch;
    }
}

bool decodificar_msg(const char *linha, Mensagem *m) {
    char tipo, status;
    int n = 0;

    memset(m, 0, sizeof(*m));
    if (sscanf(linha, "%c:%d:%d:%d:%c:%n", &tipo, &m->id, &m->valor, &m->pos,
               &status, &n) < 5 || n == 0)
        return false;
    if (m->id != 1 && m->id != 2) return false;

    m->tipo = tipo;
    m->status = status;
    strncpy(m->texto, linha + n, TAM_TEXTO);
    m->texto[TAM_TEXTO] = '\0';
    sanitizar_texto(m->texto);
    return true;
}

bool handshake(void) {
    char buf[TAM_LINHA];
    Mensagem m;
    int oponente = (meu_id == 1) ? 2 : 1;

    if (!enviar_msg('H', meu_id, VERSAO_PROTOCOLO, 0, 'N', nome[meu_id]) ||
        !receber_linha(buf, sizeof(buf)) || !decodificar_msg(buf, &m)) {
        printf("Handshake falhou ou conexao encerrada.\n");
        return false;
    }
    if (m.tipo != 'H' || m.id != oponente) {
        printf("O outro lado nao e um jogador %d valido.\n", oponente);
        return false;
    }
    if (m.valor != VERSAO_PROTOCOLO) {
        printf("Versao incompativel (local %d, remota %d).\n", VERSAO_PROTOCOLO, m.valor);
        return false;
    }
    trim(m.texto);
    if (m.texto[0] == '\0') snprintf(m.texto, sizeof(m.texto), "Jogador %d", oponente);
    snprintf(nome[oponente], TAM_NOME, "%s", m.texto);

    if (meu_id == 1) {
        primeiro_jogador = (rand() % 2) + 1;
        if (!enviar_msg('G', meu_id, rodadas_para_vencer, primeiro_jogador, 'N', "")) {
            printf("Falha ao enviar configuracao.\n");
            return false;
        }
    } else {
        if (!receber_linha(buf, sizeof(buf)) || !decodificar_msg(buf, &m)
            || m.tipo != 'G' || m.id != 1
            || m.valor < 1 || m.valor > RODADAS_MAX
            || (m.pos != 1 && m.pos != 2)) {
            printf("Configuracao invalida.\n");
            return false;
        }
        rodadas_para_vencer = m.valor;
        primeiro_jogador = m.pos;
    }
    vez = primeiro_jogador;
    return true;
}

void* thread_receber_mensagens(void* arg) {
    char buffer[TAM_LINHA];
    Mensagem m;
    int oponente = (meu_id == 1) ? 2 : 1;
    (void)arg;

    while (1) {
        bool ok = receber_linha(buffer, sizeof(buffer));
        bool encerrou = false;

        pthread_mutex_lock(&mutex_jogo);

        if (!jogo_ativo) {
            pthread_mutex_unlock(&mutex_jogo);
            break;
        }
        if (!ok) {
            encerrar_pela_thread(FIM_CONEXAO_PERDIDA);
            pthread_mutex_unlock(&mutex_jogo);
            break;
        }
        if (!decodificar_msg(buffer, &m) || m.id != oponente) {
            encerrar_pela_thread(FIM_ERRO_PROTOCOLO);
            pthread_mutex_unlock(&mutex_jogo);
            break;
        }

        switch (m.tipo) {
        case 'S':
            encerrar_pela_thread(FIM_OPONENTE_SAIU);
            encerrou = true;
            break;

        case 'C':
            log_evento("%s: %s", nome[oponente], m.texto);
            desenhar_cabo();
            break;

        case 'P':
            if (vez != oponente
                || (m.status != 'N' && m.status != 'V')
                || !dado_valido(m.valor)
                || m.pos != calcular_posicao(posicao_cabo, oponente, m.valor)
                || (m.status == 'V') != venceu_rodada(oponente, m.pos)) {
                encerrar_pela_thread(FIM_ERRO_PROTOCOLO);
                encerrou = true;
                break;
            }
            posicao_cabo = m.pos;
            log_evento("%s puxou %d%s", nome[oponente], m.valor,
                       (m.valor >= DADO_CRITICO) ? " (CRITICO!)" : "");
            if (m.status == 'V') {
                finalizar_rodada(oponente);
            } else {
                vez = meu_id;
                desenhar_cabo();
                pthread_cond_broadcast(&cond_jogo);
            }
            break;

        default:
            encerrar_pela_thread(FIM_ERRO_PROTOCOLO);
            encerrou = true;
            break;
        }

        pthread_mutex_unlock(&mutex_jogo);
        if (encerrou) break;
    }
    return NULL;
}

/* ========================== Conexao ========================== */

void mostrar_ips_locais(void) {
    char host[256];
    struct hostent *he;
    int i;

    if (gethostname(host, sizeof(host)) == 0 &&
        (he = gethostbyname(host)) != NULL && he->h_addrtype == AF_INET) {
        printf("Seu IP (passe ao Jogador 2):\n");
        for (i = 0; he->h_addr_list[i] != NULL; i++) {
            struct in_addr a;
            memcpy(&a, he->h_addr_list[i], sizeof(a));
            printf("  %s\n", inet_ntoa(a));
        }
    }
}

bool iniciar_servidor(void) {
    SOCKET server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        printf("Erro ao criar socket (WSA %d).\n", WSAGetLastError());
        return false;
    }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)porta);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR ||
        listen(server_fd, 1) == SOCKET_ERROR) {
        printf("Erro ao abrir a porta %d (WSA %d). Porta em uso?\n", porta, WSAGetLastError());
        closesocket(server_fd);
        return false;
    }

    printf("\n");
    mostrar_ips_locais();
    printf("\nAguardando o Jogador 2 na porta %d...\n", porta);

    sock_conexao = accept(server_fd, (struct sockaddr*)&address, &addrlen);
    closesocket(server_fd);

    if (sock_conexao == INVALID_SOCKET) {
        printf("Erro no accept (WSA %d).\n", WSAGetLastError());
        return false;
    }
    printf("Jogador 2 conectado!\n");
    return true;
}

bool iniciar_cliente(void) {
    struct sockaddr_in address;
    char ip_servidor[TAM_LINHA];
    unsigned long ip;

    while (1) {
        if (!perguntar("IP do Jogador 1 [Enter = 127.0.0.1]: ", ip_servidor, sizeof(ip_servidor)))
            return false;
        if (ip_servidor[0] == '\0') strcpy(ip_servidor, "127.0.0.1");
        ip = inet_addr(ip_servidor);
        if (ip != INADDR_NONE) break;
        set_cor(COR_ERRO);
        printf("IP invalido!\n");
        set_cor(COR_PADRAO);
    }

    sock_conexao = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_conexao == INVALID_SOCKET) {
        printf("Erro ao criar socket (WSA %d).\n", WSAGetLastError());
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)porta);
    address.sin_addr.s_addr = ip;

    printf("Conectando a %s:%d...\n", ip_servidor, porta);
    if (connect(sock_conexao, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Nao foi possivel conectar (WSA %d). A sala ja foi criada?\n", WSAGetLastError());
        closesocket(sock_conexao);
        sock_conexao = INVALID_SOCKET;
        return false;
    }
    printf("Conectado!\n");
    return true;
}

/* ========================== Entrada ========================== */

int interpretar_comando(char *linha, char **texto) {
    *texto = NULL;
    if (linha[0] == '\0') return ACAO_JOGAR;
    if ((linha[0] == 's' || linha[0] == 'S') && linha[1] == '\0') return ACAO_SAIR;
    if ((linha[0] == 'm' || linha[0] == 'M') && linha[1] == ' ') {
        trim(linha + 2);
        if (linha[2] != '\0') { *texto = linha + 2; return ACAO_CHAT; }
    }
    return ACAO_INVALIDA;
}

/* ========================== Main ========================== */

int main(void) {
    WSADATA wsa;
    pthread_t thread_id;
    char linha[TAM_LINHA];
    int opcao, oponente;
    bool conectou;

    srand((unsigned int)time(NULL));
    strcpy(nome[1], "Jogador 1");
    strcpy(nome[2], "Jogador 2");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Falha ao inicializar Winsock.\n");
        esperar_enter();
        return 1;
    }

    /* Menu */
    system("cls");
    set_cor(COR_J1);
    printf("== CABO DE GUERRA ==\n");
    set_cor(COR_PADRAO);
    printf("Puxe a corda para o seu lado! Cada puxada anda de 1 a 5 casas\n");
    printf("(o 6 e CRITICO e anda 9). Ganha a rodada quem chegar na ponta.\n\n");
    printf("  1 - Criar sala\n");
    printf("  2 - Entrar em sala\n\n");

    if (!ler_numero("Opcao: ", 1, 2, 1, &opcao)) { WSACleanup(); return 1; }
    meu_id = opcao;
    oponente = (meu_id == 1) ? 2 : 1;
    printf("\n");

    /* Configuracao */
    snprintf(linha, sizeof(linha), "Seu nome [Enter = Jogador %d]: ", meu_id);
    if (!perguntar(linha, linha, sizeof(linha))) { WSACleanup(); return 1; }
    sanitizar_texto(linha);
    trim(linha);
    if (linha[0] == '\0') snprintf(linha, sizeof(linha), "Jogador %d", meu_id);
    snprintf(nome[meu_id], TAM_NOME, "%s", linha);

    if (!ler_numero("Porta [Enter = 8888]: ", 1024, 65535, PORTA_PADRAO, &porta)) {
        WSACleanup(); return 1;
    }
    if (meu_id == 1 &&
        !ler_numero("Rodadas para vencer [Enter = 4]: ", 1, RODADAS_MAX,
                    RODADAS_PADRAO, &rodadas_para_vencer)) {
        WSACleanup(); return 1;
    }

    /* Conexao */
    conectou = (meu_id == 1) ? iniciar_servidor() : iniciar_cliente();
    if (conectou) {
        int nodelay = 1;
        setsockopt(sock_conexao, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        conectou = handshake();
    }
    if (!conectou) {
        printf("\nErro ao iniciar.\n");
        if (sock_conexao != INVALID_SOCKET) closesocket(sock_conexao);
        WSACleanup();
        esperar_enter();
        return 1;
    }

    log_evento("%s vs %s - comeca %s.", nome[1], nome[2], nome[primeiro_jogador]);

    if (pthread_create(&thread_id, NULL, thread_receber_mensagens, NULL) != 0) {
        printf("Falha ao criar thread receptora.\n");
        closesocket(sock_conexao);
        WSACleanup();
        esperar_enter();
        return 1;
    }

    pthread_mutex_lock(&mutex_jogo);
    desenhar_cabo();
    pthread_mutex_unlock(&mutex_jogo);

    /* Loop principal */
    while (1) {
        bool ativo, lido;
        int acao;
        char *texto_chat;

        pthread_mutex_lock(&mutex_jogo);
        while (jogo_ativo && vez != meu_id)
            pthread_cond_wait(&cond_jogo, &mutex_jogo);
        ativo = jogo_ativo;
        if (ativo) lendo_teclado = 1;
        pthread_mutex_unlock(&mutex_jogo);

        if (!ativo) break;

        FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
        lido = ler_linha_teclado(linha, sizeof(linha));
        if (!lido) strcpy(linha, "s");
        acao = interpretar_comando(linha, &texto_chat);

        if (acao == ACAO_SAIR && lido) {
            char conf[TAM_LINHA];
            printf("Sair da Partida? (s/n): ");
            fflush(stdout);
            if (ler_linha_teclado(conf, sizeof(conf)) &&
                !((conf[0] == 's' || conf[0] == 'S') && conf[1] == '\0'))
                acao = ACAO_NENHUMA;
        }

        pthread_mutex_lock(&mutex_jogo);
        lendo_teclado = 0;

        if (!jogo_ativo) {
            pthread_mutex_unlock(&mutex_jogo);
            break;
        }

        switch (acao) {
        case ACAO_SAIR:
            enviar_msg('S', meu_id, 0, 0, 'N', "");
            encerrar_SURUBA(FIM_DESISTI);
            break;

        case ACAO_CHAT:
            if (texto_chat) {
                sanitizar_texto(texto_chat);
                if (!enviar_msg('C', meu_id, 0, 0, 'N', texto_chat)) {
                    encerrar_SURUBA(FIM_CONEXAO_PERDIDA);
                } else {
                    log_evento("Voce: %s", texto_chat);
                    desenhar_cabo();
                }
            }
            break;

        case ACAO_JOGAR: {
            int dado = rolar_dado();
            int nova = calcular_posicao(posicao_cabo, meu_id, dado);
            bool venceu = venceu_rodada(meu_id, nova);

            if (!enviar_msg('P', meu_id, dado, nova, venceu ? 'V' : 'N', "")) {
                encerrar_SURUBA(FIM_CONEXAO_PERDIDA);
                break;
            }
            posicao_cabo = nova;
            log_evento("Voce puxou %d%s", dado, (dado >= DADO_CRITICO) ? " (CRITICO!)" : "");

            if (venceu) {
                finalizar_rodada(meu_id);
            } else {
                vez = oponente;
                desenhar_cabo();
            }
            break;
        }

        case ACAO_INVALIDA:
            log_evento("Comando invalido. Use ENTER, M texto ou S.");
            desenhar_cabo();
            break;

        default:
            desenhar_cabo();
            break;
        }
        pthread_mutex_unlock(&mutex_jogo);
    }

    /* Encerramento */
    pthread_mutex_lock(&mutex_jogo);
    if (!pular_espera) {
        pthread_mutex_unlock(&mutex_jogo);
        esperar_enter();
    } else {
        pthread_mutex_unlock(&mutex_jogo);
    }

    shutdown(sock_conexao, SD_BOTH);
    pthread_join(thread_id, NULL);
    closesocket(sock_conexao);
    pthread_mutex_destroy(&mutex_jogo);
    pthread_cond_destroy(&cond_jogo);
    WSACleanup();
    return 0;
}
