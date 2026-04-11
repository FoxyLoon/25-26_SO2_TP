// =============================================
//			Placar de Alertas | placar.c
// =============================================

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>

// =============================================
// 
// Definições

// Valores do Regedit
#define REG_PATH		TEXT("Software\\TrabSO2")
#define REG_VALUE		TEXT("NPIPE")
#define EVENT_NAME		TEXT("notificar")
#define COMMAND_SIZE	100

HANDLE hEvento;
HANDLE hTimer;
BOOL running = TRUE;
TCHAR namedPipe[256];

// =============================================
// 
// Estrutura para mensagens de alerta

typedef struct {
	BYTE tipo; // 4 – novo
	TCHAR msg[140];
	DWORD duracao;
} MSG_ALERTA;

// =============================================
// 
// NPIPE - Validação e criação no registry

// Verifica o NPIPE no registry
BOOL verifica_NPIPE_No_Registry(TCHAR* namedPipe, DWORD tamanhoNamedPipe) {
	HKEY CHAVE = NULL;
	// Tenta abrir a chave do registry. Se não tiver, retorna FALSE
	if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &CHAVE) != ERROR_SUCCESS) {
		_tprintf(TEXT("[INFO] >> A CHAVE '%s' não foi encontrada no registry.\n"), REG_PATH);
		return FALSE;
	}
	_tprintf(TEXT("[INFO] >> A CHAVE '%s' foi encontrada no registry.\n"), REG_PATH);

	// Tenta ler o valor do registry. Se não tiver, fecha a chave e retorna FALSE
	DWORD TIPO_CHAVE = REG_SZ;
	if (RegQueryValueEx(CHAVE, REG_VALUE, NULL, &TIPO_CHAVE, (LPBYTE)namedPipe, &tamanhoNamedPipe) != ERROR_SUCCESS) {
		_tprintf(TEXT("[INFO] >> O VALOR '%s' não foi encontrado no registry.\n"), REG_VALUE);
		RegCloseKey(CHAVE);
		return FALSE;
	}
	_tprintf(TEXT("[INFO] >> O VALOR '%s' foi encontrado no registry.\n"), REG_VALUE);

	RegCloseKey(CHAVE);
	return TRUE;

	// FALSE = Não tem chave ou valor no registry. O programa termina com erro e pede para o utilizador especificar o nome do NPIPE.
	// TRUE = Segue o código normalmente.
}

// Cria o NPIPE no registry
void cria_NPIPE_No_Registry(TCHAR* namedPipe) {
	// Nota: Apenas executado se o nome do NPIPE for especificado como argumento. 

	HKEY CHAVE;
	// Cria a Chave no Registry
	RegCreateKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,	KEY_WRITE, NULL, &CHAVE, NULL);
	_tprintf(TEXT("[INFO] >> Chave '%s' criada/aberta no registry.\n"), REG_PATH);

	// Escreve o valor do NPIPE no registry na respetiva Chave
	RegSetValueEx(CHAVE, REG_VALUE, 0, REG_SZ, (BYTE*)namedPipe, (_tcslen(namedPipe) + 1) * sizeof(TCHAR));
	_tprintf(TEXT("[INFO] >> Valor '%s' escrito no registry.\n"), namedPipe);

	RegCloseKey(CHAVE);
}

// =============================================
// 
// Alertas / Mensagens

// Verificar se há um alerta novo no registry
BOOL lerAlerta(MSG_ALERTA* alerta) {
	HKEY CHAVE;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &CHAVE) != ERROR_SUCCESS)
		return FALSE;

	DWORD TIPO_CHAVE = REG_BINARY;
	DWORD TAMANHO_MENSAGEM = sizeof(MSG_ALERTA);
	if (RegQueryValueEx(CHAVE, namedPipe, NULL, &TIPO_CHAVE, (LPBYTE)alerta, &TAMANHO_MENSAGEM) != ERROR_SUCCESS) {
		RegCloseKey(CHAVE);
		return FALSE;
	}

	RegCloseKey(CHAVE);
	return TRUE;
}

// Imprimir a mensagem de alerta no placar
void printMensagem(TCHAR* alerta_Mensagem, DWORD alerta_Duracao, BOOL bool_Tempo) {
	
	// Obtem o tempo do sistema e converte para variavel
	SYSTEMTIME TIME;
	GetLocalTime(&TIME);

	// Mostra a mensagem de alerta no placar.
	if (bool_Tempo)
		_tprintf(_T("%02d/%02d/%04d (%02d:%02d:%02d): '%s' (%d segundos)\n"),
			TIME.wDay, TIME.wMonth, TIME.wYear,
			TIME.wHour, TIME.wMinute, TIME.wSecond,
			alerta_Mensagem, alerta_Duracao);

	// Ou o placar fica vazio ("---")
	else
		_tprintf(_T("%02d/%02d/%04d (%02d:%02d:%02d): '%s'\n"),
			TIME.wDay, TIME.wMonth, TIME.wYear,
			TIME.wHour, TIME.wMinute, TIME.wSecond,
			alerta_Mensagem);
}

