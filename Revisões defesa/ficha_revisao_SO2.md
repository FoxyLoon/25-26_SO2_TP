# Ficha de Revisão - Defesa Oral SO2 "Placar Informativo"

> Estrutura: O que é? | O que faz? | Porque e onde usamos? | Como é usado no código?

---

## 1. Named Pipes (Comunicação Placar <-> Central)

### O que é?
Um Named Pipe é um mecanismo de IPC (Inter-Process Communication) do Windows que cria um canal de comunicação com nome, acessível por processos distintos através desse nome (`\\.\pipe\<nome>`). Ao contrário dos pipes anónimos, os Named Pipes existem no sistema de ficheiros de objetos do kernel e podem ser acedidos por processos sem relação de parentesco.

### O que faz?
Permite a troca de mensagens bidirecionais entre o `central` (servidor) e cada `placar` (cliente). O `central` cria o pipe e fica à escuta; cada `placar` conecta-se a ele. A comunicação é em **modo message** (`PIPE_TYPE_MESSAGE`), o que garante que cada `WriteFile` corresponde a exatamente um `ReadFile` no outro lado, preservando os limites das mensagens.

### Porque e onde usamos?
- É o mecanismo obrigatório especificado no enunciado para a comunicação Placar <-> Central.
- Permite comunicação bidirecional com múltiplos clientes (até `MAX_PLACARES = 20`) através de um único nome de pipe, usando instâncias múltiplas.
- É adequado para comunicação local (mesma máquina), estruturada e síncrona/assíncrona.

### Como é usado no código?

**Central (servidor) - criação e espera de ligação (`central.c`, `threadListener`):**
```c
HANDLE hPipe = CreateNamedPipe(
    estado->nomePipeCompleto,           // "\\.\pipe\tubo"
    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
    MAX_PLACARES,                       // max 20 instâncias
    PIPE_BUFFER_SIZE, PIPE_BUFFER_SIZE, // 512 bytes cada lado
    0, NULL
);
ConnectNamedPipe(hPipe, NULL);          // bloqueia até um cliente ligar
```

**Placar (cliente) - ligação ao pipe (`placar.c`, `executarLigar`):**
```c
hPipe = CreateFile(
    estado->nomePipeCompleto,
    GENERIC_READ | GENERIC_WRITE,
    0, NULL, OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED, NULL          // overlapped para I/O assíncrono
);
DWORD mode = PIPE_READMODE_MESSAGE;
SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);
```

**Envio e receção (`central.c`, funções `enviarMensagem` / `receberMensagem`):**
- Usam `WriteFile` / `ReadFile` com `OVERLAPPED` e `WaitForSingleObject` para I/O assíncrono.
- O `placar` usa `WaitForMultipleObjects` com o evento do pipe + o waitable timer em `threadAlertas`.

**Pergunta típica:** *"Porque é que o pipe é OVERLAPPED?"*
Resposta: Para que a thread de alertas do placar possa aguardar simultaneamente pelo pipe E pelo waitable timer sem bloquear indefinidamente num só. O `WaitForMultipleObjects` precisa de handles de eventos, não de leituras bloqueantes.

---

## 2. Threads (Modelo de Concorrência)

### O que é?
Uma thread é a unidade de execução dentro de um processo. Múltiplas threads partilham o mesmo espaço de endereçamento (variáveis globais, heap), mas cada uma tem a sua própria pilha de execução e contexto de registos.

### O que faz?
Permite que um processo execute várias tarefas em paralelo sem bloquear as restantes. Por exemplo, o `central` pode estar ao mesmo tempo a aguardar comandos do administrador, a aceitar novas ligações de placares e a comunicar com cada placar individualmente.

### Porque e onde usamos?
- O enunciado exige que os programas estejam sempre responsivos (aceitar comandos enquanto recebem alertas).
- Sem threads, um `ReadFile` bloqueante num pipe travaria toda a aplicação.
- Cada placar registado no `central` tem uma thread dedicada para gerir a sua comunicação.

### Como é usado no código?

**Central (`central.c`, `_tmain`):**
```c
hThreads[0] = CreateThread(NULL, 0, threadListener, &g_estado, 0, NULL);
hThreads[1] = CreateThread(NULL, 0, threadUtilizador_central, &g_estado, 0, NULL);
WaitForMultipleObjects(2, hThreads, TRUE, INFINITE); // espera que ambas terminem
```

