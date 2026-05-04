#ifndef PLACAR_H
#define PLACAR_H

#include "common.h"

/* -----------------------------------------------------------------------
 * Constantes do programa placar
 * ----------------------------------------------------------------------- */

#define PLACAR_CMD_LIGAR    _T("liga")
#define PLACAR_CMD_DESLIGAR _T("desliga")

#define PLACAR_MAX_CMD_LEN  64

/* Prefixo do named pipe (deve ser igual ao usado no central) */
#define PIPE_PREFIX         _T("\\\\.\\pipe\\")

/* Tamanho do buffer de leitura do pipe */
#define PIPE_BUFFER_SIZE    512

/* -----------------------------------------------------------------------
 * Estado global do placar (partilhado entre threads)
 * ----------------------------------------------------------------------- */

typedef struct {
    DWORD  identificador;       /* atribuido pelo central                         */
    BOOL   ligado;              /* TRUE apos comando "ligar"                      */
    BOOL   terminar;            /* TRUE quando se deve encerrar                   */
    TCHAR  nomePipe[MAX_PATH];  /* nome base do pipe (ex: "tubo")                 */
    TCHAR  nomePipeCompleto[MAX_PATH]; /* \\.\pipe\<nomePipe>                     */
    MSG_ALERTA alertaAtivo;     /* alerta correntemente ativo                     */
    BOOL   temAlerta;           /* TRUE se existe um alerta ativo                 */

    /* M2: named pipe para comunicacao com o central */
    HANDLE hPipe;               /* handle do named pipe (cliente)                 */

    /* Sincronizacao */
    HANDLE hWaitableTimer;      /* timer para a duracao do alerta                 */
    HANDLE hPipeEvent;          /* evento de sobreposicao (overlapped) do pipe    */
    CRITICAL_SECTION cs;        /* protege o estado partilhado entre threads      */
} EstadoPlacar;

/* -----------------------------------------------------------------------
 * Prototipos das funcoes
 * ----------------------------------------------------------------------- */

/* Inicializacao e limpeza */
BOOL  inicializar(EstadoPlacar *estado, int argc, TCHAR *argv[]);
BOOL  libertar(EstadoPlacar *estado);

/* Registry */
BOOL  lerNomePipeDoRegistry(TCHAR *buffer, DWORD tamanho);
BOOL  guardarNomePipeNoRegistry(const TCHAR *nomePipe);

/* Threads */
DWORD WINAPI threadUtilizador(LPVOID param);
DWORD WINAPI threadAlertas(LPVOID param);

/* Comandos do utilizador */
void  processarComando(const TCHAR *cmd, EstadoPlacar *estado);
void  executarLigar(EstadoPlacar *estado);
void  executarDesligar(EstadoPlacar *estado);

/* Comunicacao com o central */
BOOL  enviarMensagem_placar(HANDLE hPipe, const void *dados, DWORD tamanho);
BOOL  receberMensagem_placar(HANDLE hPipe, void *buffer, DWORD tamanhoMax, DWORD *bytesLidos);

/* Gestao de alertas */
void  receberAlerta(EstadoPlacar *estado);
void  apresentarAlerta(const MSG_ALERTA *alerta);
void  apresentarFimAlerta(void);
void  cancelarAlertaAtivo(EstadoPlacar *estado);

/* Utilitarios */
void  obterDataHoraAtual(TCHAR *buffer, DWORD tamanho);

#endif /* PLACAR_H */
