#include "placar.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* -----------------------------------------------------------------------
 * Estado global (acessivel pelas duas threads)
 * ----------------------------------------------------------------------- */

static EstadoPlacar g_estado;

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int _tmain(int argc, TCHAR* argv[])
{
    HANDLE hThreads[2];

    if (!inicializar(&g_estado, argc, argv)) {
        _tprintf(_T("Erro na inicializacao. A terminar.\n"));
        return 1;
    }

    _tprintf(_T("NamedPipe configurado = '%s'\n"), g_estado.nomePipe);
    _tprintf(_T("Escreva 'ajuda' para ver os comandos disponiveis.\n"));

    hThreads[0] = CreateThread(NULL, 0, threadUtilizador, &g_estado, 0, NULL);
    hThreads[1] = CreateThread(NULL, 0, threadAlertas, &g_estado, 0, NULL);

    if (hThreads[0] == NULL || hThreads[1] == NULL) {
        _tprintf(_T("Erro ao criar threads.\n"));
        libertar(&g_estado);
        return 1;
    }

    WaitForMultipleObjects(2, hThreads, TRUE, INFINITE);

    CloseHandle(hThreads[0]);
    CloseHandle(hThreads[1]);

    libertar(&g_estado);
    return 0;
}

/* -----------------------------------------------------------------------
 * Inicializacao e limpeza
 * ----------------------------------------------------------------------- */

BOOL inicializar(EstadoPlacar* estado, int argc, TCHAR* argv[])
{
    InitializeCriticalSection(&estado->cs);

    EnterCriticalSection(&estado->cs);
    estado->identificador = 0;
    estado->ligado = FALSE;
    estado->terminar = FALSE;
    estado->temAlerta = FALSE;
    estado->hPipe = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&estado->cs);

    if (argv[1] != NULL) {
        _tcsncpy_s(estado->nomePipe, MAX_PATH, argv[1], _TRUNCATE);
        if (!guardarNomePipeNoRegistry(estado->nomePipe)) {
            _tprintf(_T("Erro ao guardar nome do pipe no registry.\n"));
            return FALSE;
        }
    }
    else {
        if (!lerNomePipeDoRegistry(estado->nomePipe, MAX_PATH)) {
            _tprintf(_T("Erro: nome do pipe nao fornecido e nao encontrado no registry.\n"));
            return FALSE;
        }
    }

    _sntprintf_s(estado->nomePipeCompleto, MAX_PATH, _TRUNCATE,
        _T("%s%s"), PIPE_PREFIX, estado->nomePipe);

    estado->hPipeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (estado->hPipeEvent == NULL) {
        _tprintf(_T("Erro ao criar evento de overlapped: %lu\n"), GetLastError());
        return FALSE;
    }

    estado->hWaitableTimer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (estado->hWaitableTimer == NULL) {
        _tprintf(_T("Erro ao criar waitable timer: %lu\n"), GetLastError());
        CloseHandle(estado->hPipeEvent);
        return FALSE;
    }

    return TRUE;
}

BOOL libertar(EstadoPlacar* estado)
{
    if (estado->hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(estado->hPipe);
        estado->hPipe = INVALID_HANDLE_VALUE;
    }

    if (estado->hPipeEvent != NULL) CloseHandle(estado->hPipeEvent);
    if (estado->hWaitableTimer != NULL) CloseHandle(estado->hWaitableTimer);

    DeleteCriticalSection(&estado->cs);
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Registry
 * ----------------------------------------------------------------------- */

BOOL lerNomePipeDoRegistry(TCHAR* buffer, DWORD tamanho)
{
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao abrir chave do registry.\n"));
        return FALSE;
    }

    DWORD tipo = REG_SZ;
    DWORD tamanhoBytes = tamanho * sizeof(TCHAR);
    if (RegQueryValueEx(hKey, REGISTRY_NPIPE_VAL, NULL, &tipo,
        (LPBYTE)buffer, &tamanhoBytes) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao ler valor do registry.\n"));
        RegCloseKey(hKey);
        return FALSE;
    }

    RegCloseKey(hKey);
    return TRUE;
}

