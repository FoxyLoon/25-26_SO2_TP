#ifndef COMMON_H
#define COMMON_H

#include <windows.h>
#include <tchar.h>

/* -----------------------------------------------------------------------
 * Constantes globais da plataforma
 * ----------------------------------------------------------------------- */

#define MAX_PLACARES        20
#define MAX_MSG_LEN         140

#define REGISTRY_KEY        _T("Software\\TrabSO2")
#define REGISTRY_NPIPE_VAL  _T("NPIPE")

#define EVENT_NOTIFICAR     _T("notificar")

 /* Tipos das mensagens - Placar -> Central */
#define MSG_TIPO_LIGAR      1	// pedido de ligacao (o placar esta a ligar e quer receber um ID unico do central)
#define MSG_TIPO_DESLIGAR   2	// pedido de desligar (o placar esta a desligar)
#define MSG_TIPO_FIM_ALERTA 3	// notifica que o alerta terminou

/* Tipos das mensagens - Central -> Placar */
#define MSG_TIPO_ID         3	// resposta ao ligar, atribui o ID unico do placar
#define MSG_TIPO_ALERTA     4	// nova mensagem de alerta para o placar
#define MSG_TIPO_CANCELAR   5	// cancelar alerta ativo (o placar deve parar de mostrar o alerta)
#define MSG_TIPO_ENCERRAR   6	// encerrar a plataforma (o placar deve terminar imediatamente)

/* -----------------------------------------------------------------------
 * Estruturas de comunicacao - Named Pipes (Placar <-> Central)
 * ----------------------------------------------------------------------- */

 /* Placar -> Central: pedidos/atualizacoes */
typedef struct {
	BYTE tipo; /* MSG_TIPO_LIGAR | MSG_TIPO_DESLIGAR | MSG_TIPO_FIM_ALERTA */
} MSG_CMD_PC;

/* Central -> Placar: atribuicao de identificador */
typedef struct {
	BYTE  tipo;          /* MSG_TIPO_ID */
	DWORD identificador;
} MSG_ID;

/* Central -> Placar: novo alerta */
typedef struct {
	BYTE  tipo;          /* MSG_TIPO_ALERTA */
	TCHAR msg[MAX_MSG_LEN];
	DWORD duracao;       /* segundos */
} MSG_ALERTA;

/* Central -> Placar: cancelar alerta ou encerrar plataforma */
typedef struct {
	BYTE tipo; /* MSG_TIPO_CANCELAR | MSG_TIPO_ENCERRAR */
} MSG_CMD_CP;

/* -----------------------------------------------------------------------
 * Estrutura de comunicacao - Memoria Partilhada (Central <-> Monitor)
 * ----------------------------------------------------------------------- */

typedef struct {
	DWORD identificador;
	DWORD duracao;
	TCHAR msg[MAX_MSG_LEN];
	BOOL  desligar;
} SHM_ALERTA;

#endif /* COMMON_H */
