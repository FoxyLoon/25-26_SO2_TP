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

int _tmain(int argc, TCHAR *argv[])
{
    HANDLE hThreads[2];

    if (!inicializar(&g_estado, argc, argv)) {
        _tprintf(_T("Erro na inicializacao. A terminar.\n"));
        return 1;
    }

    _tprintf(_T("NamedPipe = '%s'\n"), g_estado.nomePipe);

    hThreads[0] = CreateThread(NULL, 0, threadUtilizador, &g_estado, 0, NULL);
    hThreads[1] = CreateThread(NULL, 0, threadAlertas,    &g_estado, 0, NULL);

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

BOOL inicializar(EstadoPlacar *estado, int argc, TCHAR *argv[])
{
    InitializeCriticalSection(&estado->cs);

    EnterCriticalSection(&estado->cs);
    estado->identificador = 0;
    estado->ligado        = FALSE;
    estado->terminar      = FALSE;
    estado->temAlerta     = FALSE;
    estado->hPipe         = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&estado->cs);

    /* Obter o nome do pipe (args ou registry) - identico a M1 */
    if (argv[1] != NULL) {
        _tcsncpy_s(estado->nomePipe, MAX_PATH, argv[1], _TRUNCATE);
        if (!guardarNomePipeNoRegistry(estado->nomePipe)) {
            _tprintf(_T("Erro ao guardar nome do pipe no registry.\n"));
            return FALSE;
        }
    } else {
        if (!lerNomePipeDoRegistry(estado->nomePipe, MAX_PATH)) {
            _tprintf(_T("Erro: nome do pipe nao fornecido e nao encontrado no registry.\n"));
            return FALSE;
        }
    }

    /* Construir o nome completo do pipe */
    _sntprintf_s(estado->nomePipeCompleto, MAX_PATH, _TRUNCATE,
                 _T("%s%s"), PIPE_PREFIX, estado->nomePipe);

    /*
     * TODO (M2):
     * - Criar o evento de overlapped para leitura do pipe:
     *     estado->hPipeEvent = CreateEvent(NULL, TRUE, FALSE, NULL)
     *   (manual-reset, inicialmente nao sinalizado, sem nome)
     * - Verificar erro e retornar FALSE se falhar
     *
     * Nota: a ligacao ao named pipe em si (CreateFile) e feita em
     * executarLigar(), pois so deve acontecer quando o utilizador
     * escreve o comando "liga".
     */

    estado->hWaitableTimer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (estado->hWaitableTimer == NULL) {
        _tprintf(_T("Erro ao criar waitable timer: %lu\n"), GetLastError());
        return FALSE;
    }

    return TRUE;
}