BOOL guardarNomePipeNoRegistry(const TCHAR* nomePipe)
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGISTRY_KEY, 0, NULL, 0,
        KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao criar/abrir chave do registry.\n"));
        return FALSE;
    }

    if (RegSetValueEx(hKey, REGISTRY_NPIPE_VAL, 0, REG_SZ,
        (const BYTE*)nomePipe,
        (DWORD)((_tcslen(nomePipe) + 1) * sizeof(TCHAR))) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao escrever valor no registry.\n"));
        RegCloseKey(hKey);
        return FALSE;
    }

    RegCloseKey(hKey);
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Thread do utilizador
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadUtilizador(LPVOID param)
{
    EstadoPlacar* estado = (EstadoPlacar*)param;
    TCHAR cmd[PLACAR_MAX_CMD_LEN];

    while (1) {
        _tprintf(_T("> "));
        if (_fgetts(cmd, PLACAR_MAX_CMD_LEN, stdin) == NULL)
            break;

        size_t len = _tcslen(cmd);
        if (len > 0 && cmd[len - 1] == _T('\n'))
            cmd[len - 1] = _T('\0');

        processarComando(cmd, estado);

        EnterCriticalSection(&estado->cs);
        BOOL terminar = estado->terminar;
        LeaveCriticalSection(&estado->cs);

        if (terminar) break;
    }

    return 0;
}

void processarComando(const TCHAR* cmd, EstadoPlacar* estado)
{
    // Ignore linhas vazias para nao dar erro na consola
    if (_tcslen(cmd) == 0) return;

    if (_tcscmp(cmd, PLACAR_CMD_LIGAR) == 0)
        executarLigar(estado);
    else if (_tcscmp(cmd, PLACAR_CMD_DESLIGAR) == 0)
        executarDesligar(estado);
    else if (_tcscmp(cmd, PLACAR_CMD_AJUDA) == 0)
        executarAjuda_placar();
    else
        _tprintf(_T("Comando invalido. Escreva 'ajuda' para listar os comandos.\n"));
}

void executarAjuda_placar(void)
{
    _tprintf(_T("=== Comandos do Placar ===\n"));
    _tprintf(_T("liga    : Estabelece a ligacao a plataforma central para registo\n"));
    _tprintf(_T("desliga : Remove o placar da plataforma e termina a execucao\n"));
    _tprintf(_T("ajuda   : Mostra esta mensagem de ajuda\n"));
    _tprintf(_T("==========================\n"));
}

void executarLigar(EstadoPlacar* estado)
{
    EnterCriticalSection(&estado->cs);
    if (estado->ligado) {
        _tprintf(_T("Placar ja esta ligado.\n"));
        LeaveCriticalSection(&estado->cs);
        return;
    }
    LeaveCriticalSection(&estado->cs);

    HANDLE hPipe;
    while (1) {
        hPipe = CreateFile(
            estado->nomePipeCompleto,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );

        if (hPipe != INVALID_HANDLE_VALUE)
            break;

        if (GetLastError() != ERROR_PIPE_BUSY) {
            _tprintf(_T("Erro ao abrir pipe (%d). Central nao iniciada?\n"), GetLastError());
            return;
        }

        if (!WaitNamedPipe(estado->nomePipeCompleto, 5000)) {
            _tprintf(_T("Pipe ocupado.\n"));
            return;
        }
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(hPipe, &mode, NULL, NULL)) {
        _tprintf(_T("Erro ao definir modo do pipe (%d)\n"), GetLastError());
        CloseHandle(hPipe);
        estado->hPipe = INVALID_HANDLE_VALUE;
        return;
    }

    MSG_CMD_PC msg;
    msg.tipo = MSG_TIPO_LIGAR;

    if (!enviarMensagem_placar(hPipe, &msg, sizeof(MSG_CMD_PC))) {
        _tprintf(_T("Erro ao enviar pedido de ligar.\n"));
        CloseHandle(hPipe);
        estado->hPipe = INVALID_HANDLE_VALUE;
        return;
    }

    MSG_ID resposta;
    DWORD  bytes;

    if (!receberMensagem_placar(hPipe, &resposta, sizeof(MSG_ID), &bytes)) {
        _tprintf(_T("Erro ao receber identificador.\n"));
        CloseHandle(hPipe);
        estado->hPipe = INVALID_HANDLE_VALUE;
        return;
    }

    if (resposta.tipo != MSG_TIPO_ID) {
        _tprintf(_T("Resposta invalida do central.\n"));
        CloseHandle(hPipe);
        estado->hPipe = INVALID_HANDLE_VALUE;
        return;
    }

    EnterCriticalSection(&estado->cs);
    estado->identificador = resposta.identificador;
    estado->ligado = TRUE;
    estado->hPipe = hPipe;
    LeaveCriticalSection(&estado->cs);

    _tprintf(_T("Ligado ao central. Identificador = %lu\n"), estado->identificador);
}

