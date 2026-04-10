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

    /* 1. Inicializar estado, ler nome do pipe (args ou registry) */
    if (!inicializar(&g_estado, argc, argv)) {
        _tprintf(_T("Erro na inicializacao. A terminar.\n"));
        return 1;
    }

    _tprintf(_T("NamedPipe = '%s'\n"), g_estado.nomePipe);

    /* 2. Criar as duas threads */
    hThreads[0] = CreateThread(NULL, 0, threadUtilizador, &g_estado, 0, NULL);
    hThreads[1] = CreateThread(NULL, 0, threadAlertas,    &g_estado, 0, NULL);

    if (hThreads[0] == NULL || hThreads[1] == NULL) {
        _tprintf(_T("Erro ao criar threads.\n"));
        libertar(&g_estado);
        return 1;
    }

    /* 3. Aguardar que ambas as threads terminem */
    WaitForMultipleObjects(2, hThreads, TRUE, INFINITE);

    CloseHandle(hThreads[0]);
    CloseHandle(hThreads[1]);

    /* 4. Libertar todos os recursos */
    libertar(&g_estado);

    return 0;
}

/* -----------------------------------------------------------------------
 * Inicializacao e limpeza
 * ----------------------------------------------------------------------- */

BOOL inicializar(EstadoPlacar *estado, int argc, TCHAR *argv[])
{
    /*
     * TODO:
     * - Inicializar srand com o tempo atual (para gerarIdentificadorAleatorio)
     * - Inicializar a CRITICAL_SECTION
     * - Definir os valores iniciais do estado (ligado=FALSE, terminar=FALSE, etc.)
     * - Se argv[1] existir, copiar para estado->nomePipe e guardar no registry
     * - Caso contrario, tentar ler do registry com lerNomePipeDoRegistry()
     * - Se nenhum dos dois tiver o nome do pipe, imprimir erro e retornar FALSE
     * - Criar o evento manual-reset "EVENT_NOTIFICAR" com CreateEvent()
     * - Criar o waitable timer com CreateWaitableTimer()
     */

	srand((unsigned int)time(NULL)); /* para gerarIdentificadorAleatorio() */

    InitializeCriticalSection(&estado->cs);

	EnterCriticalSection(&estado->cs);
	estado->identificador = 0;
	estado->ligado = FALSE;
	estado->terminar = FALSE;
	estado->temAlerta = FALSE;
	LeaveCriticalSection(&estado->cs);

    if(argv[1] != NULL) {
        _tcsncpy(estado->nomePipe, argv[1], MAX_PATH);
        estado->nomePipe[MAX_PATH-1] = _T('\0'); /* garantir terminacao */
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

    estado->hEventNotificar = CreateEvent(
        NULL,
        TRUE,
        FALSE,
        EVENT_NOTIFICAR
    );
    if (estado->hEventNotificar == NULL) {
        _tprintf(_T("Erro ao criar evento: %lu\n"), GetLastError());
        return FALSE;
    }

    estado->hWaitableTimer = CreateWaitableTimer(
        NULL,
        TRUE,
        NULL
    );
    if (estado->hWaitableTimer == NULL) {
        _tprintf(_T("Erro ao criar waitable timer: %lu\n"), GetLastError());
        CloseHandle(estado->hEventNotificar);
        return FALSE;
    }
    
    
    return TRUE;
}

BOOL libertar(EstadoPlacar *estado)
{
    /*
     * TODO:
     * - Fechar hEventNotificar com CloseHandle()
     * - Fechar hWaitableTimer com CloseHandle()
     * - Destruir a CRITICAL_SECTION com DeleteCriticalSection()
     */

    if (!CloseHandle(estado->hEventNotificar)){
		_tprintf(_T("Erro ao fechar evento: %lu\n"), GetLastError());
		return FALSE;
    }

    if (!CloseHandle(estado->hWaitableTimer)){
		_tprintf(_T("Erro ao fechar waitable timer: %lu\n"), GetLastError());
		return FALSE;
    }

    DeleteCriticalSection(&estado->cs);

    return TRUE;
}

/* -----------------------------------------------------------------------
 * Registry
 * ----------------------------------------------------------------------- */

BOOL lerNomePipeDoRegistry(TCHAR *buffer, DWORD tamanho)
{
    /*
     * TODO:
     * - Abrir HKEY_CURRENT_USER\Software\TrabSO2 com RegOpenKeyEx()
     * - Ler o valor NPIPE (REG_SZ) com RegQueryValueEx()
     * - Copiar o resultado para buffer
     * - Fechar a chave com RegCloseKey()
     * - Retornar TRUE em caso de sucesso, FALSE se a chave/valor nao existir
     */

    if (!RegOpenKey(HKEY_CURRENT_USER, _T("Software\\TrabSO2"), NULL)) {
		_tprintf(_T("Erro ao abrir chave do registry.\n"));
		return FALSE;
    }

	if (!RegQueryValueEx(HKEY_CURRENT_USER, _T("NPIPE"), NULL, NULL, (LPBYTE)buffer, &tamanho)) {
        _tprintf(_T("Erro ao ler valor do registry.\n"));
        RegCloseKey(HKEY_CURRENT_USER);
        return FALSE;
	}

	if (!RegCloseKey(HKEY_CURRENT_USER)) {
        _tprintf(_T("Erro ao fechar chave do registry.\n"));
        return FALSE;
	}

    return TRUE;
}

BOOL guardarNomePipeNoRegistry(const TCHAR *nomePipe)
{
    /*
     * TODO:
     * - Abrir ou criar HKEY_CURRENT_USER\Software\TrabSO2 com RegCreateKeyEx()
     * - Escrever o valor NPIPE (REG_SZ) com RegSetValueEx()
     * - Fechar a chave com RegCloseKey()
     * - Retornar TRUE em caso de sucesso
     */

	HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, _T("Software\\TrabSO2"), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao criar/abrir chave do registry.\n"));
        return FALSE;
	}

    if (RegSetValueEx(hKey, _T("NPIPE"), 0, REG_SZ, (const BYTE *)nomePipe, (DWORD)((_tcslen(nomePipe) + 1) * sizeof(TCHAR))) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao escrever valor no registry.\n"));
        RegCloseKey(hKey);
        return FALSE;
	}

    if (RegCloseKey(hKey) != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao fechar chave do registry.\n"));
		return FALSE;
	}

    return TRUE;
}

