#ifndef MONITOR_H
#define MONITOR_H

#include "../placar/common.h"
#include <windows.h>
#include <tchar.h>

/* Constantes */
#define APP_TITLE _T("Monitor de Alertas")
#define CLASSE_JANELA _T("MonitorAlertasClass")
#define TAMANHO_BUFFER 256

/* Estados e configuração padrão */
#define MAX_ALERTAS_PADRAO 5
#define NOME_SHM_PADRAO _T("SHM_PLACARES")
#define NOME_EVENTO_PADRAO EVENT_NOTIFICAR

/* IDs de recursos */
#define IDR_MENU_PRINCIPAL 101
#define IDD_DIALOG_CONFIG 102

#define ID_FICHEIRO_CONFIG 1001
#define ID_FICHEIRO_ACERCA 1002
#define ID_FICHEIRO_SAIR 1003

#define IDC_EDIT_MAX_ALERTAS 2001
#define IDC_EDIT_NOME_SHM 2002
#define IDC_EDIT_NOME_EVENTO 2003

/* Mensagens customizadas */
#define WM_APP_ATUALIZAR (WM_APP + 1)
#define WM_APP_ENCERRAR (WM_APP + 2)

/* Estado global do Monitor */
typedef struct {
    int    maxAlertas;
    int    paginaAtual;
    TCHAR  nomeSHM[MAX_PATH];
    TCHAR  nomeEvento[MAX_PATH];

    HANDLE hFileMapping;
    SHM_ALERTA* pShm;
    HANDLE hEvento;
    HANDLE hThreadEscuta;

    HWND   hwndMain;
    BOOL   terminar;

    CRITICAL_SECTION cs;
} EstadoMonitor;

/* Protótipos */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK DlgProcConfig(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI threadEscuta(LPVOID param);

void abrirRecursos(EstadoMonitor* estado);
void fecharRecursos(EstadoMonitor* estado);
void desenharAlertas(HWND hwnd, EstadoMonitor* estado, HDC hdc, RECT* rc);
int contarAlertasAtivos(EstadoMonitor* estado);

#endif /* MONITOR_H */