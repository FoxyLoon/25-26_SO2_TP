#include "central.h"
#include <stdio.h>
#include <stdlib.h>
#include "../placar/common.h"

/* -----------------------------------------------------------------------
 * Estado global
 * ----------------------------------------------------------------------- */

static EstadoCentral g_estado;

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int _tmain(int argc, TCHAR* argv[])
{
	HANDLE hThreads[2];

	if (!inicializar_central(&g_estado, argc, argv)) {
		_tprintf(_T("Erro na inicializacao. A terminar.\n"));
		return 1;
	}

	_tprintf(_T("Central iniciada. NamedPipe = '%s'\n"), g_estado.nomePipeCompleto);
	_tprintf(_T("Escreva 'ajuda' para ver os comandos disponiveis.\n"));

	hThreads[0] = CreateThread(NULL, 0, threadListener, &g_estado, 0, NULL);
	hThreads[1] = CreateThread(NULL, 0, threadUtilizador_central, &g_estado, 0, NULL);

	if (hThreads[0] == NULL || hThreads[1] == NULL) {
		_tprintf(_T("Erro ao criar threads.\n"));
		libertar_central(&g_estado);
		return 1;
	}

	WaitForMultipleObjects(2, hThreads, TRUE, INFINITE);

	CloseHandle(hThreads[0]);
	CloseHandle(hThreads[1]);

	libertar_central(&g_estado);
	return 0;
}

/* -----------------------------------------------------------------------
 * Inicializacao e limpeza
 * ----------------------------------------------------------------------- */

BOOL inicializar_central(EstadoCentral* estado, int argc, TCHAR* argv[])
{
	if (argc < 2) {
		_tprintf(_T("Uso: %s <nome_pipe>\n"), argv[0]);
		return FALSE;
	}

	_tcscpy_s(estado->nomePipe, _countof(estado->nomePipe), argv[1]);
	_sntprintf_s(estado->nomePipeCompleto, _countof(estado->nomePipeCompleto), _TRUNCATE, _T("%s%s"), PIPE_PREFIX, estado->nomePipe);

	InitializeCriticalSection(&estado->cs);

	estado->numPlacares = 0;
	estado->proximoId = 1;
	estado->terminar = FALSE;
	estado->hPipeListener = INVALID_HANDLE_VALUE;
	memset(estado->placares, 0, sizeof(estado->placares));

	/* M3: inicializar campos SHM */
	estado->hFileMapping = NULL;
	estado->pShm = NULL;
	estado->hEventMonitor = NULL;

	/* Criar memoria partilhada para o(s) monitor(s) */
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
		DeleteCriticalSection(&estado->cs);
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
		estado->hFileMapping = NULL;
		DeleteCriticalSection(&estado->cs);
		return FALSE;
	}

	ZeroMemory(estado->pShm, sizeof(SHM_ALERTA));

	/* Criar evento nomeado para sincronizacao com monitores (auto-reset) */
	estado->hEventMonitor = CreateEvent(
		NULL,
		FALSE,  /* auto-reset */
		FALSE,  /* initial state: nao assinalado */
		EVENT_NOTIFICAR
	);

	if (estado->hEventMonitor == NULL) {
		_tprintf(_T("Erro ao criar evento monitor: %lu\n"), GetLastError());
		UnmapViewOfFile(estado->pShm);
		estado->pShm = NULL;
		CloseHandle(estado->hFileMapping);
		estado->hFileMapping = NULL;
		DeleteCriticalSection(&estado->cs);
		return FALSE;
	}

	return TRUE;
}

BOOL libertar_central(EstadoCentral* estado)
{
	for (int i = 0; i < MAX_PLACARES; i++) {
		if (estado->placares[i].ativo) {
			CloseHandle(estado->placares[i].hPipe);
		}
	}

	/* M3: libertar recursos de memoria partilhada */
	if (estado->pShm != NULL) {
		UnmapViewOfFile(estado->pShm);
		estado->pShm = NULL;
	}

	if (estado->hFileMapping != NULL) {
		CloseHandle(estado->hFileMapping);
		estado->hFileMapping = NULL;
	}

	if (estado->hEventMonitor != NULL) {
		CloseHandle(estado->hEventMonitor);
		estado->hEventMonitor = NULL;
	}

	DeleteCriticalSection(&estado->cs);

	return TRUE;
}