| Thread | Ficheiro | Função | Responsabilidade |
|---|---|---|---|
| `threadListener` | `central.c` | `threadListener` | Aceita novas ligações de placares via `ConnectNamedPipe` |
| `threadPlacar[N]` | `central.c` | `threadPlacar` | Comunica com um placar específico (uma por placar) |
| `threadUtilizador_central` | `central.c` | `threadUtilizador_central` | Lê comandos do administrador via `_fgetts` |
| `threadUtilizador` | `placar.c` | `threadUtilizador` | Lê comandos do instalador |
| `threadAlertas` | `placar.c` | `threadAlertas` | I/O assíncrono do pipe + temporização do alerta |
| `threadEscuta` | `monitor.c` | `threadEscuta` | Aguarda evento de atualização da SHM |

**Criação dinâmica de thread por placar (`central.c`, `threadListener`):**
```c
ArgThreadPlacar* arg = (ArgThreadPlacar*)malloc(sizeof(ArgThreadPlacar));
arg->estado = estado;
arg->indicePlacar = slot;
HANDLE hThread = CreateThread(NULL, 0, threadPlacar, arg, 0, NULL);
```
O argumento é alocado no heap para evitar problemas de lifetime (a variável local da threadListener ficaria inválida).

**Pergunta típica:** *"Porque é que o arg é alocado com malloc?"*
Resposta: Porque a variável arg tem que sobreviver após o retorno da função que a cria. Se fosse uma variável local da stack da `threadListener`, poderia ser sobrescrita antes de a `threadPlacar` a ler.

---

## 3. Critical Section (Secção Crítica)

### O que é?
Uma `CRITICAL_SECTION` é um primitivo de sincronização leve do Windows para proteger o acesso a dados partilhados entre threads **do mesmo processo**. Funciona como um mutex mas sem ir ao kernel quando não há contenção (mais rápido).

### O que faz?
Garante exclusão mútua: apenas uma thread de cada vez pode estar dentro do bloco protegido por `EnterCriticalSection` / `LeaveCriticalSection`. As outras ficam bloqueadas até a primeira sair.

### Porque e onde usamos?
- O array `estado->placares[]`, os contadores `numPlacares`, `proximoId`, e as flags `terminar`/`ligado` são acedidos por múltiplas threads em simultâneo.
- Sem proteção, poderia haver condições de corrida (race conditions): por exemplo, a `threadListener` a adicionar um placar ao mesmo tempo que a `threadUtilizador_central` percorre o array com `executarListar`.

### Como é usado no código?

**Inicialização e destruição (`central.c`):**
```c
InitializeCriticalSection(&estado->cs);   // em inicializar_central
// ...
DeleteCriticalSection(&estado->cs);       // em libertar_central
```

**Padrão de uso típico (`central.c`, `executarListar`):**
```c
EnterCriticalSection(&estado->cs);
for (int i = 0; i < MAX_PLACARES; i++) {
    if (estado->placares[i].ativo) { /* ... */ }
}
LeaveCriticalSection(&estado->cs);
```

**Regra importante no código:** `atualizarSHM` é sempre chamada **fora** da Critical Section para não segurar o lock desnecessariamente enquanto sinaliza o evento para os monitores.

**Pergunta típica:** *"Qual a diferença entre Critical Section e Mutex?"*
Resposta: A `CRITICAL_SECTION` é intra-processo (mais rápida, sem overhead de kernel quando não há contenção). O Mutex pode ser nomeado e partilhado entre processos. No projeto usamos CS para sincronização interna e um Mutex nomeado (`g_hMutexPipe`) para impedir múltiplas instâncias do `central` com o mesmo pipe.

---

## 4. Mutex Nomeado (Named Mutex)

### O que é?
Um Mutex (Mutual Exclusion) é um objeto de sincronização do kernel do Windows. A versão **nomeada** é acessível por processos distintos através do seu nome.

### O que faz?
Garante que apenas um processo de cada vez detém o mutex. O processo que o cria com `CreateMutex(..., TRUE, nome)` torna-se imediatamente seu detentor. Se outro processo tentar criá-lo e receber `ERROR_ALREADY_EXISTS`, sabe que já existe uma instância a correr.

