# Plano de Implementação - M3 (Programa Monitor)

## Contexto da M3

A M3 exige:

1. **Programa Monitor** (novo) - interface gráfica Win32
2. **Alterações ao Central** - memória partilhada + sincronização com o Monitor

### Problema: SHM_ALERTA Incorreta

A estrutura `SHM_ALERTA` no `common.h` atual está **errada** para M3.

**Estrutura especificada no enunciado:**
```c
typedef struct {
    struct {
        DWORD identificador;
        DWORD duracao;
        TCHAR msg[140];
    } placar[20];
    BOOL desligar;
} SHM_ALERTA;
```

**Estrutura atual no `common.h`** (incorreta):
```c
typedef struct {
    DWORD identificador;
    DWORD duracao;
    TCHAR msg[MAX_MSG_LEN];
    BOOL  desligar;
} SHM_ALERTA;
```

Esta precisa ser corrigida antes de avançar.

---

## Divisão de Trabalho

### PESSOA A - Alterações ao Central + Correção de SHM_ALERTA

**Objetivo:** o Central passa a manter e expor um bloco de memória partilhada atualizado, sinalizando qualquer alteração via evento nomeado.

#### Tarefa A1 - Corrigir `common.h`

Substituir a estrutura `SHM_ALERTA` pela correcta:

```c
typedef struct {
    struct {
        DWORD identificador;
        DWORD duracao;
        TCHAR msg[MAX_MSG_LEN];
    } placar[MAX_PLACARES];
    BOOL desligar;
} SHM_ALERTA;
```

#### Tarefa A2 - Adicionar campos ao `EstadoCentral` em `central.h`

Adicionar no final da struct:

```c
HANDLE hFileMapping;        // handle do file mapping
SHM_ALERTA* pShm;          // ponteiro para a vista mapeada
HANDLE hEventMonitor;      // evento auto-reset para notificar monitores
TCHAR nomeSHM[MAX_PATH];   // nome do bloco SHM (ex: "SHM_PLACARES")
```

#### Tarefa A3 - Actualizar `inicializar_central()`

No final da inicialização, adicionar:

```c
// Criar memória partilhada para o(s) monitor(s)
_tcscpy_s(estado->nomeSHM, MAX_PATH, _T("SHM_PLACARES"));

estado->hFileMapping = CreateFileMapping(
    INVALID_HANDLE_VALUE,
    NULL,
    PAGE_READWRITE,
    0,
    sizeof(SHM_ALERTA),
    estado->nomeSHM
);

if (estado->hFileMapping == NULL) {
    _tprintf(_T("Erro ao criar file mapping: %lu\n"), GetLastError());
    return FALSE;
}

estado->pShm = (SHM_ALERTA*)MapViewOfFile(
    estado->hFileMapping,
    FILE_MAP_ALL_ACCESS,
    0, 0, 0
);

if (estado->pShm == NULL) {
    _tprintf(_T("Erro ao mapear vista: %lu\n"), GetLastError());
    CloseHandle(estado->hFileMapping);
    return FALSE;
}

ZeroMemory(estado->pShm, sizeof(SHM_ALERTA));

// Criar evento nomeado para sincronização
estado->hEventMonitor = CreateEvent(
    NULL,
    FALSE,  // auto-reset
    FALSE,  // initial state: não assinalado
    EVENT_NOTIFICAR
);

if (estado->hEventMonitor == NULL) {
    _tprintf(_T("Erro ao criar evento monitor: %lu\n"), GetLastError());
    UnmapViewOfFile(estado->pShm);
    CloseHandle(estado->hFileMapping);
    return FALSE;
}
```

#### Tarefa A4 - Implementar função auxiliar `atualizarSHM()`

Adicionar a seguinte função em `central.c`:

```c
void atualizarSHM(EstadoCentral* estado)
{
    if (estado == NULL || estado->pShm == NULL)
        return;

    EnterCriticalSection(&estado->cs);

    // Limpar a SHM
    ZeroMemory(estado->pShm, sizeof(SHM_ALERTA));

    // Copiar dados dos placares ativos com alertas
    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->placares[i].ativo && estado->placares[i].temAlerta) {
            estado->pShm->placar[i].identificador = 
                estado->placares[i].identificador;
            estado->pShm->placar[i].duracao = 
                estado->placares[i].alertaAtivo.duracao;
            _tcscpy_s(
                estado->pShm->placar[i].msg,
                MAX_MSG_LEN,
                estado->placares[i].alertaAtivo.msg
            );
        }
    }

    LeaveCriticalSection(&estado->cs);

    // Sinalizar monitores
    SetEvent(estado->hEventMonitor);
}
```

#### Tarefa A5 - Chamar `atualizarSHM()` nos pontos correctos

Adicionar chamadas nos seguintes locais (sempre depois de alterar o estado):

| Local | Função | Quando |
|-------|--------|--------|
| `executarAlerta()` | Depois de `estado->placares[i].temAlerta = TRUE` | Novo alerta enviado |
| `executarCancelar()` | Depois de `estado->placares[idx].temAlerta = FALSE` | Alerta cancelado |
| `removerPlacar()` | No final, depois de limpar o slot | Placar desconectado |
| `threadPlacar()` - caso `MSG_TIPO_FIM_ALERTA` | Depois de `estado->placares[idx].temAlerta = FALSE` | Alerta terminou no placar |
| `executarEncerrar()` | Depois de enviar para todos, setar `estado->pShm->desligar = TRUE` | Antes de `SetEvent` |

Exemplo de como chamar:

```c
EnterCriticalSection(&estado->cs);
estado->placares[i].temAlerta = TRUE;
estado->placares[i].alertaAtivo = alerta;
LeaveCriticalSection(&estado->cs);

atualizarSHM(estado);  // <-- Aqui
```

#### Tarefa A6 - Actualizar `libertar_central()`

Adicionar no final:

```c
if (estado->pShm != NULL) {
    UnmapViewOfFile(estado->pShm);
    estado->pShm = NULL;
}

if (estado->hFileMapping != INVALID_HANDLE_VALUE) {
    CloseHandle(estado->hFileMapping);
    estado->hFileMapping = INVALID_HANDLE_VALUE;
}

if (estado->hEventMonitor != NULL) {
    CloseHandle(estado->hEventMonitor);
    estado->hEventMonitor = NULL;
}
```

#### Tarefa A7 - Adicionar protótipo em `central.h`

```c
void atualizarSHM(EstadoCentral* estado);
```

---

### PESSOA B - Programa Monitor (Win32 GUI)

**Objetivo:** janela Win32 que lê a SHM, reage ao evento nomeado sem polling, e apresenta os alertas ativos com navegação por páginas.

#### Tarefa B1 - Criar o projeto Visual Studio

1. Novo projeto: "Windows Desktop Application"
2. Nomes sugeridos:
   - `monitor.c` - lógica principal
   - `monitor.h` - constantes e estado
   - `monitor.rc` - recursos
   - `resource.h` - IDs dos recursos

#### Tarefa B2 - Ficheiro `monitor.h`

```c
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
```

#### Tarefa B3 - Ficheiro `monitor.c` (parte 1: main e setup)