/* -----------------------------------------------------------------------
 * Thread listener - aceita ligacoes de novos placares
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadListener(LPVOID param)
{
	EstadoCentral* estado = (EstadoCentral*)param;

	while (!estado->terminar) {
		HANDLE hPipe = CreateNamedPipe(
			estado->nomePipeCompleto,
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			MAX_PLACARES,
			PIPE_BUFFER_SIZE,
			PIPE_BUFFER_SIZE,
			0,
			NULL
		);

		if (hPipe == INVALID_HANDLE_VALUE) {
			continue;
		}

		/* Guardar o handle do pipe listener para debloqueio durante encerramento */
		EnterCriticalSection(&estado->cs);
		estado->hPipeListener = hPipe;
		LeaveCriticalSection(&estado->cs);

		if (!ConnectNamedPipe(hPipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
			CloseHandle(hPipe);
			continue;
		}

		EnterCriticalSection(&estado->cs);
		estado->hPipeListener = INVALID_HANDLE_VALUE;
		LeaveCriticalSection(&estado->cs);

		EnterCriticalSection(&estado->cs);
		int slot = encontrarSlotLivre(estado);

		if (slot == -1) {
			DisconnectNamedPipe(hPipe);
			CloseHandle(hPipe);
			LeaveCriticalSection(&estado->cs);
			continue;
		}

		estado->placares[slot].hPipe = hPipe;
		estado->placares[slot].ativo = TRUE;
		estado->numPlacares++;

		ArgThreadPlacar* arg = (ArgThreadPlacar*)malloc(sizeof(ArgThreadPlacar));
		arg->estado = estado;
		arg->indicePlacar = slot;

		HANDLE hThread = CreateThread(NULL, 0, threadPlacar, arg, 0, NULL);
		if (hThread != NULL) {
			estado->placares[slot].hThread = hThread;
		}
		else {
			estado->placares[slot].ativo = FALSE;
			estado->numPlacares--;
			free(arg);
		}

		LeaveCriticalSection(&estado->cs);
	};

	return 0;
}

/* -----------------------------------------------------------------------
 * Thread por placar - gere a comunicacao com um placar especifico
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadPlacar(LPVOID param)
{
	ArgThreadPlacar* arg = (ArgThreadPlacar*)param;
	EstadoCentral* estado = arg->estado;
	int              idx = arg->indicePlacar;
	free(arg);

	HANDLE hPipe;
	EnterCriticalSection(&estado->cs);
	hPipe = estado->placares[idx].hPipe;
	LeaveCriticalSection(&estado->cs);

	BYTE  buffer[PIPE_BUFFER_SIZE];
	DWORD bytesLidos;

	EnterCriticalSection(&estado->cs);
	BOOL ativo = estado->placares[idx].ativo;
	LeaveCriticalSection(&estado->cs);

	while (ativo) {
		if (!receberMensagem(hPipe, buffer, PIPE_BUFFER_SIZE, &bytesLidos)) {
			removerPlacar(estado, idx);
			break;
		}

		BYTE tipo = buffer[0];

		switch (tipo) {
		case MSG_TIPO_LIGAR: {
			EnterCriticalSection(&estado->cs);
			estado->placares[idx].identificador = estado->proximoId++;
			LeaveCriticalSection(&estado->cs);

			MSG_ID msgId;
			msgId.tipo = MSG_TIPO_ID;
			msgId.identificador = estado->placares[idx].identificador;

			if (enviarMensagem(hPipe, &msgId, sizeof(msgId))) {
				_tprintf(_T("\n[Placar %lu registado]\n> "), msgId.identificador);
			}
			break;
		}
		case MSG_TIPO_DESLIGAR: {
			MSG_CMD_PC cmdDesligar;
			cmdDesligar.tipo = MSG_TIPO_DESLIGAR;
			enviarMensagem(hPipe, &cmdDesligar, sizeof(cmdDesligar));

			removerPlacar(estado, idx);
			return 0;
		}
		case MSG_TIPO_FIM_ALERTA: {
			EnterCriticalSection(&estado->cs);
			estado->placares[idx].temAlerta = FALSE;
			LeaveCriticalSection(&estado->cs);

			_tprintf(_T("\n[Alerta TERMINOU no placar %lu]\n> "),
				estado->placares[idx].identificador);

			/* M3: atualizar SHM apos fim de alerta reportado pelo placar */
			atualizarSHM(estado);

			break;
		}
		case MSG_TIPO_ALERTA: {
			MSG_ALERTA* alerta = (MSG_ALERTA*)buffer;

			EnterCriticalSection(&estado->cs);
			estado->placares[idx].temAlerta = TRUE;
			estado->placares[idx].alertaAtivo = *alerta;
			LeaveCriticalSection(&estado->cs);

			_tprintf(_T("\n[Confirmacao de alerta do placar %lu]\n> "),
				estado->placares[idx].identificador);

			break;
		}
		case MSG_TIPO_CANCELAR: {
			EnterCriticalSection(&estado->cs);
			estado->placares[idx].temAlerta = FALSE;
			LeaveCriticalSection(&estado->cs);

			_tprintf(_T("\n[Alerta CANCELADO no placar %lu]\n> "),
				estado->placares[idx].identificador);

			break;
		}
		default: {
			break;
		}
		}

		EnterCriticalSection(&estado->cs);
		ativo = estado->placares[idx].ativo;
		LeaveCriticalSection(&estado->cs);
	}

	return 0;
}