BOOL libertar(EstadoPlacar *estado)
{
    /*
     * TODO (M2 - adicionar ao que ja existia):
     * - Se estado->hPipe != INVALID_HANDLE_VALUE: CloseHandle(estado->hPipe)
     * - Fechar estado->hPipeEvent com CloseHandle()
     */

    if (estado->hPipe != INVALID_HANDLE_VALUE) {
        /* TODO: CloseHandle(estado->hPipe); */
    }

    /* TODO: CloseHandle(estado->hPipeEvent); */

    if (!CloseHandle(estado->hWaitableTimer)) {
        _tprintf(_T("Erro ao fechar waitable timer: %lu\n"), GetLastError());
        return FALSE;
    }

    DeleteCriticalSection(&estado->cs);
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Registry (sem alteracoes face a M1)
 * ----------------------------------------------------------------------- */

BOOL lerNomePipeDoRegistry(TCHAR *buffer, DWORD tamanho)
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

BOOL guardarNomePipeNoRegistry(const TCHAR *nomePipe)
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGISTRY_KEY, 0, NULL, 0,
                       KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao criar/abrir chave do registry.\n"));
        return FALSE;
    }

    if (RegSetValueEx(hKey, REGISTRY_NPIPE_VAL, 0, REG_SZ,
                      (const BYTE *)nomePipe,
                      (DWORD)((_tcslen(nomePipe) + 1) * sizeof(TCHAR))) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao escrever valor no registry.\n"));
        RegCloseKey(hKey);
        return FALSE;
    }

    RegCloseKey(hKey);
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Thread do utilizador (sem alteracoes na logica de leitura)
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadUtilizador(LPVOID param)
{
    EstadoPlacar *estado = (EstadoPlacar *)param;
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

void processarComando(const TCHAR *cmd, EstadoPlacar *estado)
{
    if (_tcscmp(cmd, PLACAR_CMD_LIGAR) == 0)
        executarLigar(estado);
    else if (_tcscmp(cmd, PLACAR_CMD_DESLIGAR) == 0)
        executarDesligar(estado);
    else
        _tprintf(_T("Comando invalido: '%s'\n"), cmd);
}

void executarLigar(EstadoPlacar *estado)
{
    EnterCriticalSection(&estado->cs);
    if (estado->ligado) {
        _tprintf(_T("Placar ja esta ligado.\n"));
        LeaveCriticalSection(&estado->cs);
        return;
    }
    LeaveCriticalSection(&estado->cs);

    /*
     * TODO (M2):
     * - Ligar ao named pipe do central com CreateFile():
     *     estado->nomePipeCompleto, acesso GENERIC_READ | GENERIC_WRITE,
     *     sem partilha (0), NULL, OPEN_EXISTING,
     *     FILE_FLAG_OVERLAPPED (necessario para leitura assincrona),
     *     NULL
     *   Se GetLastError() == ERROR_PIPE_BUSY, usar WaitNamedPipe() e repetir.
     *   Se falhar por outra razao, imprimir erro e retornar.
     *   Guardar o handle em estado->hPipe.
     *
     * - Definir o modo do pipe para mensagem com SetNamedPipeHandleState():
     *     PIPE_READMODE_MESSAGE
     *
     * - Enviar MSG_CMD_PC com tipo = MSG_TIPO_LIGAR ao central:
     *     enviarMensagem_placar(estado->hPipe, &msg, sizeof(MSG_CMD_PC))
     *
     * - Aguardar resposta MSG_ID do central:
     *     receberMensagem_placar(estado->hPipe, &resposta, sizeof(MSG_ID), &bytes)
     *
     * - Verificar que resposta.tipo == MSG_TIPO_ID
     *
     * - EnterCriticalSection
     * - estado->identificador = resposta.identificador
     * - estado->ligado = TRUE
     * - LeaveCriticalSection
     *
     * - Mostrar: "Identificador = %lu\n"
     */
}

void executarDesligar(EstadoPlacar *estado)
{
    EnterCriticalSection(&estado->cs);
    if (!estado->ligado) {
        _tprintf(_T("Placar ja esta desligado.\n"));
        LeaveCriticalSection(&estado->cs);
        return;
    }
    LeaveCriticalSection(&estado->cs);

    /*
     * TODO (M2):
     * - Cancelar qualquer alerta ativo com cancelarAlertaAtivo()
     *
     * - Enviar MSG_CMD_PC com tipo = MSG_TIPO_DESLIGAR ao central:
     *     enviarMensagem_placar(estado->hPipe, &msg, sizeof(MSG_CMD_PC))
     *
     * - Aguardar confirmacao MSG_CMD_PC do central:
     *     receberMensagem_placar(estado->hPipe, &conf, sizeof(MSG_CMD_PC), &bytes)
     *   (o central ecoa uma MSG_CMD_PC com o mesmo tipo)
     *
     * - EnterCriticalSection
     * - estado->ligado  = FALSE
     * - estado->terminar = TRUE
     * - LeaveCriticalSection
     */
}

/* -----------------------------------------------------------------------
 * Thread de alertas - M2: leitura do named pipe com overlapped I/O
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadAlertas(LPVOID param)
{
    EstadoPlacar *estado = (EstadoPlacar *)param;

    /*
     * Nesta thread fazemos duas coisas em paralelo:
     *   [0] Leitura assincrona do named pipe (overlapped)
     *   [1] Waitable timer para a duracao do alerta ativo
     *
     * Fluxo geral:
     *
     * 1. Verificar estado->terminar no inicio de cada iteracao.
     *
     * 2. Se o pipe ainda nao estiver aberto (estado->hPipe == INVALID_HANDLE_VALUE),
     *    aguardar brevemente (Sleep(100)) e repetir - o utilizador ainda nao
     *    executou o comando "liga".
     *
     * 3. Iniciar uma leitura assincrona com ReadFile() usando uma estrutura
     *    OVERLAPPED com hEvent = estado->hPipeEvent.
     *    - Se ReadFile() retornar FALSE e GetLastError() != ERROR_IO_PENDING:
     *      o pipe foi fechado ou ocorreu erro - sair do loop.
     *
     * 4. Construir o array de handles para WaitForMultipleObjects:
     *      hWaitHandles[0] = estado->hPipeEvent   (leitura do pipe concluida)
     *      hWaitHandles[1] = estado->hWaitableTimer
     *    Aguardar com timeout de 500ms para re-verificar estado->terminar.
     *
     * 5. Se [0] (hPipeEvent) disparou:
     *      - Usar GetOverlappedResult() para saber quantos bytes foram lidos
     *      - ResetEvent(estado->hPipeEvent)
     *      - Chamar receberAlerta() passando o buffer lido
     *        (receberAlerta trata dos diferentes tipos de mensagem)
     *
     * 6. Se [1] (hWaitableTimer) disparou:
     *      - EnterCriticalSection
     *      - Se estado->temAlerta:
     *          apresentarFimAlerta()
     *          estado->temAlerta = FALSE
     *          Enviar MSG_CMD_PC tipo=MSG_TIPO_FIM_ALERTA ao central:
     *              enviarMensagem_placar(estado->hPipe, &msg, sizeof(MSG_CMD_PC))
     *      - LeaveCriticalSection
     *
     * 7. Se WAIT_TIMEOUT: apenas reiterar o loop.
     */

    HANDLE hWaitHandles[2];
    BYTE   buffer[PIPE_BUFFER_SIZE];
    OVERLAPPED ov;

    hWaitHandles[0] = estado->hPipeEvent;
    hWaitHandles[1] = estado->hWaitableTimer;

    while (1) {
        EnterCriticalSection(&estado->cs);
        BOOL terminar = estado->terminar;
        LeaveCriticalSection(&estado->cs);
        if (terminar) break;

        /* TODO: implementar o fluxo descrito acima */
        Sleep(100);
    }

    return 0;
}

