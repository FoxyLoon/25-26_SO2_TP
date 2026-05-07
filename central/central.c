#include "central.h"
#include <stdio.h>
#include <stdlib.h>

/* -----------------------------------------------------------------------
 * Estado global
 * ----------------------------------------------------------------------- */

static EstadoCentral g_estado;

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int _tmain(int argc, TCHAR *argv[])
{
    HANDLE hThreads[2];

    if (!inicializar_central(&g_estado, argc, argv)) {
        _tprintf(_T("Erro na inicializacao. A terminar.\n"));
        return 1;
    }

    _tprintf(_T("Central iniciada. NamedPipe = '%s'\n"), g_estado.nomePipeCompleto);

    /*
     * Criar as duas threads principais:
     *   [0] threadListener      - aceita ligacoes de novos placares
     *   [1] threadUtilizador_central - le comandos do administrador
     */
    hThreads[0] = CreateThread(NULL, 0, threadListener,           &g_estado, 0, NULL);
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

BOOL inicializar_central(EstadoCentral *estado, int argc, TCHAR *argv[])
{
    /*
     * TODO:
     * - Verificar que argv[1] existe; se nao, mostrar uso e retornar FALSE
     * - Copiar argv[1] para estado->nomePipe
     * - Construir estado->nomePipeCompleto = PIPE_PREFIX + nomePipe
     *   (ex: "\\\\.\\pipe\\tubo")
     * - Inicializar a CRITICAL_SECTION
     * - Inicializar estado->numPlacares = 0, proximoId = 1, terminar = FALSE
     * - Zerar o array estado->placares (memset ou loop)
     * - Retornar TRUE
     */

    if (argc < 2) { // se argc for menor que 2, significa que o nome do pipe nao foi fornecido
        _tprintf(_T("Uso: %s <nome_pipe>\n"), argv[0]);
        return FALSE;
    }

    _tcscpy_s(estado->nomePipe, _countof(estado->nomePipe), argv[1]); // copiar nome do pipe (sem prefixo)
    _sntprintf_s(estado->nomePipeCompleto, _countof(estado->nomePipeCompleto), _TRUNCATE, _T("%s%s"), PIPE_PREFIX, estado->nomePipe); // construir nome completo do pipe

    InitializeCriticalSection(&estado->cs); // inicializar a CRITICAL_SECTION

    estado->numPlacares = 0;
    estado->proximoId   = 1;
    estado->terminar    = FALSE;
    memset(estado->placares, 0, sizeof(estado->placares));

    return TRUE;
}

BOOL libertar_central(EstadoCentral *estado)
{
    /*
     * TODO:
     * - Fechar todos os hPipe dos placares ainda ativos (loop sobre placares[])
     * - Destruir a CRITICAL_SECTION com DeleteCriticalSection()
     * - Retornar TRUE
     */

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->placares[i].ativo) {
            CloseHandle(estado->placares[i].hPipe);
        }
    }
    DeleteCriticalSection(&estado->cs);

    return TRUE;
}

/* -----------------------------------------------------------------------
 * Thread listener - aceita ligacoes de novos placares
 * ----------------------------------------------------------------------- */