/* -----------------------------------------------------------------------
 * Thread do utilizador administrador
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadUtilizador_central(LPVOID param)
{
	EstadoCentral* estado = (EstadoCentral*)param;
	TCHAR linha[CENTRAL_MAX_CMD_LEN];

	while (!estado->terminar) {
		_tprintf(_T("> "));
		if (_fgetts(linha, CENTRAL_MAX_CMD_LEN, stdin) == NULL) {
			break;
		}
		size_t len = _tcslen(linha);
		if (len > 0 && linha[len - 1] == _T('\n')) {
			linha[len - 1] = _T('\0');
		}
		processarComando_central(linha, estado);
	}
	return 0;
}

/* -----------------------------------------------------------------------
 * Processamento de comandos do administrador
 * ----------------------------------------------------------------------- */

void processarComando_central(const TCHAR* linha, EstadoCentral* estado)
{
	TCHAR comando[CENTRAL_MAX_CMD_LEN];
	DWORD duracao, idPlacar;

	int numScanned = _stscanf_s(linha, _T("%s"), comando, CENTRAL_MAX_CMD_LEN);

	if (numScanned < 1) {
		return;
	}

	if (_tcscmp(comando, CENTRAL_CMD_ALERTA) == 0) {
		TCHAR* posicao = (TCHAR*)linha + _tcslen(CENTRAL_CMD_ALERTA);
		while (*posicao == _T(' ')) posicao++;

		TCHAR tempoCopy[CENTRAL_MAX_CMD_LEN];
		_tcscpy_s(tempoCopy, CENTRAL_MAX_CMD_LEN, posicao);

		TCHAR* tokens[CENTRAL_MAX_CMD_LEN / 2];
		int    numTokens = 0;
		TCHAR* contexto = NULL;
		TCHAR* token = _tcstok_s(tempoCopy, _T(" "), &contexto);

		while (token != NULL && numTokens < CENTRAL_MAX_CMD_LEN / 2) {
			tokens[numTokens++] = token;
			token = _tcstok_s(NULL, _T(" "), &contexto);
		}

		if (numTokens >= 3) {
			idPlacar = (DWORD)_tcstoul(tokens[numTokens - 1], NULL, 10);
			duracao = (DWORD)_tcstoul(tokens[numTokens - 2], NULL, 10);

			TCHAR msg[MAX_MSG_LEN] = _T("");
			for (int i = 0; i < numTokens - 2; i++) {
				if (i > 0) _tcsncat_s(msg, MAX_MSG_LEN, _T(" "), _TRUNCATE);
				_tcsncat_s(msg, MAX_MSG_LEN, tokens[i], _TRUNCATE);
			}
			executarAlerta(msg, duracao, idPlacar, estado);
		}
		else {
			_tprintf(_T("Formato: alerta <msg> <duracao> <id_placar>\n"));
		}
	}
	else if (_tcscmp(comando, CENTRAL_CMD_CANCELAR) == 0) {
		int numScanned2 = _stscanf_s(linha, _T("%*s %lu"), &idPlacar);

		if (numScanned2 < 1) {
			_tprintf(_T("Formato: cancelar <id_placar>\n"));
		}
		else {
			executarCancelar(idPlacar, estado);
		}
	}
	else if (_tcscmp(comando, CENTRAL_CMD_LISTAR) == 0) {
		executarListar(estado);
	}
	else if (_tcscmp(comando, CENTRAL_CMD_ENCERRAR) == 0) {
		executarEncerrar(estado);
	}
	else if (_tcscmp(comando, CENTRAL_CMD_AJUDA) == 0) {
		executarAjuda_central();
	}
	else {
		_tprintf(_T("Comando invalido. Escreva 'ajuda' para listar os comandos.\n"));
	}
}