```c
#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>

static EstadoMonitor g_estado;

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPTSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc = {0};
    HWND hwnd;
    MSG msg;

    /* Inicializar estado */
    InitializeCriticalSection(&g_estado.cs);
    g_estado.maxAlertas = MAX_ALERTAS_PADRAO;
    g_estado.paginaAtual = 0;
    _tcscpy_s(g_estado.nomeSHM, MAX_PATH, NOME_SHM_PADRAO);
    _tcscpy_s(g_estado.nomeEvento, MAX_PATH, NOME_EVENTO_PADRAO);
    g_estado.terminar = FALSE;

    /* Registar classe de janela */
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASSE_JANELA;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU_PRINCIPAL);
    wc.style = CS_VREDRAW | CS_HREDRAW;

    if (!RegisterClass(&wc)) {
        _tprintf(_T("Erro ao registar classe de janela.\n"));
        return 1;
    }

    /* Criar janela principal */
    hwnd = CreateWindow(
        CLASSE_JANELA,
        APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        _tprintf(_T("Erro ao criar janela.\n"));
        return 1;
    }

    g_estado.hwndMain = hwnd;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    /* Message loop */
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteCriticalSection(&g_estado.cs);
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CREATE:
    {
        abrirRecursos(&g_estado);
        g_estado.hwndMain = hwnd;
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        desenharAlertas(hwnd, &g_estado, hdc, &rc);
        
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_APP_ATUALIZAR:
    {
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_APP_ENCERRAR:
    {
        MessageBox(hwnd, _T("A plataforma central foi encerrada."),
                   APP_TITLE, MB_OK | MB_INFORMATION);
        PostQuitMessage(0);
        return 0;
    }

    case WM_KEYDOWN:
    {
        int totalAlertas = contarAlertasAtivos(&g_estado);
        int totalPaginas = (totalAlertas + g_estado.maxAlertas - 1) / g_estado.maxAlertas;

        if (wParam == VK_NEXT) {  // Page Down
            if (g_estado.paginaAtual < totalPaginas - 1) {
                g_estado.paginaAtual++;
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wParam == VK_PRIOR) {  // Page Up
            if (g_estado.paginaAtual > 0) {
                g_estado.paginaAtual--;
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id) {
        case ID_FICHEIRO_CONFIG:
            DialogBox(GetModuleHandle(NULL),
                      MAKEINTRESOURCE(IDD_DIALOG_CONFIG),
                      hwnd, DlgProcConfig);
            return 0;

        case ID_FICHEIRO_ACERCA:
            MessageBox(hwnd,
                       _T("Monitor de Alertas\n\n")
                       _T("Grupo: [Nome do Aluno 1], [Número]\n")
                       _T("       [Nome do Aluno 2], [Número]\n\n")
                       _T("Sistemas Operativos 2 - 2025/26"),
                       _T("Acerca do Monitor"),
                       MB_OK | MB_INFORMATION);
            return 0;

        case ID_FICHEIRO_SAIR:
            PostQuitMessage(0);
            return 0;
        }
        return 0;
    }

    case WM_DESTROY:
    {
        fecharRecursos(&g_estado);
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}
```

#### Tarefa B4 - Ficheiro `monitor.c` (parte 2: gestão de recursos)

