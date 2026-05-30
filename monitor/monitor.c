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
    g_estado.hFileMapping = NULL;
    g_estado.pShm = NULL;
    g_estado.hEvento = NULL;
    g_estado.hThreadEscuta = NULL;

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
        MessageBox(NULL, _T("Erro ao registar classe de janela."),
                   APP_TITLE, MB_OK | MB_ICONERROR);
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
        MessageBox(NULL, _T("Erro ao criar janela."),
                   APP_TITLE, MB_OK | MB_ICONERROR);
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
        g_estado.hwndMain = hwnd;
        abrirRecursos(&g_estado);
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
        /* Mostrar aviso e destruir a janela de forma limpa */
        MessageBox(hwnd, _T("A plataforma central foi encerrada."),
                   APP_TITLE, MB_OK | MB_ICONINFORMATION);
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_KEYDOWN:
    {
        int totalAlertas = contarAlertasAtivos(&g_estado);
        int totalPaginas = (totalAlertas + g_estado.maxAlertas - 1) / g_estado.maxAlertas;
        if (totalPaginas < 1) totalPaginas = 1;

        if (wParam == VK_NEXT) {  /* Page Down */
            if (g_estado.paginaAtual < totalPaginas - 1) {
                g_estado.paginaAtual++;
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wParam == VK_PRIOR) {  /* Page Up */
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
                       _T("Grupo: [Nome do Aluno 1], [N\xfamero]\n")
                       _T("       [Nome do Aluno 2], [N\xfamero]\n\n")
                       _T("Sistemas Operativos 2 - 2025/26"),
                       _T("Acerca do Monitor"),
                       MB_OK | MB_ICONINFORMATION);
            return 0;

        case ID_FICHEIRO_SAIR:
            /* Destruir a janela -> WM_DESTROY faz a limpeza */
            DestroyWindow(hwnd);
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
        MessageBox(estado->hwndMain,
            _T("N\xe3o foi poss\xedvel abrir a mem\xf3ria partilhada.\n")
            _T("Certifique-se que o Central est\xe1 a correr."),
            APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    /* Mapear vista */
    estado->pShm = (SHM_ALERTA*)MapViewOfFile(
        estado->hFileMapping,
        FILE_MAP_READ,
        0, 0, 0
    );

    if (estado->pShm == NULL) {
        CloseHandle(estado->hFileMapping);
        estado->hFileMapping = NULL;
        MessageBox(estado->hwndMain,
            _T("Erro ao mapear a mem\xf3ria partilhada."),
            APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    /* Abrir evento de notificacao */
    estado->hEvento = OpenEvent(
        SYNCHRONIZE,
        FALSE,
        estado->nomeEvento
    );

    if (estado->hEvento == NULL) {
        UnmapViewOfFile(estado->pShm);
        CloseHandle(estado->hFileMapping);
        estado->pShm = NULL;
        estado->hFileMapping = NULL;
        MessageBox(estado->hwndMain,
            _T("Erro ao abrir o evento de notifica\xe7\xe3o."),
            APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    /* Lancar thread de escuta */
    estado->terminar = FALSE;
    estado->hThreadEscuta = CreateThread(
        NULL, 0, threadEscuta, estado, 0, NULL
    );

    if (estado->hThreadEscuta == NULL) {
        CloseHandle(estado->hEvento);
        UnmapViewOfFile(estado->pShm);
        CloseHandle(estado->hFileMapping);
        estado->hEvento = NULL;
        estado->pShm = NULL;
        estado->hFileMapping = NULL;
        MessageBox(estado->hwndMain,
            _T("Erro ao criar thread de escuta."),
            APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
}

void fecharRecursos(EstadoMonitor* estado)
{
    if (estado == NULL)
        return;

    /* Sinalizar a thread de escuta para terminar */
    EnterCriticalSection(&estado->cs);
    estado->terminar = TRUE;
    LeaveCriticalSection(&estado->cs);

    /* Desbloquear a thread de escuta que pode estar em WaitForSingleObject */
    if (estado->hEvento != NULL) {
        SetEvent(estado->hEvento);
    }

    /* Aguardar que a thread de escuta termine (maximo 3 segundos) */
    if (estado->hThreadEscuta != NULL) {
        WaitForSingleObject(estado->hThreadEscuta, 3000);
        CloseHandle(estado->hThreadEscuta);
        estado->hThreadEscuta = NULL;
    }

    /* Fechar evento */
    if (estado->hEvento != NULL) {
        CloseHandle(estado->hEvento);
        estado->hEvento = NULL;
    }

    /* Desfazer o mapeamento da SHM */
    if (estado->pShm != NULL) {
        UnmapViewOfFile(estado->pShm);
        estado->pShm = NULL;
    }

    /* Fechar o file mapping */
    if (estado->hFileMapping != NULL) {
        CloseHandle(estado->hFileMapping);
        estado->hFileMapping = NULL;
    }
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

        if (wait != WAIT_OBJECT_0)
            break;

        /* Reler o estado terminar apos acordar */
        EnterCriticalSection(&estado->cs);
        terminar = estado->terminar;
        BOOL desligar = (estado->pShm != NULL) ? estado->pShm->desligar : FALSE;
        LeaveCriticalSection(&estado->cs);

        if (terminar)
            break;

        if (desligar) {
            PostMessage(estado->hwndMain, WM_APP_ENCERRAR, 0, 0);
            break;
        }
        else {
            PostMessage(estado->hwndMain, WM_APP_ATUALIZAR, 0, 0);
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
        TCHAR semConn[] = _T("A aguardar liga\xe7\xe3o ao Central...");
        TextOut(hdc, 10, 10, semConn, (int)_tcslen(semConn));
        return;
    }

    EnterCriticalSection(&estado->cs);

    /* Contar alertas ativos dentro da CS (sem chamar contarAlertasAtivos para evitar re-entrancia) */
    int totalAlertas = 0;
    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->pShm->placar[i].identificador != 0)
            totalAlertas++;
    }

    if (totalAlertas == 0) {
        TCHAR semAlertas[] = _T("Sem alertas ativos.");
        TextOut(hdc, 10, 10, semAlertas, (int)_tcslen(semAlertas));
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
                    _T("ID: %lu | %s | Dura\xe7\xe3o: %lu s"),
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

    /* Mostrar indicador de pagina se necessario */
    if (totalPaginas > 1) {
        TCHAR rodape[64];
        _sntprintf_s(rodape, 64, _TRUNCATE,
            _T("P\xe1gina %d/%d  (Page Up / Page Down para navegar)"),
            estado->paginaAtual + 1, totalPaginas
        );

        RECT rcRodape = *rc;
        rcRodape.top = rcRodape.bottom - 30;
        DrawText(hdc, rodape, (int)_tcslen(rodape), &rcRodape,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    LeaveCriticalSection(&estado->cs);
}


INT_PTR CALLBACK DlgProcConfig(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_INITDIALOG:
    {
        /* Preencher campos com valores actuais */
        TCHAR szTemp[16];
        _sntprintf_s(szTemp, 16, _TRUNCATE, _T("%d"), g_estado.maxAlertas);
        SetDlgItemText(hDlg, IDC_EDIT_MAX_ALERTAS, szTemp);
        SetDlgItemText(hDlg, IDC_EDIT_NOME_SHM,    g_estado.nomeSHM);
        SetDlgItemText(hDlg, IDC_EDIT_NOME_EVENTO,  g_estado.nomeEvento);

        return TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id == IDOK) {
            TCHAR szTemp[MAX_PATH];
            int maxAlertas;

            /* Ler novo maximo de alertas */
            GetDlgItemText(hDlg, IDC_EDIT_MAX_ALERTAS, szTemp, 16);
            maxAlertas = _tcstol(szTemp, NULL, 10);

            if (maxAlertas < 1 || maxAlertas > 20) {
                MessageBox(hDlg,
                    _T("M\xe1ximo de alertas deve estar entre 1 e 20."),
                    _T("Erro"), MB_OK | MB_ICONERROR);
                return TRUE;
            }

            /* Ler novo nome SHM */
            GetDlgItemText(hDlg, IDC_EDIT_NOME_SHM, szTemp, MAX_PATH);
            if (_tcslen(szTemp) == 0) {
                MessageBox(hDlg,
                    _T("Nome SHM n\xe3o pode estar vazio."),
                    _T("Erro"), MB_OK | MB_ICONERROR);
                return TRUE;
            }

            /* Actualizar estado global */
            EnterCriticalSection(&g_estado.cs);
            g_estado.maxAlertas = maxAlertas;
            _tcscpy_s(g_estado.nomeSHM, MAX_PATH, szTemp);

            GetDlgItemText(hDlg, IDC_EDIT_NOME_EVENTO, szTemp, MAX_PATH);
            if (_tcslen(szTemp) > 0) {
                _tcscpy_s(g_estado.nomeEvento, MAX_PATH, szTemp);
            }
            LeaveCriticalSection(&g_estado.cs);

            /* Se os nomes dos recursos mudaram, reconectar */
            fecharRecursos(&g_estado);
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