void executarAjuda_central(void)
{
	_tprintf(_T("=== Comandos da Central ===\n"));
	_tprintf(_T("alerta <msg> <duracao> <id_placar> : Envia alerta para placar (id_placar=0 envia a todos)\n"));
	_tprintf(_T("cancelar <id_placar>               : Cancela alerta ativo num determinado placar\n"));
	_tprintf(_T("listar                             : Lista placares registados e alertas ativos\n"));
	_tprintf(_T("encerrar                           : Encerra a plataforma e notifica todos\n"));
	_tprintf(_T("ajuda                              : Mostra esta mensagem de ajuda\n"));
	_tprintf(_T("===========================\n"));
}

void executarAlerta(const TCHAR* msg, DWORD duracao, DWORD idPlacar, EstadoCentral* estado)
{
	MSG_ALERTA alerta;
	alerta.tipo = MSG_TIPO_ALERTA;
	_tcscpy_s(alerta.msg, MAX_MSG_LEN, msg);
	alerta.duracao = duracao;

	if (idPlacar == 0) {
		for (int i = 0; i < MAX_PLACARES; i++) {
			EnterCriticalSection(&estado->cs);
			BOOL   ativo = estado->placares[i].ativo;
			HANDLE hPipe = estado->placares[i].hPipe;
			LeaveCriticalSection(&estado->cs);

			if (!ativo) continue;

			if (!enviarMensagem(hPipe, &alerta, sizeof(alerta))) {
				_tprintf(_T("Falha ao enviar alerta para placar indice %d. Pipe possivelmente fechado.\n"), i);
				continue;
			}

			EnterCriticalSection(&estado->cs);
			estado->placares[i].temAlerta = TRUE;
			estado->placares[i].alertaAtivo = alerta;
			LeaveCriticalSection(&estado->cs);
		}

		/* M3: atualizar SHM depois de processar todos os placares (broadcast) */
		atualizarSHM(estado);
	}
	else {
		EnterCriticalSection(&estado->cs);
		int    idx = encontrarPlacaPorId(estado, idPlacar);
		HANDLE hPipe = (idx != -1) ? estado->placares[idx].hPipe : INVALID_HANDLE_VALUE;
		LeaveCriticalSection(&estado->cs);

		if (idx != -1) {
			if (!enviarMensagem(hPipe, &alerta, sizeof(alerta))) {
				_tprintf(_T("Falha ao enviar alerta para placar ID %lu.\n"), idPlacar);
			}
			else {
				EnterCriticalSection(&estado->cs);
				estado->placares[idx].temAlerta = TRUE;
				estado->placares[idx].alertaAtivo = alerta;
				LeaveCriticalSection(&estado->cs);

				/* M3: atualizar SHM apos alerta enviado com sucesso */
				atualizarSHM(estado);
			}
		}
		else {
			_tprintf(_T("Placar com ID %lu nao encontrado.\n"), idPlacar);
		}
	}
}