```c
void abrirRecursos(EstadoMonitor* estado)
{
    if (estado == NULL)
        return;

    /* Abrir file mapping */
    estado->hFileMapping = OpenFileMapping(
        FILE_MAP_READ,
        FALSE,
        estado->nomeSHM
    );

    if (estado->hFileMapping == NULL) {
        _tprintf(_T("Erro ao abrir file mapping '%s': %lu\n"),
                 estado->nomeSHM, GetLastError());
        MessageBox(estado->hwndMain,
                   _T("Não foi possível abrir a memória partilhada.\n")
                   _T("Certifique-se que o Central está a correr."),
                   APP_TITLE, MB_OK | MB_ERROR);
        return;
    }

    /* Mapear vista */
    estado->pShm = (SHM_ALERTA*)MapViewOfFile(
        estado->hFileMapping,
        FILE_MAP_READ,
        0, 0, 0
    );

    if (estado->pShm == NULL) {
        _tprintf(_T("Erro ao mapear vista: %lu\n"), GetLastError());
        CloseHandle(estado->hFileMapping);
        estado->hFileMapping = NULL;
        return;
    }

    /* Abrir evento de notificação */
    estado->hEvento = OpenEvent(
        SYNCHRONIZE,
        FALSE,
        estado->nomeEvento
    );

    if (estado->hEvento == NULL) {
        _tprintf(_T("Erro ao abrir evento '%s': %lu\n"),
                 estado->nomeEvento, GetLastError());
        UnmapViewOfFile(estado->pShm);
        CloseHandle(estado->hFileMapping);
        estado->pShm = NULL;
        estado->hFileMapping = NULL;
        return;
    }

    /* Lançar thread de escuta */
    estado->hThreadEscuta = CreateThread(
        NULL, 0, threadEscuta, estado, 0, NULL
    );

    if (estado->hThreadEscuta == NULL) {
        _tprintf(_T("Erro ao criar thread de escuta: %lu\n"), GetLastError());
        CloseHandle(estado->hEvento);
        UnmapViewOfFile(estado->pShm);
        CloseHandle(estado->hFileMapping);
        estado->hEvento = NULL;
        estado->pShm = NULL;
        estado->hFileMapping = NULL;
        return;
    }

    _tprintf(_T("Monitor conectado à plataforma.\n"));
}

void fecharRecursos(EstadoMonitor* estado)
{
    if (estado == NULL)
        return;

    EnterCriticalSection(&estado->cs);
    estado->terminar = TRUE;
    LeaveCriticalSection(&estado->cs);

    if (estado->hThreadEscuta != NULL) {
        WaitForSingleObject(estado->hThreadEscuta, 5000);
        CloseHandle(estado->hThreadEscuta);
        estado->hThreadEscuta = NULL;
    }

    if (estado->hEvento != NULL) {
        CloseHandle(estado->hEvento);
        estado->hEvento = NULL;
    }

    if (estado->pShm != NULL) {
        UnmapViewOfFile(estado->pShm);
        estado->pShm = NULL;
    }

    if (estado->hFileMapping != NULL) {
        CloseHandle(estado->hFileMapping);
        estado->hFileMapping = NULL;
    }

    _tprintf(_T("Monitor desconectado.\n"));
}

DWORD WINAPI threadEscuta(LPVOID param)
{
    EstadoMonitor* estado = (EstadoMonitor*)param;

    while (1) {
        EnterCriticalSection(&estado->cs);
        BOOL terminar = estado->terminar;
        HANDLE hEvento = estado->hEvento;
        LeaveCriticalSection(&estado->cs);

        if (terminar || hEvento == NULL)
            break;

        DWORD wait = WaitForSingleObject(hEvento, INFINITE);

        if (wait == WAIT_OBJECT_0) {
            EnterCriticalSection(&estado->cs);
            BOOL desligar = (estado->pShm != NULL) ? 
                            estado->pShm->desligar : FALSE;
            LeaveCriticalSection(&estado->cs);

            if (desligar) {
                PostMessage(estado->hwndMain, WM_APP_ENCERRAR, 0, 0);
                break;
            }
            else {
                PostMessage(estado->hwndMain, WM_APP_ATUALIZAR, 0, 0);
            }
        }
    }

    return 0;
}

int contarAlertasAtivos(EstadoMonitor* estado)
{
    int count = 0;

    if (estado == NULL || estado->pShm == NULL)
        return 0;

    EnterCriticalSection(&estado->cs);

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->pShm->placar[i].identificador != 0) {
            count++;
        }
    }

    LeaveCriticalSection(&estado->cs);

    return count;
}

void desenharAlertas(HWND hwnd, EstadoMonitor* estado, HDC hdc, RECT* rc)
{
    if (estado == NULL || estado->pShm == NULL) {
        TextOut(hdc, 10, 10,
                _T("A aguardar ligação ao Central..."), 33);
        return;
    }

    EnterCriticalSection(&estado->cs);

    int totalAlertas = contarAlertasAtivos(estado);

    if (totalAlertas == 0) {
        TextOut(hdc, 10, 10,
                _T("Sem alertas ativos."), 19);
        LeaveCriticalSection(&estado->cs);
        return;
    }

    int indiceInicio = estado->paginaAtual * estado->maxAlertas;
    int totalPaginas = (totalAlertas + estado->maxAlertas - 1) / estado->maxAlertas;

    int y = 10;
    int count = 0;
    int mostrados = 0;

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->pShm->placar[i].identificador != 0) {
            if (count >= indiceInicio && mostrados < estado->maxAlertas) {
                TCHAR linha[256];
                _sntprintf_s(linha, 256, _TRUNCATE,
                    _T("ID: %lu | %s | Duração: %lu s"),
                    estado->pShm->placar[i].identificador,
                    estado->pShm->placar[i].msg,
                    estado->pShm->placar[i].duracao
                );

                TextOut(hdc, 10, y, linha, (int)_tcslen(linha));
                y += 25;
                mostrados++;
            }
            count++;
        }
    }

    /* Mostrar indicador de página */
    if (totalPaginas > 1) {
        TCHAR rodape[64];
        _sntprintf_s(rodape, 64, _TRUNCATE,
            _T("Página %d/%d"),
            estado->paginaAtual + 1, totalPaginas
        );

        RECT rcRodape = *rc;
        rcRodape.top = rcRodape.bottom - 30;
        DrawText(hdc, rodape, (int)_tcslen(rodape), &rcRodape,
                 DT_CENTER | DT_VCENTER);
    }

    LeaveCriticalSection(&estado->cs);
}
```