### Porque e onde usamos?
Para garantir que só existe **uma instância do `central` com um determinado nome de pipe**. Sem este mecanismo, poderiam ser lançados dois `central.exe tubo` em simultâneo, causando conflitos.

### Como é usado no código?

**`central.c`, `inicializar_central`:**
```c
TCHAR nomeMutex[MAX_PATH];
_sntprintf_s(nomeMutex, MAX_PATH, _TRUNCATE, _T("MUTEX_PIPE_%s"), estado->nomePipe);

g_hMutexPipe = CreateMutex(NULL, TRUE, nomeMutex); // TRUE = torna-se dono imediatamente
if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Já existe outra instância -> rejeitar
    CloseHandle(g_hMutexPipe);
    return FALSE;
}
```

**Libertação (`central.c`, `libertar_central`):**
```c
ReleaseMutex(g_hMutexPipe);
CloseHandle(g_hMutexPipe);
```

---

## 5. Overlapped I/O (I/O Assíncrono)

### O que é?
O Overlapped I/O é o modelo de I/O assíncrono do Windows. Em vez de bloquear a thread até a operação (`ReadFile`, `WriteFile`) completar, a chamada retorna imediatamente com `ERROR_IO_PENDING` e a thread pode fazer outras coisas. A conclusão é sinalizada através de um objeto `OVERLAPPED` com um handle de evento.

### O que faz?
Permite que uma thread aguarde simultaneamente por múltiplas fontes de eventos (pipe, timer, etc.) usando `WaitForMultipleObjects`.

### Porque e onde usamos?
- No `placar`, a `threadAlertas` precisa de reagir tanto a mensagens do pipe como ao disparo do waitable timer. Com `ReadFile` bloqueante, a thread ficaria presa à espera do pipe e nunca processaria o timer.
- No `central`, o pipe é criado com `FILE_FLAG_OVERLAPPED` para suportar múltiplas instâncias concorrentes.

### Como é usado no código?

**`placar.c`, `threadAlertas` - iniciar leitura assíncrona:**
```c
ZeroMemory(&ov, sizeof(OVERLAPPED));
ov.hEvent = estado->hPipeEvent;       // evento que será sinalizado quando a leitura completar

BOOL readOk = ReadFile(hPipe, buffer, PIPE_BUFFER_SIZE, NULL, &ov);
// Se retornar FALSE com ERROR_IO_PENDING -> operação pendente, normal
```

**Aguardar eventos múltiplos:**
```c
HANDLE hWaitHandles[2] = { estado->hPipeEvent, estado->hWaitableTimer };
DWORD wait = WaitForMultipleObjects(2, hWaitHandles, FALSE, 500);

switch (wait) {
    case WAIT_OBJECT_0:     // pipe tem dados -> GetOverlappedResult + receberAlerta
    case WAIT_OBJECT_0 + 1: // timer disparou -> fim do alerta
    case WAIT_TIMEOUT:      // 500ms -> verificar flag terminar
}
```

**Obter resultado (`central.c`, `enviarMensagem`):**
```c
OVERLAPPED ov = { 0 };
ov.hEvent = hEvent;
WriteFile(hPipe, dados, tamanho, NULL, &ov);
WaitForSingleObject(hEvent, INFINITE);
GetOverlappedResult(hPipe, &ov, &bytesWritten, FALSE);
```

**Pergunta típica:** *"Porque é que `leituraPendente` é necessário?"*
Resposta: Para não chamar `ReadFile` múltiplas vezes sobre a mesma operação ainda em curso. Se o `WaitForMultipleObjects` retornar `WAIT_TIMEOUT`, não significa que a leitura falhou; a operação continua pendente. Chamar `ReadFile` novamente cancelaria a operação anterior e causaria problemas.

---

## 6. Waitable Timer

### O que é?
Um `WaitableTimer` é um objeto do kernel do Windows que pode ser programado para disparar após um intervalo de tempo, sinalizando um evento que pode ser aguardado com `WaitForSingleObject` ou `WaitForMultipleObjects`.

### O que faz?
No projeto, serve como cronómetro preciso para gerir a duração de um alerta no `placar`. Quando o timer dispara, a `threadAlertas` sabe que o alerta expirou e deve notificar o `central`.

### Porque e onde usamos?
- Alternativa eficiente ao `Sleep` (que bloquearia a thread e impediria receber outros eventos).
- Permite que a mesma thread aguarde o fim do alerta E novas mensagens do pipe em simultâneo.
- O enunciado especifica explicitamente o uso de waitable timer para gerir a duração.