// =============================================
// 
// Thread dos Alertas e Mensagens

DWORD WINAPI threadAlertas(LPVOID lpParam) {
    HANDLE handles[2] = { hEvento, hTimer };
    MSG_ALERTA alerta;
    DWORD res;

    while (running) {
        // Espera por qualquer um dos dois: novo alerta OU fim do tempo do alerta atual
        res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (!running) break;

        if (res == WAIT_OBJECT_0) { 
            // EVENTO: Novo Alerta
            ResetEvent(hEvento);
            if (lerAlerta(&alerta)) {
                printMensagem(alerta.msg, alerta.duracao, TRUE);

                // Re-arma ou cancela e arma o timer
                LARGE_INTEGER li;
                li.QuadPart = -(LONGLONG)alerta.duracao * 10000000LL;
                SetWaitableTimer(hTimer, &li, 0, NULL, NULL, FALSE);
            }
        } 
        else if (res == WAIT_OBJECT_0 + 1) {
            // TIMER: Alerta expirou
            printMensagem(_T("---"), 0, FALSE);
        }
    }
    return 0;
}

// =============================================
// 
// Thread do Input do Utilizador

DWORD WINAPI threadInput(LPVOID lpParam) {
	TCHAR comando[COMMAND_SIZE];

	while (running) {
		_tprintf(TEXT("[CMD] >> "));
		_fgetts(comando, COMMAND_SIZE, stdin);

		if (_tcsncmp(comando, TEXT("liga"), 4) == 0) {
			int id = rand() % 100 + 1;
			_tprintf(TEXT("[INFO] >> Identificador = %d\n"), id);
		}
		else if (_tcsncmp(comando, TEXT("desliga"), 7) == 0) {
			_tprintf(TEXT("[INFO] >> A terminar...\n"));
			running = FALSE;
			SetEvent(hEvento);
			CancelWaitableTimer(hTimer);
			break;
		}
	}

	return 0;
}

// =============================================
// 
// Inicio do Código | MAIN

int _tmain(int argc, TCHAR* argv[]) {
	_tprintf(TEXT("\n\n[INFO] >> A iniciar o Placar...\n\n"));
	HANDLE hThreads[2];

#ifdef _UNICODE
	(VOID)_setmode(_fileno(stdin), _O_WTEXT);
	(VOID)_setmode(_fileno(stdout), _O_WTEXT);
#endif

	// =============================================
	// NPIPE - Leitura e validação do comando

	// Argumentos a mais do que o necessário
	if (argc > 2) {
		_tprintf("[ERRO] >> Demasiados argumentos. Uso: placar.exe <NPIPE>\n");
	}
	// Sem argumentos especificados
	else if (argc < 2) {
		// Verifica se o nome do NPIPE está no registry
		if (!verifica_NPIPE_No_Registry(namedPipe, sizeof(namedPipe))) {
			_tprintf(TEXT("[ERRO] >> Problemas ao criar a CHAVE/VALOR no Registry!\n"));
			exit(1);
		}
	}
	// Tem exatamente 2 argumentos, o segundo é o nome do NPIPE
	else {
		_tcscpy_s(namedPipe, sizeof(namedPipe) / sizeof(TCHAR), argv[1]);
		cria_NPIPE_No_Registry(namedPipe);
	}

	_tprintf(TEXT("[INFO] >> Named Pipe = '%s'\n"), namedPipe);

	// =============================================
	// EVENT - Evento para notificar os alertas/mensagens
	hEvento = CreateEvent(NULL, TRUE, FALSE, EVENT_NAME);
	if (hEvento == NULL) {
		_tprintf(TEXT("[ERRO] Falha ao criar evento: %d\n"), GetLastError());
		return 1;
	}
	_tprintf(TEXT("[INFO] >> Evento '%s' criado.\n"), EVENT_NAME);

	// =============================================
	// WAITABLE TIMER - Temporizador
	hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
	if (hTimer == NULL) {
		_tprintf(TEXT("[ERRO] Falha ao criar timer: %d\n"), GetLastError());
		return 1;
	}
	_tprintf(TEXT("[INFO] >> Waitable Timer criado.\n"));

	// =============================================
	// THREADS
	hThreads[0] = CreateThread(NULL, 0, threadInput, NULL, 0, NULL);
	hThreads[1] = CreateThread(NULL, 0, threadAlertas, NULL, 0, NULL);

	// =============================================
	// Caso as threads terminem, o programa termina
	WaitForMultipleObjects(2, hThreads, TRUE, INFINITE);
	_tprintf(TEXT("[INFO] >> Threads terminadas...\n"));

	CloseHandle(hEvento);
	CloseHandle(hTimer);
	CloseHandle(hThreads[0]);
	CloseHandle(hThreads[1]);

	_tprintf(TEXT("\n\n[INFO] >> Programa Terminado!\n\n"));
	return 0;
}