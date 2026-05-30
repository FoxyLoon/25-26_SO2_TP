#ifndef CENTRAL_H
#define CENTRAL_H

#include "../placar/common.h"

/* -----------------------------------------------------------------------
 * Constantes do programa central
 * ----------------------------------------------------------------------- */

#define CENTRAL_CMD_ALERTA      _T("alerta")
#define CENTRAL_CMD_CANCELAR    _T("cancelar")
#define CENTRAL_CMD_LISTAR      _T("listar")
#define CENTRAL_CMD_ENCERRAR    _T("encerrar")
#define CENTRAL_CMD_AJUDA       _T("ajuda")

#define CENTRAL_MAX_CMD_LEN     256

 /* Prefixo do named pipe do sistema */
#define PIPE_PREFIX             _T("\\\\.\\pipe\\")

/* Tamanho do buffer do named pipe */
#define PIPE_BUFFER_SIZE        512

/* -----------------------------------------------------------------------
 * Informacao de cada placar registado no central
 * ----------------------------------------------------------------------- */

typedef struct {
    DWORD  identificador;       /* ID unico atribuido pelo central           */
    HANDLE hPipe;               /* handle do named pipe para este placar     */
    HANDLE hThread;             /* thread dedicada a este placar             */
    BOOL   ativo;               /* TRUE enquanto o placar estiver ligado     */
    BOOL   temAlerta;           /* TRUE se existe um alerta ativo            */
    MSG_ALERTA alertaAtivo;     /* alerta correntemente ativo neste placar   */
} InfoPlacar;

/* -----------------------------------------------------------------------
 * Estado global do central
 * ----------------------------------------------------------------------- */

typedef struct {
    TCHAR      nomePipe[MAX_PATH];          /* nome base do pipe (sem prefixo)   */
    TCHAR      nomePipeCompleto[MAX_PATH];  /* nome completo: \\.\pipe\<nome>    */
    InfoPlacar placares[MAX_PLACARES];      /* array de placares registados      */
    int        numPlacares;                 /* numero de placares atualmente registados */
    DWORD      proximoId;                   /* proximo identificador a atribuir  */
    BOOL       terminar;                    /* TRUE quando se deve encerrar      */
    HANDLE     hPipeListener;               /* handle do pipe listener para encerramento */
    CRITICAL_SECTION cs;                    /* protege o estado partilhado       */

    /* M3: memoria partilhada + evento para o Monitor */
    HANDLE     hFileMapping;               /* handle do file mapping da SHM     */
    SHM_ALERTA* pShm;                      /* ponteiro para a vista mapeada     */
    HANDLE     hEventMonitor;              /* evento auto-reset para monitores  */
    TCHAR      nomeSHM[MAX_PATH];          /* nome do bloco SHM                 */
} EstadoCentral;

/* -----------------------------------------------------------------------
 * Argumento passado a cada threadPlacar
 * ----------------------------------------------------------------------- */

typedef struct {
    EstadoCentral* estado;
    int            indicePlacar; /* indice em estado->placares[] */
} ArgThreadPlacar;

/* -----------------------------------------------------------------------
 * Prototipos das funcoes
 * ----------------------------------------------------------------------- */

 /* Inicializacao e limpeza */
BOOL  inicializar_central(EstadoCentral* estado, int argc, TCHAR* argv[]);
BOOL  libertar_central(EstadoCentral* estado);

/* Threads */
DWORD WINAPI threadListener(LPVOID param);
DWORD WINAPI threadPlacar(LPVOID param);
DWORD WINAPI threadUtilizador_central(LPVOID param);

/* Processamento de comandos do administrador */
void  processarComando_central(const TCHAR* linha, EstadoCentral* estado);
void  executarAlerta(const TCHAR* msg, DWORD duracao, DWORD idPlacar, EstadoCentral* estado);
void  executarCancelar(DWORD idPlacar, EstadoCentral* estado);
void  executarListar(EstadoCentral* estado);
void  executarEncerrar(EstadoCentral* estado);
void  executarAjuda_central(void);

/* Comunicacao com os placares */
BOOL  enviarMensagem(HANDLE hPipe, const void* dados, DWORD tamanho);
BOOL  receberMensagem(HANDLE hPipe, void* buffer, DWORD tamanhoMax, DWORD* bytesLidos);

/* Gestao de placares */
int   encontrarPlacaPorId(EstadoCentral* estado, DWORD id);
int   encontrarSlotLivre(EstadoCentral* estado);
void  removerPlacar(EstadoCentral* estado, int indice);

/* Utilitarios */
void  obterDataHoraAtual_central(TCHAR* buffer, DWORD tamanho);

/* Memoria partilhada */
void  atualizarSHM(EstadoCentral* estado);

#endif /* CENTRAL_H */