DWORD WINAPI threadListener(LPVOID param)
{
    EstadoCentral *estado = (EstadoCentral *)param;

    /*
     * Loop principal (enquanto !estado->terminar):
     *
     * 1. Criar uma nova instancia do named pipe com CreateNamedPipe():
     *      - dwPipeMode:   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
     *      - nMaxInstances: MAX_PLACARES
     *      - Buffers de entrada/saida: PIPE_BUFFER_SIZE
     *
     * 2. Aguardar ligacao de um cliente com ConnectNamedPipe().
     *    (bloqueante - espera ate um placar se ligar)
     *
     * 3. Quando um placar se liga:
     *    - EnterCriticalSection
     *    - Encontrar um slot livre com encontrarSlotLivre()
     *    - Se nao houver slot (numPlacares >= MAX_PLACARES):
     *        - DisconnectNamedPipe + CloseHandle e continuar o loop
     *    - Preencher placares[slot]: hPipe = handle obtido, ativo = TRUE
     *    - numPlacares++
     *    - LeaveCriticalSection
     *
     * 4. Criar uma nova thread threadPlacar para gerir este placar:
     *    - Alocar um ArgThreadPlacar com malloc() (a thread liberta com free())
     *    - Preencher estado e indicePlacar
     *    - CreateThread(..., threadPlacar, arg, ...)
     *    - Guardar o handle em placares[slot].hThread
     *
     * 5. Voltar ao inicio do loop para aceitar o proximo placar
     */

    while (!estado->terminar) {
        HANDLE hPipe = CreateNamedPipe(
            estado->nomePipeCompleto,
            PIPE_ACCESS_DUPLEX,
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

        if (!ConnectNamedPipe(hPipe, NULL)) {
            CloseHandle(hPipe);
            continue;
        }

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

        ArgThreadPlacar *arg = (ArgThreadPlacar *)malloc(sizeof(ArgThreadPlacar));
        arg->estado       = estado;
        arg->indicePlacar = slot;

        HANDLE hThread = CreateThread(NULL, 0, threadPlacar, arg, 0, NULL);
        if (hThread != NULL) {
            estado->placares[slot].hThread = hThread;
        }
        else {
            // Se falhou ao criar a thread, limpar o slot
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
    ArgThreadPlacar *arg    = (ArgThreadPlacar *)param;
    EstadoCentral   *estado = arg->estado;
    int              idx    = arg->indicePlacar;
    free(arg); /* libertar o argumento alocado em threadListener */

    HANDLE hPipe;
    EnterCriticalSection(&estado->cs);
    hPipe = estado->placares[idx].hPipe;
    LeaveCriticalSection(&estado->cs);

    BYTE  buffer[PIPE_BUFFER_SIZE];
    DWORD bytesLidos;

    /*
     * Loop principal (enquanto o placar estiver ativo):
     *
     * Ler uma mensagem do pipe com receberMensagem().
     * Verificar o campo 'tipo' do primeiro byte para saber o tipo:
     *
     * Caso MSG_TIPO_LIGAR (1):
     *   - EnterCriticalSection
     *   - Atribuir identificador: placares[idx].identificador = estado->proximoId++
     *   - LeaveCriticalSection
     *   - Construir MSG_ID com tipo = MSG_TIPO_ID e o identificador atribuido
     *   - enviarMensagem() com a MSG_ID
     *   - Mostrar na consola do central: "Placar %lu registado." (com o id)
     *
     * Caso MSG_TIPO_DESLIGAR (2):
     *   - Enviar confirmacao: MSG_CMD_PC com tipo = MSG_TIPO_DESLIGAR
     *   - removerPlacar() para limpar o slot
     *   - Sair do loop (break)
     *
     * Caso MSG_TIPO_FIM_ALERTA (3):
     *   - EnterCriticalSection
     *   - Limpar placares[idx].temAlerta = FALSE
     *   - LeaveCriticalSection
     *   - Nao e necessario enviar resposta
     *
     * Em caso de erro de leitura (pipe desligado abruptamente):
     *   - removerPlacar() e sair do loop
     */

    EnterCriticalSection(&estado->cs);
    BOOL ativo = estado->placares[idx].ativo;
    LeaveCriticalSection(&estado->cs);

    while (ativo) {
        if (!receberMensagem(hPipe, buffer, PIPE_BUFFER_SIZE, &bytesLidos)) {
            // Erro de leitura (pipe desligado)
            removerPlacar(estado, idx);
            break;
        }

        BYTE tipo = buffer[0]; // Supondo que o tipo esta no primeiro byte

        switch (tipo) {
        case MSG_TIPO_LIGAR: {
            EnterCriticalSection(&estado->cs);
            estado->placares[idx].identificador = estado->proximoId++;
            LeaveCriticalSection(&estado->cs);

            MSG_ID msgId;
            msgId.tipo          = MSG_TIPO_ID;
            msgId.identificador = estado->placares[idx].identificador;

            enviarMensagem(hPipe, &msgId, sizeof(msgId));

            _tprintf(_T("Placar %lu registado.\n"), msgId.identificador);
            break;
        }
        case MSG_TIPO_DESLIGAR: {
            MSG_CMD_PC cmdDesligar;
            cmdDesligar.tipo = MSG_TIPO_DESLIGAR;
            enviarMensagem(hPipe, &cmdDesligar, sizeof(cmdDesligar));

            removerPlacar(estado, idx);
            return 0; // Sair do loop
        }
        case MSG_TIPO_FIM_ALERTA: {
            EnterCriticalSection(&estado->cs);
            estado->placares[idx].temAlerta = FALSE;
            LeaveCriticalSection(&estado->cs);
            break;
        }
        default: {
            // Tipo desconhecido - ignorar ou logar
            break;
        }
        }

        /* CORRECAO: a versao anterior declarava 'BOOL ativo' aqui dentro,
         * criando uma variavel local que sombreava (shadow) a do scope exterior
         * e nunca era lida pelo while - codigo morto com aviso C4456.
         * Corrigido: atribuir directamente a variavel 'ativo' do scope exterior. */
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
    EstadoCentral *estado = (EstadoCentral *)param;
    TCHAR linha[CENTRAL_MAX_CMD_LEN];

    /*
     * Loop principal (enquanto !estado->terminar):
     * - Mostrar prompt "> "
     * - Ler linha com _fgetts()
     * - Remover '\n' final
     * - Chamar processarComando_central()
     */

    while (!estado->terminar) {
        _tprintf(_T("> "));
        if (_fgetts(linha, CENTRAL_MAX_CMD_LEN, stdin) == NULL) {
            // Erro ou EOF - tratar como comando de encerrar
            break;
        }
        // Remover '\n' final
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

void processarComando_central(const TCHAR *linha, EstadoCentral *estado)
{
    /*
     * TODO:
     * Fazer parse da linha para extrair o comando e os argumentos.
     * Usar _stscanf_s() ou _tcstok_s() para dividir os tokens.
     *
     * - Se comecar por CENTRAL_CMD_ALERTA:
     *     Formato: "alerta <msg> <duracao> <id_placar>"
     *     Nota: a mensagem pode conter espacos - tratar com cuidado o parse
     *     -> executarAlerta(msg, duracao, idPlacar, estado)
     *
     * - Se comecar por CENTRAL_CMD_CANCELAR:
     *     Formato: "cancelar <id_placar>"
     *     -> executarCancelar(idPlacar, estado)
     *
     * - Se for CENTRAL_CMD_LISTAR:
     *     -> executarListar(estado)
     *
     * - Se for CENTRAL_CMD_ENCERRAR:
     *     -> executarEncerrar(estado)
     *
     * - Caso contrario:
     *     -> Mostrar "Comando invalido"
     */

    TCHAR comando[CENTRAL_MAX_CMD_LEN];
    DWORD duracao, idPlacar;

    /* Extrair comando (primeira palavra) */
    int numScanned = _stscanf_s(linha, _T("%s"), comando, CENTRAL_MAX_CMD_LEN);

    if (numScanned < 1) {
        _tprintf(_T("Comando invalido\n"));
        return;
    }

    /* Processar comandos */
    if (_tcscmp(comando, CENTRAL_CMD_ALERTA) == 0) {
        TCHAR *posicao = linha + _tcslen(CENTRAL_CMD_ALERTA);
        while (*posicao == _T(' ')) posicao++;

        TCHAR tempoCopy[CENTRAL_MAX_CMD_LEN];
        _tcscpy_s(tempoCopy, CENTRAL_MAX_CMD_LEN, posicao);

        /* Array grande o suficiente para o pior caso */
        TCHAR *tokens[CENTRAL_MAX_CMD_LEN / 2];
        int    numTokens = 0;
        TCHAR *contexto  = NULL;
        TCHAR *token     = _tcstok_s(tempoCopy, _T(" "), &contexto);

        while (token != NULL && numTokens < CENTRAL_MAX_CMD_LEN / 2) {
            tokens[numTokens++] = token;
            token = _tcstok_s(NULL, _T(" "), &contexto);
        }

        if (numTokens >= 3) {
            idPlacar = (DWORD)_tcstoul(tokens[numTokens - 1], NULL, 10);
            duracao  = (DWORD)_tcstoul(tokens[numTokens - 2], NULL, 10);

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
        /* Formato: cancelar <id_placar> */
        int numScanned2 = _stscanf_s(linha, _T("%s %lu"), comando, CENTRAL_MAX_CMD_LEN, &idPlacar);

        if (numScanned2 < 2) {
            _tprintf(_T("Formato: cancelar <id_placar>\n"));
        } else {
            executarCancelar(idPlacar, estado);
        }
    }
    else if (_tcscmp(comando, CENTRAL_CMD_LISTAR) == 0) {
        executarListar(estado);
    }
    else if (_tcscmp(comando, CENTRAL_CMD_ENCERRAR) == 0) {
        executarEncerrar(estado);
    }
    else {
        _tprintf(_T("Comando invalido\n"));
    }
}

void executarAlerta(const TCHAR *msg, DWORD duracao, DWORD idPlacar, EstadoCentral *estado)
{
    /*
     * TODO:
     * - Construir MSG_ALERTA com tipo=MSG_TIPO_ALERTA, msg e duracao
     *
     * - Se idPlacar == 0: enviar a TODOS os placares ativos (loop)
     *   Caso contrario: encontrar o placar com encontrarPlacaPorId() e enviar so a esse
     *
     * Para cada placar alvo:
     *   - enviarMensagem() com a MSG_ALERTA
     *   - Aguardar confirmacao com receberMensagem() (o placar ecoa a mesma estrutura)
     *   - EnterCriticalSection
     *   - Atualizar placares[i].temAlerta = TRUE e placares[i].alertaAtivo
     *   - LeaveCriticalSection
     */

    MSG_ALERTA alerta;
    alerta.tipo = MSG_TIPO_ALERTA;
    _tcscpy_s(alerta.msg, MAX_MSG_LEN, msg);
    alerta.duracao = duracao;

    if (idPlacar == 0) {
        /* Enviar a todos os placares ativos.
         *
         * CORRECAO: a critical section NAO pode ser mantida durante as chamadas
         * de I/O (enviarMensagem / receberMensagem), pois essas chamadas bloqueiam
         * ate o placar responder. Manter a CS bloqueada durante esse tempo causaria
         * deadlock: a threadPlacar do mesmo placar precisa da CS para processar
         * mensagens (ex: MSG_TIPO_FIM_ALERTA) entretanto recebidas.
         * Solucao: ler o handle com CS, largar a CS, fazer o I/O, re-adquirir
         * a CS apenas para actualizar o estado. */
        for (int i = 0; i < MAX_PLACARES; i++) {
            EnterCriticalSection(&estado->cs);
            BOOL   ativo  = estado->placares[i].ativo;
            HANDLE hPipe  = estado->placares[i].hPipe;
            LeaveCriticalSection(&estado->cs); /* largar ANTES do I/O */

            if (!ativo) continue;

            enviarMensagem(hPipe, &alerta, sizeof(alerta));

            MSG_ALERTA eco;
            DWORD bytesLidos;
            receberMensagem(hPipe, &eco, sizeof(eco), &bytesLidos);

            EnterCriticalSection(&estado->cs); /* re-adquirir para escrever estado */
            estado->placares[i].temAlerta   = TRUE;
            estado->placares[i].alertaAtivo = alerta;
            LeaveCriticalSection(&estado->cs);
        }
    }
    else {
        /* Enviar apenas ao placar com idPlacar.
         *
         * CORRECAO: encontrarPlacaPorId() deve ser chamado com a CS adquirida
         * (o array pode ser modificado por outra thread). O handle e lido com
         * CS e a CS e largada antes do I/O bloqueante, pelo mesmo motivo
         * descrito acima. */
        EnterCriticalSection(&estado->cs);
        int    idx   = encontrarPlacaPorId(estado, idPlacar);
        HANDLE hPipe = (idx != -1) ? estado->placares[idx].hPipe : INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&estado->cs); /* largar ANTES do I/O */

        if (idx != -1) {
            enviarMensagem(hPipe, &alerta, sizeof(alerta));

            MSG_ALERTA eco;
            DWORD bytesLidos;
            receberMensagem(hPipe, &eco, sizeof(eco), &bytesLidos);

            EnterCriticalSection(&estado->cs);
            estado->placares[idx].temAlerta   = TRUE;
            estado->placares[idx].alertaAtivo = alerta;
            LeaveCriticalSection(&estado->cs);
        } else {
            _tprintf(_T("Placar com ID %lu nao encontrado.\n"), idPlacar);
        }
    }
}

void executarCancelar(DWORD idPlacar, EstadoCentral *estado)
{
    /*
     * TODO:
     * - Encontrar o placar com encontrarPlacaPorId()
     * - Se nao encontrado ou nao tem alerta ativo: avisar e retornar
     * - Construir MSG_CMD_CP com tipo=MSG_TIPO_CANCELAR
     * - enviarMensagem() ao placar
     * - Aguardar confirmacao com receberMensagem()
     * - EnterCriticalSection
     * - Limpar placares[i].temAlerta = FALSE
     * - LeaveCriticalSection
     */

    /* CORRECAO: encontrarPlacaPorId() chamado com CS adquirida; handle lido
     * com CS; CS largada antes do I/O bloqueante (mesmo motivo de executarAlerta). */
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
    LeaveCriticalSection(&estado->cs); /* largar ANTES do I/O */

    MSG_CMD_CP cmdCancelar;
    cmdCancelar.tipo = MSG_TIPO_CANCELAR;
    enviarMensagem(hPipe, &cmdCancelar, sizeof(cmdCancelar));

    DWORD bytesLidos;
    receberMensagem(hPipe, &cmdCancelar, sizeof(cmdCancelar), &bytesLidos);

    EnterCriticalSection(&estado->cs);
    estado->placares[idx].temAlerta = FALSE;
    LeaveCriticalSection(&estado->cs);
}

void executarListar(EstadoCentral *estado)
{
    /*
     * TODO:
     * - EnterCriticalSection
     * - Iterar sobre placares[]
     * - Para cada placar com ativo=TRUE, mostrar:
     *     "ID: %lu | Alerta: '%s'" (ou "sem alerta" se temAlerta=FALSE)
     * - LeaveCriticalSection
     */

    EnterCriticalSection(&estado->cs);
    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->placares[i].ativo) {
            if (estado->placares[i].temAlerta) {
                _tprintf(_T("ID: %lu | Alerta: '%s'\n"),
                         estado->placares[i].identificador,
                         estado->placares[i].alertaAtivo.msg);
            } else {
                _tprintf(_T("ID: %lu | Sem alerta\n"),
                         estado->placares[i].identificador);
            }
        }
    }
    LeaveCriticalSection(&estado->cs);
}

void executarEncerrar(EstadoCentral *estado)
{
    /*
     * TODO:
     * - EnterCriticalSection
     * - Para cada placar ativo: enviar MSG_CMD_CP com tipo=MSG_TIPO_ENCERRAR
     *   (nao aguarda confirmacao - o placar termina ao receber este tipo)
     * - estado->terminar = TRUE
     * - LeaveCriticalSection
     */

    EnterCriticalSection(&estado->cs);
    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->placares[i].ativo) {
            MSG_CMD_CP cmdEncerrar;
            cmdEncerrar.tipo = MSG_TIPO_ENCERRAR;
            enviarMensagem(estado->placares[i].hPipe, &cmdEncerrar, sizeof(cmdEncerrar));
        }
    }
    estado->terminar = TRUE;
    LeaveCriticalSection(&estado->cs);
}

/* -----------------------------------------------------------------------
 * Comunicacao com os placares
 * ----------------------------------------------------------------------- */

BOOL enviarMensagem(HANDLE hPipe, const void *dados, DWORD tamanho)
{
    /*
     * TODO:
     * - Usar WriteFile() para escrever 'tamanho' bytes de 'dados' no pipe
     * - Verificar se bytesEscritos == tamanho
     * - Retornar TRUE em caso de sucesso, FALSE caso contrario
     */

    /* CORRECAO: declarado como DWORD (era 'int'), pois WriteFile exige LPDWORD. */
    DWORD bytesEscritos;
    if (WriteFile(hPipe, dados, tamanho, &bytesEscritos, NULL) && bytesEscritos == tamanho) {
        return TRUE;
    }
    return FALSE;
}

BOOL receberMensagem(HANDLE hPipe, void *buffer, DWORD tamanhoMax, DWORD *bytesLidos)
{
    /*
     * TODO:
     * - Usar ReadFile() para ler do pipe para 'buffer'
     * - Preencher *bytesLidos com o numero de bytes lidos
     * - Retornar TRUE em caso de sucesso, FALSE caso contrario (incluindo
     *   ERROR_BROKEN_PIPE que indica que o placar fechou a ligacao)
     */

    if (ReadFile(hPipe, buffer, tamanhoMax, bytesLidos, NULL)) {
        return TRUE;
    }
    return FALSE;
}

/* -----------------------------------------------------------------------
 * Gestao de placares
 * ----------------------------------------------------------------------- */

int encontrarPlacaPorId(EstadoCentral *estado, DWORD id)
{
    /*
     * TODO:
     * - Iterar sobre placares[]
     * - Retornar o indice do primeiro placar com ativo=TRUE e identificador==id
     * - Retornar -1 se nao encontrado
     * Nota: chamar com a critical section ja adquirida, ou adquirir aqui.
     */

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (estado->placares[i].ativo && estado->placares[i].identificador == id) {
            return i;
        }
    }
    return -1;
}

int encontrarSlotLivre(EstadoCentral *estado)
{
    /*
     * TODO:
     * - Iterar sobre placares[]
     * - Retornar o indice do primeiro slot com ativo=FALSE
     * - Retornar -1 se o array estiver cheio
     * Nota: assumir que a critical section ja esta adquirida.
     */

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (!estado->placares[i].ativo) {
            return i;
        }
    }
    return -1;
}