/* -----------------------------------------------------------------------
 * Thread do utilizador (comandos da consola)
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadUtilizador(LPVOID param)
{
    EstadoPlacar *estado = (EstadoPlacar *)param;
    TCHAR cmd[PLACAR_MAX_CMD_LEN];

    /*
     * Loop principal:
     * - Ler uma linha da consola com _fgetts() ou equivalente
     * - Remover o '\n' final
     * - Chamar processarComando()
     * - Sair do loop quando estado->terminar for TRUE
     */

    while (1)
    {
        _tprintf(_T("> ")); /* prompt */
        if (_fgetts(cmd, PLACAR_MAX_CMD_LEN, stdin) == NULL) {
            _tprintf(_T("Erro ao ler comando. A terminar.\n"));
            break;
        }
        
        /* Remover '\n' final */
        size_t len = _tcslen(cmd);
        if (len > 0 && cmd[len - 1] == _T('\n')) {
            cmd[len - 1] = _T('\0');
        }
        
        processarComando(cmd, estado);
        
        EnterCriticalSection(&estado->cs);
        if (estado->terminar) {
            LeaveCriticalSection(&estado->cs);
            break;
        }
        LeaveCriticalSection(&estado->cs);
    }

    return 0;
}

void processarComando(const TCHAR *cmd, EstadoPlacar *estado)
{
    /*
     * TODO:
     * - Comparar cmd com PLACAR_CMD_LIGAR  -> executarLigar()
     * - Comparar cmd com PLACAR_CMD_DESLIGAR -> executarDesligar()
     * - Caso contrario, mostrar mensagem de comando invalido
     */

    if (_tcscmp(cmd, PLACAR_CMD_LIGAR) == 0) {
		executarLigar(estado);
	}
	else if (_tcscmp(cmd, PLACAR_CMD_DESLIGAR) == 0) {
        executarDesligar(estado);
    }
    else {
        _tprintf(_T("Comando invalido: '%s'\n"), cmd);
    }
}

void executarLigar(EstadoPlacar *estado)
{
    /*
     * TODO (M1):
     * - Verificar se ja esta ligado; se sim, avisar e retornar
     * - Gerar identificador aleatorio com gerarIdentificadorAleatorio()
     * - Guardar em estado->identificador
     * - Definir estado->ligado = TRUE
     * - Mostrar: "Identificador = <id>"
     *
     * (M2): substituir por comunicacao real com o central via named pipe
     */

	EnterCriticalSection(&estado->cs);
    if (estado->ligado) {
        _tprintf(_T("Placar ja esta ligado.\n"));
    }
    else {
        estado->identificador = gerarIdentificadorAleatorio();
        estado->ligado = TRUE;
        _tprintf(_T("Identificador = %lu\n"), estado->identificador);
	}
	LeaveCriticalSection(&estado->cs);

}