#### Tarefa B5 - Ficheiro `monitor.c` (parte 3: dialog de configuração)

```c
INT_PTR CALLBACK DlgProcConfig(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_INITDIALOG:
    {
        /* Preencher campos com valores actuais */
        TCHAR szTemp[16];
        _sntprintf_s(szTemp, 16, _TRUNCATE, _T("%d"), g_estado.maxAlertas);
        SetDlgItemText(hDlg, IDC_EDIT_MAX_ALERTAS, szTemp);
        SetDlgItemText(hDlg, IDC_EDIT_NOME_SHM, g_estado.nomeSHM);
        SetDlgItemText(hDlg, IDC_EDIT_NOME_EVENTO, g_estado.nomeEvento);

        return TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        
        if (id == IDOK) {
            TCHAR szTemp[MAX_PATH];
            int maxAlertas;

            /* Ler novo máximo de alertas */
            GetDlgItemText(hDlg, IDC_EDIT_MAX_ALERTAS, szTemp, 16);
            maxAlertas = _tcstol(szTemp, NULL, 10);

            if (maxAlertas < 1 || maxAlertas > 20) {
                MessageBox(hDlg,
                    _T("Máximo de alertas deve estar entre 1 e 20."),
                    _T("Erro"), MB_OK | MB_ERROR);
                return TRUE;
            }

            /* Ler novos nomes */
            GetDlgItemText(hDlg, IDC_EDIT_NOME_SHM, szTemp, MAX_PATH);
            if (_tcslen(szTemp) == 0) {
                MessageBox(hDlg,
                    _T("Nome SHM não pode estar vazio."),
                    _T("Erro"), MB_OK | MB_ERROR);
                return TRUE;
            }

            /* Actualizar estado */
            EnterCriticalSection(&g_estado.cs);
            g_estado.maxAlertas = maxAlertas;
            _tcscpy_s(g_estado.nomeSHM, MAX_PATH, szTemp);

            GetDlgItemText(hDlg, IDC_EDIT_NOME_EVENTO, szTemp, MAX_PATH);
            if (_tcslen(szTemp) > 0) {
                _tcscpy_s(g_estado.nomeEvento, MAX_PATH, szTemp);
            }
            LeaveCriticalSection(&g_estado.cs);

            /* Se nomes mudaram, reconectar */
            fecharRecursos(&g_estado);
            Sleep(100);  // Dar tempo a fechar
            abrirRecursos(&g_estado);

            g_estado.paginaAtual = 0;
            InvalidateRect(g_estado.hwndMain, NULL, TRUE);

            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }

        return FALSE;
    }

    default:
        return FALSE;
    }
}
```

#### Tarefa B6 - Ficheiro `monitor.rc` (Recursos)

```
#include "resource.h"

/////////////////////////////////////////////////////////////////////////////
// Menu
/////////////////////////////////////////////////////////////////////////////

IDR_MENU_PRINCIPAL MENU
BEGIN
    POPUP "Ficheiro", 0, MFT_STRING
    BEGIN
        MENUITEM "Configuração",        ID_FICHEIRO_CONFIG,  MFT_STRING
        MENUITEM "Acerca",              ID_FICHEIRO_ACERCA,  MFT_STRING
        MENUITEM SEPARATOR
        MENUITEM "Sair",                ID_FICHEIRO_SAIR,    MFT_STRING
    END
END

/////////////////////////////////////////////////////////////////////////////
// Dialog
/////////////////////////////////////////////////////////////////////////////

IDD_DIALOG_CONFIG DIALOGEX 0, 0, 300, 150
STYLE DS_SHELLFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION "Configuração do Monitor"
FONT 8, "MS Shell Dlg", 400, 0, 0x1
BEGIN
    LTEXT "Máximo de alertas a mostrar:", IDC_STATIC, 10, 10, 150, 14
    EDITTEXT IDC_EDIT_MAX_ALERTAS, 160, 10, 50, 14, ES_LEFT | ES_AUTOHSCROLL | ES_NUMBER
    
    LTEXT "Nome da SHM:", IDC_STATIC, 10, 30, 150, 14
    EDITTEXT IDC_EDIT_NOME_SHM, 160, 30, 130, 14, ES_LEFT | ES_AUTOHSCROLL
    
    LTEXT "Nome do Evento:", IDC_STATIC, 10, 50, 150, 14
    EDITTEXT IDC_EDIT_NOME_EVENTO, 160, 50, 130, 14, ES_LEFT | ES_AUTOHSCROLL
    
    DEFPUSHBUTTON "OK", IDOK, 150, 120, 50, 14
    PUSHBUTTON "Cancelar", IDCANCEL, 240, 120, 50, 14
END
```