void removerPlacar(EstadoCentral *estado, int indice)
{
    /*
     * TODO:
     * - EnterCriticalSection (se ainda nao adquirida)
     * - DisconnectNamedPipe(placares[indice].hPipe)
     * - CloseHandle(placares[indice].hPipe)
     * - Marcar placares[indice].ativo = FALSE
     * - Zerar os restantes campos do slot
     * - estado->numPlacares--
     * - LeaveCriticalSection
     */

    EnterCriticalSection(&estado->cs);
    DisconnectNamedPipe(estado->placares[indice].hPipe);
    CloseHandle(estado->placares[indice].hPipe);
    estado->placares[indice].ativo        = FALSE;
    estado->placares[indice].identificador = 0;
    estado->placares[indice].temAlerta    = FALSE;
    memset(&estado->placares[indice].alertaAtivo, 0, sizeof(MSG_ALERTA));
    estado->numPlacares--;
    LeaveCriticalSection(&estado->cs);
}

/* -----------------------------------------------------------------------
 * Utilitarios
 * ----------------------------------------------------------------------- */

void obterDataHoraAtual_central(TCHAR *buffer, DWORD tamanho)
{
    /*
     * TODO: identico a obterDataHoraAtual() do placar.
     * - GetLocalTime() -> SYSTEMTIME
     * - _sntprintf_s() para formatar "DD/MM/AAAA (HH:MM:SS)"
     */

    SYSTEMTIME st;
    GetLocalTime(&st);
    _sntprintf_s(buffer, tamanho, _TRUNCATE, _T("%02d/%02d/%04d (%02d:%02d:%02d)"),
        st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
}