void executarDesligar(EstadoPlacar *estado)
{
    /*
     * TODO:
     * - Verificar se esta ligado; se nao, avisar e retornar
     * - Cancelar qualquer alerta ativo
     * - Definir estado->ligado = FALSE e estado->terminar = TRUE
     *   para que ambas as threads terminem de forma controlada
     *
     * (M2): antes de terminar, enviar MSG_CMD_PC com tipo=MSG_TIPO_DESLIGAR
     *       ao central via named pipe
     */

	EnterCriticalSection(&estado->cs);
	if (!estado->ligado) {
        _tprintf(_T("Placar ja esta desligado.\n"));
    }
    else {
        cancelarAlertaAtivo(estado);
        estado->ligado = FALSE;
        estado->terminar = TRUE;
    }
}

/* -----------------------------------------------------------------------
 * Thread de alertas (simulacao de recepcao M1)
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadAlertas(LPVOID param)
{
    EstadoPlacar *estado = (EstadoPlacar *)param;

    /*
     * Loop principal:
     * - Aguardar num dos seguintes handles (WaitForMultipleObjects):
     *     [0] estado->hEventNotificar  (novo alerta disponivel no registry)
     *     [1] estado->hWaitableTimer   (duracao do alerta expirou)
     *   Usar INFINITE como timeout
     *
     * - Se [0] disparou (hEventNotificar):
     *     - Repor o evento a manual-reset: ResetEvent()
     *     - Chamar receberAlerta() para ler do registry e mostrar na consola
     *     - Armar o waitable timer para a duracao do alerta
     *
     * - Se [1] disparou (hWaitableTimer):
     *     - O alerta expirou
     *     - Chamar apresentarFimAlerta()
     *     - Limpar estado->temAlerta
     *
     * - Sair do loop quando estado->terminar for TRUE
     */

    return 0;
}

void receberAlerta(EstadoPlacar *estado)
{
    /*
     * TODO (M1):
     * - Abrir HKEY_CURRENT_USER\Software\TrabSO2 com RegOpenKeyEx()
     * - Ler o valor cujo nome e o nome do pipe (estado->nomePipe),
     *   tipo BINARY, para uma variavel do tipo MSG_ALERTA
     * - Fechar a chave com RegCloseKey()
     * - Proteger o acesso a estado com EnterCriticalSection / LeaveCriticalSection
     * - Guardar o alerta em estado->alertaAtivo e definir estado->temAlerta = TRUE
     * - Chamar apresentarAlerta()
     *
     * (M2): substituir por leitura real do named pipe
     */
}

/* -----------------------------------------------------------------------
 * Gestao de alertas
 * ----------------------------------------------------------------------- */

void apresentarAlerta(const MSG_ALERTA *alerta)
{
    TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
    /*
     * TODO:
     * - Mostrar: "DD/MM/AAAA (HH:MM:SS): '<msg>'"
     * Exemplo:   "23/03/2026 (13:51:10): 'A7 Norte - Acidente Km 34 - Lento'"
     */

    TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
    _tprintf(_T("%s: '%s'\n"), datahora, alerta->msg);
}

void apresentarFimAlerta(void)
{
    TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
    /*
     * TODO:
     * - Mostrar: "DD/MM/AAAA (HH:MM:SS): '---'"
     */

	TCHAR datahora[32];
    obterDataHoraAtual(datahora, 32);
	_tprintf(_T("%s: '---'\n"), datahora);

}

void cancelarAlertaAtivo(EstadoPlacar *estado)
{
    /*
     * TODO:
     * - Se estado->temAlerta for FALSE, nao fazer nada
     * - Cancelar o waitable timer com CancelWaitableTimer()
     * - Definir estado->temAlerta = FALSE
     * - (Opcional M1) Mostrar mensagem de alerta cancelado
     */

	EnterCriticalSection(&estado->cs);
    if (estado->temAlerta) {
        CancelWaitableTimer(estado->hWaitableTimer);
        estado->temAlerta = FALSE;
        _tprintf(_T("Alerta cancelado.\n"));
	}
	LeaveCriticalSection(&estado->cs);

}

/* -----------------------------------------------------------------------
 * Utilitarios
 * ----------------------------------------------------------------------- */

void obterDataHoraAtual(TCHAR *buffer, DWORD tamanho)
{
    /*
     * TODO:
     * - Obter hora local com GetLocalTime() -> SYSTEMTIME
     * - Formatar em "DD/MM/AAAA (HH:MM:SS)" com _sntprintf()
     */

    SYSTEMTIME st;
    GetLocalTime(&st);
    _sntprintf(buffer, tamanho, _T("%02d/%02d/%04d (%02d:%02d:%02d)"),
        st.wDay, st.wMonth, st.wYear,
        st.wHour, st.wMinute, st.wSecond);

}

DWORD gerarIdentificadorAleatorio(void)
{
    /*
     * TODO (M1 apenas):
     * - Retornar um valor aleatorio entre 1 e 100 com rand()
     * - (O identificador real sera atribuido pelo central na M2)
     */

    return rand() % 100 + 1;
}