void executarCancelar(DWORD idPlacar, EstadoCentral* estado)
{
	EnterCriticalSection(&estado->cs);
	int idx = encontrarPlacaPorId(estado, idPlacar);
	if (idx == -1) {
		LeaveCriticalSection(&estado->cs);
		_tprintf(_T("Placar com ID %lu nao encontrado.\n"), idPlacar);
		return;
	}
	if (!estado->placares[idx].temAlerta) {
		LeaveCriticalSection(&estado->cs);
		_tprintf(_T("Placar com ID %lu nao tem alerta ativo.\n"), idPlacar);
		return;
	}
	HANDLE hPipe = estado->placares[idx].hPipe;
	LeaveCriticalSection(&estado->cs);

	MSG_CMD_CP cmdCancelar;
	cmdCancelar.tipo = MSG_TIPO_CANCELAR;

	if (!enviarMensagem(hPipe, &cmdCancelar, sizeof(cmdCancelar))) {
		_tprintf(_T("Falha ao enviar cancelamento para placar ID %lu.\n"), idPlacar);
		return;
	}

	// ==============================
	// 20260521 - Adicionado confirma��o da rece��o do cancelamento
	// MSG_CMD_CP confirmacao;
	// DWORD bytesLidos;

	// if (!receberMensagem(hPipe, &confirmacao, sizeof(confirmacao), &bytesLidos)) {
	// 	_tprintf(_T("Falha ao receber confirmacao de cancelamento do placar ID %lu.\n"), idPlacar);
	// 	return;
	// }

	// if (confirmacao.tipo != MSG_TIPO_CANCELAR) {
	// 	_tprintf(_T("Confirmacao invalida de cancelamento do placar ID %lu.\n"), idPlacar);
	// 	return;
	// }

	// ==============================

	EnterCriticalSection(&estado->cs);
	estado->placares[idx].temAlerta = FALSE;
	LeaveCriticalSection(&estado->cs);

	/* M3: atualizar SHM apos cancelamento */
	atualizarSHM(estado);
}

void executarListar(EstadoCentral* estado)
{
	EnterCriticalSection(&estado->cs);
	_tprintf(_T("--- Placares Registados ---\n"));
	int cont = 0;
	for (int i = 0; i < MAX_PLACARES; i++) {
		if (estado->placares[i].ativo) {
			cont++;
			if (estado->placares[i].temAlerta) {
				_tprintf(_T("ID: %lu | Alerta Ativo: '%s' (Duracao: %lu s)\n"),
					estado->placares[i].identificador,
					estado->placares[i].alertaAtivo.msg,
					estado->placares[i].alertaAtivo.duracao);
			}
			else {
				_tprintf(_T("ID: %lu | Sem alerta\n"),
					estado->placares[i].identificador);
			}
		}
	}
	if (cont == 0) _tprintf(_T("Nenhum placar ligado.\n"));
	_tprintf(_T("---------------------------\n"));
	LeaveCriticalSection(&estado->cs);
}

void executarEncerrar(EstadoCentral* estado)
{
	EnterCriticalSection(&estado->cs);
	for (int i = 0; i < MAX_PLACARES; i++) {
		if (estado->placares[i].ativo) {
			MSG_CMD_CP cmdEncerrar;
			cmdEncerrar.tipo = MSG_TIPO_ENCERRAR;
			enviarMensagem(estado->placares[i].hPipe, &cmdEncerrar, sizeof(cmdEncerrar));
		}
	}

	/* Fechar o pipe listener para deblocar a thread listener que esta a aguardar ConnectNamedPipe() */
	if (estado->hPipeListener != INVALID_HANDLE_VALUE) {
		DisconnectNamedPipe(estado->hPipeListener);
		CloseHandle(estado->hPipeListener);
		estado->hPipeListener = INVALID_HANDLE_VALUE;
	}

	/* M3: assinalar encerramento na SHM antes de notificar monitores */
	if (estado->pShm != NULL) {
		estado->pShm->desligar = TRUE;
	}

	estado->terminar = TRUE;
	LeaveCriticalSection(&estado->cs);

	/* M3: notificar monitores do encerramento (fora da CS) */
	if (estado->hEventMonitor != NULL) {
		SetEvent(estado->hEventMonitor);
	}
}