void executarDesligar(EstadoPlacar* estado)
{
    EnterCriticalSection(&estado->cs);
    if (!estado->ligado) {
        _tprintf(_T("Placar ja esta desligado.\n"));
        LeaveCriticalSection(&estado->cs);
        return;
    }
    LeaveCriticalSection(&estado->cs);

    cancelarAlertaAtivo(estado);

    MSG_CMD_PC msg;
    msg.tipo = MSG_TIPO_DESLIGAR;

    if (!enviarMensagem_placar(estado->hPipe, &msg, sizeof(MSG_CMD_PC))) {
        _tprintf(_T("Erro ao enviar pedido de desligar.\n"));
        return;
    }

    // REMOVIDO: receberMensagem_placar e alteração de estado.
    // A confirmação vai ser processada assincronamente pela threadAlertas.
}

/* -----------------------------------------------------------------------
 * Thread de alertas - Leitura Otimizada com overlapped I/O
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadAlertas(LPVOID param)
{
    EstadoPlacar* estado = (EstadoPlacar*)param;
    HANDLE     hWaitHandles[2];
    BYTE       buffer[PIPE_BUFFER_SIZE];
    OVERLAPPED ov;

    hWaitHandles[0] = estado->hPipeEvent;
    hWaitHandles[1] = estado->hWaitableTimer;

    /* CORRECAO: Variavel para controlar se ja foi emitido um pedido ReadFile
     * assincrono. Evita cancelar operacoes e mandar milhares de pedidos de leitura
     * sucessivos por causa do WAIT_TIMEOUT que reiniciava o loop em loop constante. */
    BOOL leituraPendente = FALSE;

    while (1) {
        EnterCriticalSection(&estado->cs);
        BOOL   terminar = estado->terminar;
        HANDLE hPipe = estado->hPipe;
        LeaveCriticalSection(&estado->cs);

        if (terminar) {
            // Se formos sair, cancelamos o I/O que la ficou pendente
            if (leituraPendente && hPipe != INVALID_HANDLE_VALUE) {
                CancelIo(hPipe);
            }
            break;
        }

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        // 3. Inicia leitura apenas se nao estiver nenhuma em curso
        if (!leituraPendente) {
            ZeroMemory(&ov, sizeof(OVERLAPPED));
            ov.hEvent = estado->hPipeEvent;

            BOOL readOk = ReadFile(
                hPipe,
                buffer,
                PIPE_BUFFER_SIZE,
                NULL,
                &ov
            );

            if (!readOk) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    leituraPendente = TRUE;
                }
                else {
                    _tprintf(_T("\nErro na leitura do pipe (Possivel desconexao da central): %lu\n> "), err);
                    break;
                }
            }
            else {
                // Sincrono: completou imediatamente, o evento e na mesma assinalado
                leituraPendente = TRUE;
            }
        }

        // 4. Espera por eventos de leitura ou timer com timeout regular para verificar estado
        DWORD wait = WaitForMultipleObjects(
            2,
            hWaitHandles,
            FALSE,
            500
        );

        DWORD bytesLidos = 0;

        switch (wait) {
        case WAIT_OBJECT_0: // PipeEvent disparou leitura completada
        {
            if (GetOverlappedResult(hPipe, &ov, &bytesLidos, FALSE)) {
                leituraPendente = FALSE; // Limpa para proxima iteracao
                receberAlerta(estado, buffer, bytesLidos);
            }
            else {
                leituraPendente = FALSE; // Se deu erro (e.g. pipe quebrado) tentara na proxima e fara quebra
            }
            break;
        }

        case WAIT_OBJECT_0 + 1: // hWaitableTimer disparou o fim do alerta
        {
            BOOL deveEnviar = FALSE;

            EnterCriticalSection(&estado->cs);
            if (estado->temAlerta) {
                apresentarFimAlerta();
                estado->temAlerta = FALSE;
                deveEnviar = TRUE;
            }
            LeaveCriticalSection(&estado->cs);

            if (deveEnviar) {
                MSG_CMD_PC msg;
                msg.tipo = MSG_TIPO_FIM_ALERTA;
                enviarMensagem_placar(estado->hPipe, &msg, sizeof(MSG_CMD_PC));
            }
            break;
        }

        case WAIT_TIMEOUT:
            // 7. Timeout apenas reiterar o loop.
            // CORRECAO: O CancelIo foi REMOVIDO daqui. Deixar a operacao do ReadFile prosseguir pelo tempo que for preciso.
            break;

        default:
            break;
        }
    }

    return 0;
}

