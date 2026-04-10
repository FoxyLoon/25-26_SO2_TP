#ifndef PLACAR_H
#define PLACAR_H

#include "common.h"

/* -----------------------------------------------------------------------
 * Constantes do programa placar
 * ----------------------------------------------------------------------- */

#define PLACAR_CMD_LIGAR    _T("ligar")
#define PLACAR_CMD_DESLIGAR _T("desligar")

#define PLACAR_MAX_CMD_LEN  64

/* -----------------------------------------------------------------------
 * Estado global do placar (partilhado entre threads)
 * ----------------------------------------------------------------------- */

typedef struct {
    DWORD  identificador;       /* atribuido pelo central (M1: gerado aleatorio) */
    BOOL   ligado;              /* TRUE apos comando "ligar"                      */
    BOOL   terminar;            /* TRUE quando se deve encerrar                   */
    TCHAR  nomePipe[MAX_PATH];  /* nome do named pipe lido do args ou registry    */
    MSG_ALERTA alertaAtivo;     /* alerta correntemente ativo                     */
    BOOL   temAlerta;           /* TRUE se existe um alerta ativo                 */

    /* Sincronizacao */
    HANDLE hEventNotificar;     /* evento manual-reset "notificar" (M1: local)    */
    HANDLE hWaitableTimer;      /* timer para a duracao do alerta                 */
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

/* Gestao de alertas */
void  receberAlerta(EstadoPlacar *estado);
void  apresentarAlerta(const MSG_ALERTA *alerta);
void  apresentarFimAlerta(void);
void  cancelarAlertaAtivo(EstadoPlacar *estado);

/* Utilitarios */
void  obterDataHoraAtual(TCHAR *buffer, DWORD tamanho);
DWORD gerarIdentificadorAleatorio(void); /* usado apenas na M1 */

#endif /* PLACAR_H */
