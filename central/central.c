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
    return FALSE; /* remover apos implementacao */
}

BOOL libertar_central(EstadoCentral *estado)
{
    /*
     * TODO:
     * - Fechar todos os hPipe dos placares ainda ativos (loop sobre placares[])
     * - Destruir a CRITICAL_SECTION com DeleteCriticalSection()
     * - Retornar TRUE
     */
    return FALSE; /* remover apos implementacao */
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

    BYTE buffer[PIPE_BUFFER_SIZE];
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
}