#### Tarefa B7 - Ficheiro `resource.h`

```c
#ifndef IDC_STATIC
#define IDC_STATIC -1
#endif

#define IDR_MENU_PRINCIPAL 101
#define IDD_DIALOG_CONFIG 102

#define ID_FICHEIRO_CONFIG 1001
#define ID_FICHEIRO_ACERCA 1002
#define ID_FICHEIRO_SAIR 1003

#define IDC_EDIT_MAX_ALERTAS 2001
#define IDC_EDIT_NOME_SHM 2002
#define IDC_EDIT_NOME_EVENTO 2003
```

---

## Checklist de Implementação

### Pessoa A (Central)
- [ ] A1: Corrigir `SHM_ALERTA` em `common.h`
- [ ] A2: Adicionar campos a `EstadoCentral`
- [ ] A3: Inicializar recursos em `inicializar_central()`
- [ ] A4: Implementar `atualizarSHM()`
- [ ] A5: Chamar `atualizarSHM()` nos 5+ pontos correctos
- [ ] A6: Limpar em `libertar_central()`
- [ ] A7: Adicionar protótipo a `central.h`
- [ ] Testar que central cria SHM e evento sem erros

### Pessoa B (Monitor)
- [ ] B1: Criar projeto Visual Studio
- [ ] B2: Implementar `monitor.h`
- [ ] B3: Implementar `monitor.c` (main + WndProc)
- [ ] B4: Implementar `abrirRecursos()`, `fecharRecursos()`, `threadEscuta()`
- [ ] B5: Implementar `desenharAlertas()`, `contarAlertasAtivos()`
- [ ] B6: Criar `monitor.rc` com menu e dialogs
- [ ] B7: Criar `resource.h`
- [ ] Testar navegação Page Up/Down
- [ ] Testar menu Configuração com novo SHM/evento
- [ ] Testar menu Acerca

---

## Ponto de Sincronização Crítico

**Antes de B começar qualquer teste**, A deve ter terminado A1 (a correção de `SHM_ALERTA`).

Recomendação: ambos escolham os nomes por defeito das estruturas IPC:
- **Nome SHM:** `"SHM_PLACARES"`
- **Nome Evento:** `"notificar"` (já definido em `common.h` como `EVENT_NOTIFICAR`)

O resto pode ser desenvolvido em paralelo.

---

## Notas Importantes

1. **Memory mapping é read-only no Monitor.** A central é a única que escreve.

2. **Sincronização crítica:** usar `EnterCriticalSection` / `LeaveCriticalSection` em cada acesso a `pShm` e estado partilhado.

3. **A thread `threadEscuta` não toca na UI directamente.** Comunica via `PostMessage(WM_APP_*)`.

4. **Page Up/Down:** verificar limites de página antes de ajustar `paginaAtual`.

5. **Encerramento gracioso:** quando central sinala `desligar=TRUE`, monitor mostra mensagem e fecha.

6. **Sem polling:** a thread espera no evento (bloqueada), não num loop com Sleep().

---

## Estrutura Final do Projecto

```
SistemaAlertas/
├── placar/
│   ├── common.h
│   ├── placar.h
│   └── placar.c
│
├── central/
│   ├── central.h
│   └── central.c
│
└── monitor/
    ├── monitor.h
    ├── monitor.c
    ├── monitor.rc
    ├── resource.h
    └── monitor.vcxproj
```