### Como é usado no código?

**Criação (`placar.c`, `inicializar`):**
```c
estado->hWaitableTimer = CreateWaitableTimer(NULL, TRUE, NULL);
// TRUE = manual-reset (dispara uma vez e fica sinalizado)
```

**Armado quando recebe um alerta (`placar.c`, `receberAlerta`):**
```c
LARGE_INTEGER liDueTime;
liDueTime.QuadPart = -(LONGLONG)alerta->duracao * 10000000LL;
// Negativo = relativo ao momento atual, em intervalos de 100ns
// 10.000.000 intervalos de 100ns = 1 segundo

SetWaitableTimer(estado->hWaitableTimer, &liDueTime, 0, NULL, NULL, FALSE);
```

**Cancelamento (`placar.c`, `cancelarAlertaAtivo`):**
```c
CancelWaitableTimer(estado->hWaitableTimer);
```

**Pergunta típica:** *"Porque é que o tempo é negativo e em unidades de 100ns?"*
Resposta: A API `SetWaitableTimer` usa valores negativos para indicar tempo relativo (a partir de agora) e positivos para tempo absoluto (hora do sistema). A unidade é o intervalo de 100 nanossegundos (padrão FILETIME do Windows). Para 5 segundos: `5 * 10.000.000 = 50.000.000` intervalos de 100ns.

---

## 7. Memória Partilhada / Shared Memory (SHM)

### O que é?
A memória partilhada é criada com `CreateFileMapping` (mapeamento de ficheiro respaldado pela pagefile do sistema) e mapeada no espaço de endereçamento de cada processo com `MapViewOfFile`. Processos diferentes que abram o mesmo nome de mapeamento acedem fisicamente à mesma memória RAM.

### O que faz?
Permite que o `central` publique o estado atual dos alertas ativos e que os processos `monitor` o leiam diretamente, sem troca de mensagens. É a forma mais rápida de partilhar grandes volumes de dados entre processos.

### Porque e onde usamos?
- É o mecanismo obrigatório especificado no enunciado para a comunicação Central -> Monitor.
- Eficiente para leitura por múltiplos consumidores (vários monitores podem ler simultaneamente sem sobrecarregar o central).
- O `central` escreve; os `monitor` só leem (permissões diferentes: `FILE_MAP_ALL_ACCESS` vs `FILE_MAP_READ`).

### Como é usado no código?

**Criação no central (`central.c`, `inicializar_central`):**
```c
estado->hFileMapping = CreateFileMapping(
    INVALID_HANDLE_VALUE,   // respaldado pela pagefile (não por ficheiro real)
    NULL,
    PAGE_READWRITE,
    0,
    sizeof(SHM_ALERTA),     // tamanho do bloco
    _T("SHM_PLACARES")      // nome para outros processos acederem
);
estado->pShm = (SHM_ALERTA*)MapViewOfFile(
    estado->hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0
);
ZeroMemory(estado->pShm, sizeof(SHM_ALERTA));
```

**Abertura no monitor (`monitor.c`, `abrirRecursos`):**
```c
estado->hFileMapping = OpenFileMapping(
    FILE_MAP_READ,          // apenas leitura
    FALSE,
    estado->nomeSHM         // "SHM_PLACARES"
);
estado->pShm = (SHM_ALERTA*)MapViewOfFile(
    estado->hFileMapping, FILE_MAP_READ, 0, 0, 0
);
```

**Estrutura partilhada (`common.h`):**
```c
typedef struct {
    struct {
        DWORD identificador;
        DWORD duracao;
        TCHAR msg[MAX_MSG_LEN]; // 140 chars
    } placar[MAX_PLACARES];     // 20 entradas
    BOOL desligar;
} SHM_ALERTA;
```

**Atualização (`central.c`, `atualizarSHM`):**
```c
EnterCriticalSection(&estado->cs);
ZeroMemory(estado->pShm, sizeof(SHM_ALERTA));  // limpar tudo antes
for (int i = 0; i < MAX_PLACARES; i++) {
    if (estado->placares[i].ativo && estado->placares[i].temAlerta) {
        estado->pShm->placar[i].identificador = estado->placares[i].identificador;
        // ...
    }
}
LeaveCriticalSection(&estado->cs);
SetEvent(estado->hEventMonitor);  // notificar monitores (fora da CS)
```