void receberAlerta(EstadoPlacar *estado)
{
    /*
     * TODO (M2):
     * Esta funcao e chamada pela threadAlertas quando chegou uma mensagem
     * do pipe. O buffer ja foi lido - passar como parametro ou usar
     * uma variavel partilhada adequada.
     *
     * Verificar o campo 'tipo' da mensagem recebida:
     *
     * Caso MSG_TIPO_ALERTA (4):
     *   - Fazer cast para MSG_ALERTA*
     *   - Enviar a mesma estrutura de volta ao central como confirmacao:
     *       enviarMensagem_placar(estado->hPipe, &alerta, sizeof(MSG_ALERTA))
     *   - EnterCriticalSection
     *   - Se ja havia alerta ativo: CancelWaitableTimer()
     *   - estado->alertaAtivo = *alerta; estado->temAlerta = TRUE
     *   - LeaveCriticalSection
     *   - apresentarAlerta(&estado->alertaAtivo)
     *   - Armar o waitable timer com a duracao do alerta (valor negativo,
     *     unidades de 100 nanosegundos):
     *       liDueTime.QuadPart = -(LONGLONG)alerta->duracao * 10000000LL
     *       SetWaitableTimer(estado->hWaitableTimer, &liDueTime, 0, NULL, NULL, FALSE)
     *
     * Caso MSG_TIPO_CANCELAR (5):
     *   - Fazer cast para MSG_CMD_CP*
     *   - Enviar a mesma estrutura de volta como confirmacao
     *   - cancelarAlertaAtivo(estado) - cancela o timer e limpa o estado
     *   - apresentarFimAlerta()
     *
     * Caso MSG_TIPO_ENCERRAR (6):
     *   - EnterCriticalSection
     *   - estado->terminar = TRUE
     *   - LeaveCriticalSection
     *   - (nao e necessario enviar resposta)
     */
}

/* -----------------------------------------------------------------------
 * Comunicacao com o central
 * ----------------------------------------------------------------------- */

BOOL enviarMensagem_placar(HANDLE hPipe, const void *dados, DWORD tamanho)
{
    /*
     * TODO:
     * - WriteFile() para escrever 'tamanho' bytes no pipe
     * - Verificar bytesEscritos == tamanho
     * - Retornar TRUE em sucesso, FALSE em erro
     */
    return FALSE;
}

BOOL receberMensagem_placar(HANDLE hPipe, void *buffer, DWORD tamanhoMax, DWORD *bytesLidos)
{
    /*
     * TODO:
     * - ReadFile() sincrono (sem overlapped) para leituras pontuais
     *   feitas a partir das funcoes executarLigar / executarDesligar,
     *   onde e aceitavel bloquear brevemente.
     * - Preencher *bytesLidos
     * - Retornar TRUE em sucesso, FALSE em erro
     *
     * Nota: a leitura continua em threadAlertas usa overlapped e e
     * tratada diretamente nessa thread.
     */
    return FALSE;
}

/* -----------------------------------------------------------------------
 * Gestao de alertas (sem alteracoes face a M1)
 * ----------------------------------------------------------------------- */

void apresentarAlerta(const MSG_ALERTA *alerta)
{
    TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
    _tprintf(_T("%s: '%s'\n"), datahora, alerta->msg);
}

void apresentarFimAlerta(void)
{
    TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
    _tprintf(_T("%s: '---'\n"), datahora);
}

void cancelarAlertaAtivo(EstadoPlacar *estado)
{
    /* Nota: assume que a critical section esta adquirida pelo chamador
     * OU que nao ha conflito. Ajustar conforme necessario. */
    if (estado->temAlerta) {
        CancelWaitableTimer(estado->hWaitableTimer);
        estado->temAlerta = FALSE;
        _tprintf(_T("Alerta cancelado.\n"));
    }
}

/* -----------------------------------------------------------------------
 * Utilitarios (sem alteracoes face a M1)
 * ----------------------------------------------------------------------- */

void obterDataHoraAtual(TCHAR *buffer, DWORD tamanho)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _sntprintf_s(buffer, tamanho, _TRUNCATE,
                 _T("%02d/%02d/%04d (%02d:%02d:%02d)"),
                 st.wDay, st.wMonth, st.wYear,
                 st.wHour, st.wMinute, st.wSecond);
}