void receberAlerta(EstadoPlacar* estado, BYTE* buffer, DWORD bytes)
{
    if (bytes == 0 || buffer == NULL)
        return;

    BYTE tipo = buffer[0];

    switch (tipo) {

    case MSG_TIPO_ALERTA:
    {
        MSG_ALERTA* alerta = (MSG_ALERTA*)buffer;

        EnterCriticalSection(&estado->cs);
        if (estado->temAlerta) {
            CancelWaitableTimer(estado->hWaitableTimer);
        }

        estado->alertaAtivo = *alerta;
        estado->temAlerta = TRUE;
        LeaveCriticalSection(&estado->cs);

        apresentarAlerta(&estado->alertaAtivo);

        LARGE_INTEGER liDueTime;
        liDueTime.QuadPart = -(LONGLONG)alerta->duracao * 10000000LL;

        SetWaitableTimer(estado->hWaitableTimer, &liDueTime, 0, NULL, NULL, FALSE);

        // 20260521 - envia a mesma estrutura MSG_ALERTA de volta
        enviarMensagem_placar(
            estado->hPipe,
            alerta,
            sizeof(MSG_ALERTA)
        );

        break;
    }

    case MSG_TIPO_CANCELAR:
    {
        MSG_CMD_CP* cancelar = (MSG_CMD_CP*)buffer;

        cancelarAlertaAtivo(estado);
        apresentarFimAlerta();

        // 20260521 - envia a mesma estrutura MSG_CMD_CP de volta
        enviarMensagem_placar(
            estado->hPipe,
            cancelar,
            sizeof(MSG_CMD_CP)
        );

        break;
    }

    case MSG_TIPO_DESLIGAR:
    {
        // A central confirmou o nosso pedido de desligar.
        _tprintf(_T("\nConfirmacao de desligar recebida. A terminar processo...\n> "));
        EnterCriticalSection(&estado->cs);
        estado->ligado = FALSE;
        estado->terminar = TRUE;
        LeaveCriticalSection(&estado->cs);
        break;
    }

    case MSG_TIPO_ENCERRAR:
    {
        _tprintf(_T("\nPlataforma central foi encerrada. A terminar processo.\n> "));
        EnterCriticalSection(&estado->cs);
        estado->terminar = TRUE;
        LeaveCriticalSection(&estado->cs);
        break;
    }

    default:
        _tprintf(_T("Mensagem do tipo desconhecido recebida: %d\n"), tipo);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Comunicacao com o central
 * ----------------------------------------------------------------------- */

BOOL enviarMensagem_placar(HANDLE hPipe, const void* dados, DWORD tamanho)
{
    if (hPipe == INVALID_HANDLE_VALUE || dados == NULL || tamanho == 0)
        return FALSE;

    HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (hEvent == NULL) return FALSE;

    OVERLAPPED ov = { 0 };
    ov.hEvent = hEvent;
    DWORD bytesEscritos = 0;

    BOOL ok = WriteFile(hPipe, dados, tamanho, NULL, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(hEvent);
        return FALSE;
    }

    ok = GetOverlappedResult(hPipe, &ov, &bytesEscritos, TRUE);
    CloseHandle(hEvent);

    return ok && (bytesEscritos == tamanho);
}

BOOL receberMensagem_placar(HANDLE hPipe, void* buffer, DWORD tamanhoMax, DWORD* bytesLidos)
{
    if (hPipe == INVALID_HANDLE_VALUE || buffer == NULL || bytesLidos == NULL)
        return FALSE;

    HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (hEvent == NULL) return FALSE;

    OVERLAPPED ov = { 0 };
    ov.hEvent = hEvent;
    *bytesLidos = 0;

    BOOL ok = ReadFile(hPipe, buffer, tamanhoMax, NULL, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(hEvent);
        return FALSE;
    }

    ok = GetOverlappedResult(hPipe, &ov, bytesLidos, TRUE);
    CloseHandle(hEvent);

    return ok;
}

/* -----------------------------------------------------------------------
 * Gestao de alertas
 * ----------------------------------------------------------------------- */

void apresentarAlerta(const MSG_ALERTA* alerta)
{
    TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
    _tprintf(_T("\n%s: '%s'\n> "), datahora, alerta->msg);
}

void apresentarFimAlerta(void)
{
    TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
    _tprintf(_T("\n%s: '---'\n> "), datahora);
}

void cancelarAlertaAtivo(EstadoPlacar* estado)
{
    if (estado == NULL)
        return;

    EnterCriticalSection(&estado->cs);
    if (estado->temAlerta) {
        CancelWaitableTimer(estado->hWaitableTimer);
        estado->temAlerta = FALSE;
        ZeroMemory(&estado->alertaAtivo, sizeof(MSG_ALERTA));
    }
    LeaveCriticalSection(&estado->cs);
}

/* -----------------------------------------------------------------------
 * Utilitarios
 * ----------------------------------------------------------------------- */

void obterDataHoraAtual(TCHAR* buffer, DWORD tamanho)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _sntprintf_s(buffer, tamanho, _TRUNCATE,
        _T("%02d/%02d/%04d (%02d:%02d:%02d)"),
        st.wDay, st.wMonth, st.wYear,
        st.wHour, st.wMinute, st.wSecond);
}