**Pergunta típica:** *"Porque é que não colocamos ponteiros na SHM?"*
Resposta: Os ponteiros contêm endereços virtuais do processo que os criou. Outro processo mapeia a SHM num endereço base diferente do seu espaço de endereçamento, pelo que os ponteiros do primeiro seriam inválidos no segundo. Só se usam tipos de dados com tamanho fixo (DWORD, TCHAR[]).

---

## 8. Evento do Kernel (Named Event)

### O que é?
Um Event é um objeto de sincronização do kernel do Windows com dois estados: sinalizado e não-sinalizado. Pode ser **auto-reset** (volta automaticamente ao estado não-sinalizado após acordar uma thread) ou **manual-reset** (fica sinalizado até ser explicitamente reposto). A versão **nomeada** é acessível entre processos.

### O que faz?
Serve como mecanismo de notificação: o `central` sinaliza o evento (`SetEvent`) sempre que atualiza a SHM; os `monitor` aguardam nesse evento (`WaitForSingleObject`) e, ao serem acordados, releem a SHM e atualizam a interface gráfica.

### Porque e onde usamos?
- Alternativa eficiente ao polling: em vez de o monitor verificar a SHM a cada X ms (desperdiçando CPU), bloqueia à espera do evento e só acorda quando há algo novo.
- O enunciado proíbe explicitamente o uso de polling.
- É **auto-reset** porque só precisa de acordar uma instância de monitor por atualização (evita que múltiplos monitores acordem em cascata de forma incontrolada por SetEvent anterior).

### Como é usado no código?

**Criação no central (`central.c`, `inicializar_central`):**
```c
estado->hEventMonitor = CreateEvent(
    NULL,
    FALSE,   // auto-reset: volta a não-sinalizado após acordar uma thread
    FALSE,   // estado inicial: não-sinalizado
    EVENT_NOTIFICAR  // nome = "notificar"
);
```

**Abertura no monitor (`monitor.c`, `abrirRecursos`):**
```c
estado->hEvento = OpenEvent(
    SYNCHRONIZE,     // apenas esperar, não sinalizar
    FALSE,
    estado->nomeEvento  // "notificar"
);
```

**Espera no monitor (`monitor.c`, `threadEscuta`):**
```c
DWORD wait = WaitForSingleObject(hEvento, INFINITE);
if (wait == WAIT_OBJECT_0) {
    if (estado->pShm->desligar)
        PostMessage(estado->hwndMain, WM_APP_ENCERRAR, 0, 0);
    else
        PostMessage(estado->hwndMain, WM_APP_ATUALIZAR, 0, 0);
}
```

**Sinalização pelo central (`central.c`, `atualizarSHM`):**
```c
SetEvent(estado->hEventMonitor); // acorda a threadEscuta do(s) monitor(s)
```

**Pergunta típica:** *"Porque é que o evento é auto-reset e não manual-reset?"*
Resposta: Com auto-reset, o evento volta automaticamente a não-sinalizado depois de acordar uma thread, o que é o comportamento desejado: cada `SetEvent` acorda exatamente uma iteração de escuta. Com manual-reset seria necessário chamar `ResetEvent` explicitamente para evitar que chamadas futuras a `WaitForSingleObject` retornassem imediatamente sem ter havido atualização.

---

## 9. Registry do Windows

### O que é?
O Registry é uma base de dados hierárquica do Windows que armazena configurações do sistema e de aplicações como pares chave-valor. Acessível via `RegOpenKeyEx`, `RegQueryValueEx`, `RegCreateKeyEx`, `RegSetValueEx`.

### O que faz?
Permite persistir configurações entre execuções do programa. No projeto, o nome do named pipe é guardado na primeira execução e reutilizado nas seguintes sem precisar de passá-lo como argumento.

### Porque e onde usamos?
- O enunciado especifica obrigatoriamente que o nome do pipe deve ser guardado/lido de `HKEY_CURRENT_USER\Software\TrabSO2` com o valor `NPIPE` (tipo `REG_SZ`).
- Simplifica o uso: após a primeira configuração, o instalador não precisa de memorizar o nome do pipe.

### Como é usado no código?