/* -----------------------------------------------------------------------
 * Comunicacao com os placares
 * ----------------------------------------------------------------------- */

BOOL enviarMensagem(HANDLE hPipe, const void* dados, DWORD tamanho)
{
	HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hEvent == NULL) return FALSE;

	OVERLAPPED ov = { 0 };
	ov.hEvent = hEvent;
	DWORD bytesWritten = 0;

	BOOL ok = WriteFile(hPipe, dados, tamanho, NULL, &ov);
	if (!ok && GetLastError() != ERROR_IO_PENDING) {
		CloseHandle(hEvent);
		return FALSE;
	}

	DWORD waitResult = WaitForSingleObject(hEvent, INFINITE);

	if (waitResult == WAIT_OBJECT_0) {
		ok = GetOverlappedResult(hPipe, &ov, &bytesWritten, FALSE);
		CloseHandle(hEvent);
		return ok && (bytesWritten == tamanho);
	}
	else {
		CloseHandle(hEvent);
		return FALSE;
	}
}

BOOL receberMensagem(HANDLE hPipe, void* buffer, DWORD tamanhoMax, DWORD* bytesLidos)
{
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

	DWORD waitResult = WaitForSingleObject(hEvent, INFINITE);

	if (waitResult == WAIT_OBJECT_0) {
		ok = GetOverlappedResult(hPipe, &ov, bytesLidos, FALSE);
		CloseHandle(hEvent);
		return ok;
	}
	else {
		CloseHandle(hEvent);
		return FALSE;
	}
}

/* -----------------------------------------------------------------------
 * Gestao de placares
 * ----------------------------------------------------------------------- */

int encontrarPlacaPorId(EstadoCentral* estado, DWORD id)
{
	for (int i = 0; i < MAX_PLACARES; i++) {
		if (estado->placares[i].ativo && estado->placares[i].identificador == id) {
			return i;
		}
	}
	return -1;
}

int encontrarSlotLivre(EstadoCentral* estado)
{
	for (int i = 0; i < MAX_PLACARES; i++) {
		if (!estado->placares[i].ativo) {
			return i;
		}
	}
	return -1;
}

void removerPlacar(EstadoCentral* estado, int indice)
{
	EnterCriticalSection(&estado->cs);
	if (estado->placares[indice].hPipe != INVALID_HANDLE_VALUE) {
		DisconnectNamedPipe(estado->placares[indice].hPipe);
		CloseHandle(estado->placares[indice].hPipe);
		estado->placares[indice].hPipe = INVALID_HANDLE_VALUE;
	}
	estado->placares[indice].ativo = FALSE;
	estado->placares[indice].identificador = 0;
	estado->placares[indice].temAlerta = FALSE;
	memset(&estado->placares[indice].alertaAtivo, 0, sizeof(MSG_ALERTA));
	estado->numPlacares--;
	LeaveCriticalSection(&estado->cs);

	/* M3: atualizar SHM apos remocao do placar */
	atualizarSHM(estado);
}

/* -----------------------------------------------------------------------
 * Memoria partilhada
 * ----------------------------------------------------------------------- */

void atualizarSHM(EstadoCentral* estado)
{
	if (estado == NULL || estado->pShm == NULL)
		return;

	EnterCriticalSection(&estado->cs);

	/* Limpar toda a SHM antes de reescrever */
	ZeroMemory(estado->pShm, sizeof(SHM_ALERTA));

	/* Copiar dados dos placares ativos com alertas */
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

	/* Sinalizar monitores (fora da CS para nao segurar o lock desnecessariamente) */
	SetEvent(estado->hEventMonitor);
}

/* -----------------------------------------------------------------------
 * Utilitarios
 * ----------------------------------------------------------------------- */

void obterDataHoraAtual_central(TCHAR* buffer, DWORD tamanho)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	_sntprintf_s(buffer, tamanho, _TRUNCATE, _T("%02d/%02d/%04d (%02d:%02d:%02d)"),
		st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
}