**Escrita (`placar.c`, `guardarNomePipeNoRegistry`):**
```c
HKEY hKey;
RegCreateKeyEx(HKEY_CURRENT_USER, _T("Software\\TrabSO2"), 0, NULL, 0,
               KEY_WRITE, NULL, &hKey, NULL);
RegSetValueEx(hKey, _T("NPIPE"), 0, REG_SZ,
              (const BYTE*)nomePipe,
              (DWORD)((_tcslen(nomePipe) + 1) * sizeof(TCHAR)));
RegCloseKey(hKey);
```

**Leitura (`placar.c`, `lerNomePipeDoRegistry`):**
```c
HKEY hKey;
RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\TrabSO2"), 0, KEY_READ, &hKey);
DWORD tipo = REG_SZ;
DWORD tamanhoBytes = tamanho * sizeof(TCHAR);
RegQueryValueEx(hKey, _T("NPIPE"), NULL, &tipo, (LPBYTE)buffer, &tamanhoBytes);
RegCloseKey(hKey);
```

---

## 10. Interface Gráfica Win32 (Monitor)

### O que é?
A API Win32 nativa do Windows para criar janelas, processar mensagens e desenhar na área de cliente. Funciona com um **message loop** que despacha mensagens para o `WndProc` (callback da janela).

### O que faz?
Apresenta os alertas ativos numa janela gráfica, com suporte a paginação (Page Up/Down) e menu de configuração.

### Porque e onde usamos?
- O enunciado especifica obrigatoriamente interface gráfica Win32 para o monitor.
- O `WndProc` não pode ser bloqueado, por isso as operações de espera são feitas numa thread separada (`threadEscuta`) que comunica com a janela via `PostMessage`.

### Como é usado no código?

**Message loop principal (`monitor.c`, `_tWinMain`):**
```c
while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);   // envia para WndProc
}
```

**Mensagens customizadas para comunicação thread -> janela:**
```c
#define WM_APP_ATUALIZAR (WM_APP + 1)  // nova informação na SHM -> redesenhar
#define WM_APP_ENCERRAR  (WM_APP + 2)  // central encerrou -> fechar monitor

// Enviadas pela threadEscuta (não bloqueiam o WndProc):
PostMessage(estado->hwndMain, WM_APP_ATUALIZAR, 0, 0);
```

**Desenho da lista de alertas (`monitor.c`, `WndProc`, `WM_PAINT`):**
```c
case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    desenharAlertas(hwnd, &g_estado, hdc, &rc);
    EndPaint(hwnd, &ps);
}
```

**Pergunta típica:** *"Porque é que usamos `PostMessage` em vez de `SendMessage` na threadEscuta?"*
Resposta: `SendMessage` bloqueia até o `WndProc` processar a mensagem, o que criaria um potencial deadlock se o `WndProc` tentar aceder ao mesmo estado que a `threadEscuta` protege com a Critical Section. `PostMessage` coloca a mensagem na fila e retorna imediatamente.

---

## 11. Estruturas de Mensagens (Protocolo de Comunicação)

### Resumo do protocolo (`common.h`)

| Tipo | Valor | Direção | Estrutura | Significado |
|---|---|---|---|---|
| `MSG_TIPO_LIGAR` | 1 | Placar -> Central | `MSG_CMD_PC` | Pedido de registo |
| `MSG_TIPO_DESLIGAR` | 2 | Placar -> Central | `MSG_CMD_PC` | Pedido de desligar |
| `MSG_TIPO_FIM_ALERTA` | 3 | Placar -> Central | `MSG_CMD_PC` | Alerta expirou pelo timer |
| `MSG_TIPO_ALERTA` | 4 | Central -> Placar (e confirmação inversa) | `MSG_ALERTA` | Novo alerta com msg + duração |
| `MSG_TIPO_CANCELAR` | 5 | Central -> Placar (e confirmação inversa) | `MSG_CMD_CP` | Cancelar alerta ativo |
| `MSG_TIPO_ENCERRAR` | 6 | Central -> Placar | `MSG_CMD_CP` | Encerrar plataforma |
| `MSG_TIPO_ID` | 7 | Central -> Placar | `MSG_ID` | Atribuição de identificador |

**O primeiro byte de qualquer mensagem é sempre o `tipo`** - permite ao recetor identificar a estrutura completa sem conhecer o tamanho a priori (possível porque o pipe está em modo message).

---

## 12. Fluxo Completo - Sequência de Operações Importantes

### Ligar um placar
1. `placar`: `executarLigar` -> `CreateFile` (liga ao pipe)
2. `placar`: envia `MSG_CMD_PC {tipo=1}` (LIGAR)
3. `central` (`threadPlacar`): recebe tipo 1 -> atribui `proximoId++`, envia `MSG_ID {tipo=7, id=N}`
4. `placar`: recebe `MSG_ID` -> guarda `identificador`, `ligado=TRUE`

### Enviar um alerta
1. `central` (`executarAlerta`): envia `MSG_ALERTA {tipo=4, msg, duracao}` ao pipe do placar
2. `placar` (`threadAlertas`, `receberAlerta`): recebe tipo 4 -> arma `SetWaitableTimer(duracao)` -> mostra alerta na consola -> envia `MSG_ALERTA` de volta como confirmação
3. `central` (`threadPlacar`): recebe tipo 4 de volta -> atualiza `temAlerta=TRUE`
4. `central` (`executarAlerta`): chama `atualizarSHM` -> `SetEvent(hEventMonitor)`
5. `monitor` (`threadEscuta`): acorda -> `PostMessage(WM_APP_ATUALIZAR)` -> `WndProc` redesenha

### Encerrar a plataforma
1. `central` (`executarEncerrar`): envia `MSG_CMD_CP {tipo=6}` a todos os placares -> `pShm->desligar=TRUE` -> `estado->terminar=TRUE` -> `SetEvent(hEventMonitor)`
2. Cada `placar` (`receberAlerta`): recebe tipo 6 -> `terminar=TRUE` -> threads terminam
3. `monitor` (`threadEscuta`): acorda -> `pShm->desligar==TRUE` -> `PostMessage(WM_APP_ENCERRAR)` -> `MessageBox` + `DestroyWindow`

---

## 13. Pontos Críticos e Decisões de Implementação

### Porque o `cancelarAlertaAtivo` é chamado sem confirmação de receção?
No código final (ver comentário `20260521`), o bloco de receção da confirmação do `cancelar` está comentado. A confirmação é recebida assincronamente pela `threadPlacar` (case `MSG_TIPO_CANCELAR`). Esta decisão evita que o `executarCancelar` bloqueie a thread do utilizador à espera de uma confirmação que pode demorar.

### Porque é que `atualizarSHM` é chamada fora da Critical Section para o `SetEvent`?
Para minimizar o tempo com o lock seguro. A escrita na SHM é feita dentro da CS (para consistência), mas o `SetEvent` é chamado depois de liberar a CS para não bloquear outras threads enquanto o kernel processa o evento.

### Porque é que a `threadListener` guarda o `hPipeListener` no estado global?
Para que `executarEncerrar` possa fazer `DisconnectNamedPipe` nesse handle e desbloquear a `ConnectNamedPipe` que está à espera. Sem isto, a `threadListener` ficaria bloqueada para sempre após o encerramento.

### Porque é que o placar usa `FILE_FLAG_OVERLAPPED` no `CreateFile`?
Porque o pipe foi criado pelo central com `FILE_FLAG_OVERLAPPED`. Ambos os lados têm de usar o mesmo modo. Adicionalmente, o placar precisa de I/O overlapped para poder usar `WaitForMultipleObjects` com o pipe e o timer em simultâneo.

---

## 14. Glossário Rápido

| Termo | Definição curta |
|---|---|
| IPC | Inter-Process Communication - comunicação entre processos distintos |
| Handle | Referência opaca a um objeto do kernel (pipe, thread, evento, etc.) |
| Race condition | Comportamento incorreto causado por duas threads acederem dados partilhados sem sincronização |
| Deadlock | Situação em que duas threads se bloqueiam mutuamente à espera uma da outra |
| `TCHAR` | Tipo de caracter genérico: `char` em ANSI, `wchar_t` em Unicode (projeto usa Unicode) |
| `DWORD` | Unsigned 32-bit integer (Windows) |
| `OVERLAPPED` | Estrutura para operações de I/O assíncronas no Windows |
| Pagefile-backed | Memória partilhada não associada a um ficheiro real, mas à memória virtual do sistema |
| Auto-reset event | Evento que volta a não-sinalizado automaticamente após acordar uma thread |
| Manual-reset event | Evento que fica sinalizado até `ResetEvent` ser chamado explicitamente |
