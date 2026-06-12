# Perguntas de Defesa — Plataforma de Distribuição de Alertas

> **Como usar:** Este ficheiro contém ~120 perguntas organizadas por tema. Cada pergunta tem uma dificuldade indicada entre parênteses: (Fácil), (Médio), (Difícil). O avaliador (`avaliador_estudos.py`) seleciona perguntas aleatoriamente e apresenta uma de cada vez.

---

## 1. Named Pipes (Comunicação Placar ↔ Central)

### Fácil

1. (Fácil) Qual é o propósito de um Named Pipe no Windows e como se diferencia de um ficheiro normal no sistema de ficheiros?

2. (Fácil) Qual é a função do prefixo `\\\\.\\pipe\\` no nome de um Named Pipe? Porque é necessário?

3. (Fácil) Qual é a diferença entre `PIPE_READMODE_MESSAGE` e `PIPE_READMODE_BYTE`? Que modo é usado neste projeto e porque?

4. (Fácil) O que acontece quando dois processos tentam criar um Named Pipe com o mesmo nome?

### Médio

5. (Médio) Descreve o fluxo completo de criação de uma ligação Named Pipe entre o `central` e um `placar`. Inclui as funções Windows chamadas em cada lado.

6. (Médio) Porque é que o `threadListener` no `central` usa um ciclo `while` em vez de aceitar apenas uma ligação? O que acontece quando o `central` recebe a conexão de um novo `placar`?

7. (Médio) Qual é o papel do `PIPE_BUFFER_SIZE` (512 bytes) definido em `central.h`? O que acontece se uma mensagem for maior que este buffer?

8. (Médio) Explica a diferença entre `CreateFile` (usado pelo `placar` para ligar ao pipe) e `CreateNamedPipe` (usado pelo `central` para criar o servidor). Qual é o papel de `ConnectNamedPipe`?

### Difícil

9. (Difícil) Porque é que o `central` usa `FILE_FLAG_OVERLAPPED` ao criar o Named Pipe? Que implicações tem isto na forma como as operações de leitura/escrita são feitas?

10. (Difícil) No `threadListener`, o handle do pipe é guardado em `estado->hPipeListener` dentro da Critical Section e depois definido como `INVALID_HANDLE_VALUE`. Porque é que esta sequência é necessária e que problema de concorrência resolve?

11. (Difícil) Quando um `placar` se desconecta abruptamente (ex: crash), como é que o `central` deteta esta situação? Descreve o mecanismo de deteção e a sequência de limpeza.

12. (Difícil) Explica o papel do mutex nomeado `g_hMutexPipe` (criado em `inicializar_central`). Como é que ele impede múltiplas instâncias do `central` de usarem o mesmo pipe simultaneamente? Que Windows API é usada e como funciona o mecanismo de `ERROR_ALREADY_EXISTS`?

---

## 2. Memória Partilhada (Central → Monitor)

### Fácil

13. (Fácil) O que é memória partilhada (shared memory) e qual é a sua vantagem em relação a Named Pipes para comunicação entre processos?

14. (Fácil) Que funções Windows são usadas para criar e mapear memória partilhada? Qual é o papel de `CreateFileMapping` vs `MapViewOfFile`?

15. (Fácil) Qual é o nome da memória partilhada usada neste projeto e onde é definido?

16. (Fácil) Porque é que o `monitor` usa `FILE_MAP_READ` enquanto o `central` usa `PAGE_READWRITE` ao aceder à mesma SHM?

### Médio

17. (Médio) Descreve a estrutura `SHM_ALERTA` definida em `common.h`. Que informações contém e porque é que tem um array de `MAX_PLACARES` elementos?

18. (Médio) No `central`, a função `atualizarSHM()` é chamada em vários pontos do código. Explica porque é que esta função é chamada após cada alteração de estado e o que ela faz passo a passo.

19. (Médio) Porque é que o `central` faz `ZeroMemory` na SHM antes de reescrever os dados? O que aconteceria se não limpasse antes de reescrever?

20. (Médio) No `monitor`, a função `abrirRecursos()` abre a SHM e o evento de notificação. Que acontece se a SHM já não existir (ex: `central` terminou)? Como é tratada esta situação?

### Difícil

21. (Difícil) A função `atualizarSHM()` faz `EnterCriticalSection` antes de copiar dados e `LeaveCriticalSection` antes de chamar `SetEvent`. Porque é que o `SetEvent` é feito **fora** da Critical Section? Que problema de desempenho evitaria?

22. (Difícil) O `monitor` lê a SHM na função `desenharAlertas()` também dentro de uma Critical Section. Porque é que esta segunda Critical Section é necessária se o `central` já escreve os dados? Há risco de o `monitor` ler dados inconsistentes?

23. (Difícil) Explica o mecanismo de paginação no `monitor`: como é que `paginaAtual` e `maxAlertas` permitem exibir mais de `maxAlertas` alertas? Qual é o papel das teclas Page Up / Page Down?

24. (Difícil) Quando o `central` termina (comando `encerrar`), escreve `desligar = TRUE` na SHM e chama `SetEvent`. Como é que o `monitor` deteta esta condição e qual é a sequência de ações que executa?

---

## 3. Sincronização (Critical Section, Mutex, Events)

### Fácil

25. (Fácil) O que é uma Critical Section no Windows e qual é a diferença entre `EnterCriticalSection` e `LeaveCriticalSection`?

26. (Fácil) O que é um Event no Windows? Qual é a diferença entre um event manual-reset e auto-reset?

27. (Fácil) Porque é que todos os três programas (`central`, `placar`, `monitor`) usam `CRITICAL_SECTION`? Que tipo de recurso protegem?

28. (Fácil) O que é um Mutex nomeado e como é diferente de uma Critical Section?

### Médio

29. (Médio) No `central`, a variável `g_hMutexPipe` é declarada como `extern` em `central.h`. Como é que outras partes do código (ou outras instâncias) usam este mutex para coordenar o acesso ao Named Pipe?

30. (Médio) No `placar`, a `threadAlertas` e a `threadUtilizador` partilha o `estado->cs`. Que dados partilhados protegem estas duas threads e porque é que a Critical Section é necessária entre elas?

31. (Médio) O evento `hEventMonitor` é criado com `auto-reset` (segundo parâmetro `FALSE` em `CreateEvent`). O que significa auto-reset e porque é adequado para este caso de uso?

32. (Médio) No `monitor`, a `threadEscuta` faz `WaitForSingleObject(hEvento, INFINITE)`. Porque é que o timeout é `INFINITE` e não um valor finito? Que problema resolvem os checks de `terminar` antes e depois do `WaitForSingleObject`?

### Difícil

33. (Difícil) Explica o padrão de uso da Critical Section no `central`: `EnterCriticalSection` → ler/escrever estado → `LeaveCriticalSection`. Porque é que as operações de I/O (como `enviarMensagem`) são feitas **fora** da Critical Section? Que problema de dead lock evitaria?

34. (Difícil) Na função `executarEncerrar`, o `central` faz `terminar = TRUE` **dentro** da Critical Section e depois `SetEvent` **fora**. Porque é esta separação? O que aconteceria se o `SetEvent` fosse dentro da CS?

35. (Difícil) No `placar`, a `threadAlertas` verifica `estado->terminar` dentro do ciclo principal. Quando a `threadUtilizador` define `terminar = TRUE`, como é que a `threadAlertas` é notificada para sair? Há algum mecanismo de despertar ou depende de polling?

36. (Difícil) Compara Mutex nomeado vs Critical Section: em que cenários cada um é apropriado? Porque é que o `central` usa um mutex nomeado (`g_hMutexPipe`) para proteger o pipe mas uma Critical Section para proteger o estado interno?

---

## 4. I/O Assíncrono e Overlapped

### Fácil

37. (Fácil) O que significa I/O sobreposto (overlapped I/O) no Windows? Qual é a vantagem em relação ao I/O síncrono?

38. (Fácil) Qual é o papel da estrutura `OVERLAPPED` nas chamadas `ReadFile` e `WriteFile`? Que campo é essencial?

39. (Fácil) O que representa o código de erro `ERROR_IO_PENDING` retornado por `GetLastError` após uma operação de I/O?

40. (Fácil) Porque é que o `placar` usa `FILE_FLAG_OVERLAPPED` ao chamar `CreateFile` para abrir o Named Pipe?

### Médio

41. (Médio) Descreve o fluxo de `enviarMensagem` no `central`: como é que a função inicia uma escrita assíncrona e depois espera pela sua conclusão? Qual é o papel do `HANDLE hEvent` criado localmente?

42. (Médio) Na `threadAlertas` do `placar`, o código verifica `ERROR_IO_PENDING` após `ReadFile` e define `leituraPendente = TRUE`. Como é que o valor de `leituraPendente` controla o comportamento do loop principal?

43. (Médio) Porque é que a função `enviarMensagem_placar` no `placar` chama `GetOverlappedResult` com o parâmetro `bWait = TRUE`, enquanto a função equivalente no `central` usa `WaitForSingleObject` explícito? Há diferença funcional entre as duas abordagens?

44. (Médio) O que acontece quando o `placar` termina e o `threadAlertas` executa `CancelIo(hPipe)`? Porque é que este cancelamento é necessário e o que aconteceria se não fosse feito?

### Difícil

45. (Difícil) Na `threadAlertas`, o código faz `WaitForMultipleObjects` com `hPipeEvent` e `hWaitableTimer`. Porque é que `WAIT_OBJECT_0` indica que a leitura do pipe completou? Como é que o evento `hPipeEvent` é sinalizado automaticamente pelo sistema operativo quando o I/O sobreposto termina?

46. (Difícil) Explica o bug que a variável `leituraPendente` resolve (documentado no código como "CORRECAO"). O que acontecia antes desta correção? Porque é que o `WAIT_TIMEOUT` do timer causava "milhares de pedidos de leitura sucessivos"?

47. (Difícil) No `central`, as funções `enviarMensagem` e `receberMensagem` criam um `HANDLE hEvent` com `CreateEvent(NULL, TRUE, FALSE, NULL)`. Porque é que o segundo parâmetro é `TRUE` (manual-reset) e não `FALSE` (auto-reset)? Que impacto tem na correção do código?

48. (Difícil) Quando o `placar` recebe uma mensagem do tipo `MSG_TIPO_DESLIGAR` ou `MSG_TIPO_ENCERRAR`, o código define `estado->terminar = TRUE`. Como é que a `threadAlertas` deteta esta alteração no próximo ciclo? Porque é que o `leituraPendente` não interfere com esta deteção?

---

## 5. Waitable Timers (Duração de Alertas)

### Fácil

49. (Fácil) O que é um Waitable Timer no Windows e qual é o seu propósito neste projeto?

50. (Fácil) Que Windows API é usada para criar um Waitable Timer e qual é o papel do segundo parâmetro (bManualReset)?

51. (Fácil) Porque é que o `placar` precisa de um timer para gerir a duração dos alertas? O que acontece quando o timer expira?

52. (Fácil) Qual é a diferença entre `SetWaitableTimer` e `CreateWaitableTimer`? Em que fase de execução cada uma é chamada?

### Médio

53. (Médio) Explica a conversão de segundos para o formato `LARGE_INTEGER` usado por `SetWaitableTimer`: `-(LONGLONG)alerta->duracao * 10000000LL`. Porque é que o valor é negativo e porque é multiplicado por 10000000?

54. (Médio) Quando um novo alerta chega enquanto um alerta anterior está ativo, o código chama `CancelWaitableTimer`. Porque é necessário cancelar o timer anterior antes de configurar o novo?

55. (Médio) Na `threadAlertas`, o `WaitForMultipleObjects` espera por `hWaitableTimer` com um timeout de 500ms. Porque é que o timeout é 500ms e não `INFINITE`? Que informação adicional permite verificar durante este intervalo?

56. (Médio) Quando o timer expira (`WAIT_OBJECT_0 + 1`), o código verifica `estado->temAlerta` dentro da Critical Section antes de enviar `MSG_TIPO_FIM_ALERTA`. Porque é que esta verificação é necessária?

### Difícil

57. (Difícil) Descreve o ciclo completo de vida de um timer neste projeto: desde a criação em `inicializar()`, passando por `SetWaitableTimer` quando um alerta chega, até à expiração e reutilização do timer para um novo alerta.

58. (Difícil) Porque é que `CancelWaitableTimer` é chamado dentro da Critical Section em `cancelarAlertaAtivo` e também na função `receberAlerta`? Há risco de dead lock se o timer estiver sinalizado no momento do cancelamento?

59. (Difícil) Se dois alertas chegarem rapidamente um após o outro, qual é o comportamento correto do timer? Descreve o estado do timer em cada ponto: primeiro `SetWaitableTimer`, segundo `SetWaitableTimer`, expiração, e um terceiro `SetWaitableTimer`.

60. (Difícil) O `Waitable Timer` é criado com `bManualReset = TRUE`. Porque é que este parâmetro é `TRUE` neste caso? O que mudaria se fosse `FALSE`? (Nota: com auto-reset, o timer seria automaticamente não sinalizado após despertar uma thread.)

---

## 6. Protocolo de Mensagens

### Fácil

61. (Fácil) Quais são os tipos de mensagem definidos em `common.h`? Lista os tipos de mensagem de Placar→Central e de Central→Placar.

62. (Fácil) Qual é o tamanho máximo de uma mensagem de alerta (`MAX_MSG_LEN`) e porque é este valor escolhido?

63. (Fácil) Porque é que o primeiro byte de cada mensagem é o campo `tipo`? Que padrão de comunicação este representa?

64. (Fácil) Qual é a estrutura `MSG_ALERTA` e que campos contém? Porque é que o campo `msg` é do tipo `TCHAR` e não `char`?

### Médio

65. (Médio) Descreve o protocolo de handshake quando um `placar` se liga ao `central`: que mensagem o `placar` envia, que mensagem o `central` responde, e qual é o resultado para o `placar`.

66. (Médio) Quando o `central` envia um alerta para um `placar` específico (idPlacar != 0), como é que o `central` encontra o `placar` alvo? Qual é o papel de `encontrarPlacaPorId`?

67. (Médio) O comando `alerta` com `id_placar = 0` envia o alerta a **todos** os `placar` ligados. Descreve o algoritmo de broadcast e o que acontece se um `placar` estiver inativo no momento do envio.

68. (Médio) Quando o `central` executa `executarCancelar`, o código comentado mostra uma versão com confirmação de receção. Porque é que esta confirmação foi removida? Qual é a diferença entre o comportamento atual e o comportamento comentado?

### Difícil

69. (Difícil) Explica o problema potencial de enviar uma mensagem `MSG_ALERTA` (que contém um array de `MAX_MSG_LEN` chars) via Named Pipe sem qualquer mecanismo de serialização. Porque é que este enfoque funciona neste projeto?

70. (Difícil) O `placar` envia de volta a estrutura `MSG_ALERTA` ao `central` quando recebe um alerta (ver `receberAlerta`). Porque é que o `central` precisa de receber esta confirmação? Que informação adicional o `central` extrai disto?

71. (Difícil) O comando `encerrar` envia `MSG_TIPO_ENCERRAR` para todos os `placar` ativos. Depois, define `estado->terminar = TRUE` e fecha o pipe listener. Porque é que a ordem destas operações é importante? O que aconteceria se o `terminar` fosse definido antes de enviar as mensagens?

72. (Difícil) Descreve o ciclo de vida completo de uma mensagem de alerta: desde o comando `alerta` no CLI do `central`, passando pela escrita no pipe, leitura no `placar`, configuração do timer, expiração do timer, e a mensagem `MSG_TIPO_FIM_ALERTA` de volta ao `central`.

---

## 7. Arquitetura de Processos e Threads

### Fácil

73. (Fácil) Quantos processos compõem esta plataforma? Quais são os seus papéis?

74. (Fácil) Quantas threads tem cada processo? Qual é o papel de cada thread?

75. (Fácil) Porque é que o `central` precisa de uma thread dedicada para aceitar ligações (`threadListener`) em vez de fazer isso na thread principal?

76. (Fácil) Cada `placar` ligado ao `central` tem uma thread dedicada (`threadPlacar`). Porque é que esta thread é criada por placar e não todas partilhadas numa thread pool?

### Médio

77. (Médio) Descreve o ciclo de vida do `central`: como é iniciado, como cria e gere as suas threads, e como é encerrado limpoamente.

78. (Médio) O `placar` tem duas threads: `threadUtilizador` (CLI) e `threadAlertas` (I/O). Porque é que estas funcionalidades não podem ser combinadas numa única thread?

79. (Médio) O `monitor` é uma aplicação GUI Win32. Porque é que usa `threadEscuta` para esperar pelo evento de notificação e não faz o `WaitForSingleObject` diretamente na thread principal do GUI?

80. (Médio) Quando o `central` termina (ex: `encerrar`), como é que todas as threads dos `placar` são notificadas e terminam? Descreve a sequência de eventos desde o comando até ao término de cada processo.

### Difícil

81. (Difícil) O `central` usa `WaitForMultipleObjects(2, hThreads, TRUE, INFINITE)` no `main`. Porque é que o terceiro parâmetro é `TRUE` (wait-all) e não `FALSE` (wait-any)? Que implicações tem para o comportamento do `central`?

82. (Difícil) Na `threadPlacar`, o código faz `free(arg)` imediatamente após extrair os parâmetros. Porque é que o `arg` foi alocado com `malloc` no `threadListener`? O que aconteceria se não fosse libertado?

83. (Difícil) O `monitor` usa a mensagem personalizada `WM_APP_ATUALIZAR` para notificar a GUI quando a SHM muda. Porque é que não pode desenhar diretamente na thread `threadEscuta`? Qual é o papel de `InvalidateRect` e `WM_PAINT` neste fluxo?

84. (Difícil) Explica o mecanismo de encerramento do `monitor`: quando a SHM tem `desligar = TRUE`, como é que a `threadEscuta` notifica a GUI, como a GUI reage, e como os recursos são libertados? Porque é que `fecharRecursos` faz `SetEvent` no evento antes de esperar pela thread?

---

## 8. Win32 API e GUI (Monitor)

### Fácil

85. (Fácil) O que é uma janela Win32 e quais são os elementos principais necessários para criar uma (WndClass, CreateWindow, etc.)?

86. (Fácil) Qual é o papel da função `WndProc` numa aplicação Win32? Que tipo de mensagens processa?

87. (Fácil) Porque é que o `monitor` usa `GetMessage` e `DispatchMessage` num ciclo? O que acontece se não houver mensagens na fila?

88. (Fácil) O que é `TextOut` e `DrawText` na Win32 API? Como é que o `monitor` as usa para desenhar os alertas?

### Médio

89. (Médio) Explica o papel de `WM_PAINT` no `monitor`: quando é que esta mensagem é gerada, o que é que `InvalidateRect` faz para a gerar, e porque é que `desenharAlertas` é chamada dentro dela?

90. (Médio) O `monitor` usa `WM_APP_ATUALIZAR` como mensagem personalizada. Porque é que não pode usar uma das mensagens Win32 predefinidas? Qual é o intervalo de valores reservado para mensagens personalizadas?

91. (Médio) A `DlgProcConfig` é um dialog procedure. Porque é que `DialogBox` cria um diálogo modal? O que acontece ao `GetMessage` loop principal enquanto o diálogo está aberto?

92. (Médio) O `monitor` usa `TCHAR` e macros como `_tcscpy_s`, `_sntprintf_s`, `_tprintf`. Qual é o propósito destas macros e como é que o caracter set (Unicode vs ANSI) afeta o comportamento?

### Difícil

93. (Difícil) Descreve o fluxo completo de atualização visual no `monitor`: evento sinalizado → `threadEscuta` post `WM_APP_ATUALIZAR` → `WndProc` recebe → `InvalidateRect` → `WM_PAINT` → `desenharAlertas` → `TextOut`. Porque é que este padrão é necessário em vez de desenhar diretamente na thread do evento?

94. (Difícil) Na função `desenharAlertas`, o código faz `EnterCriticalSection` antes de ler `estado->pShm->placar[]`. Porque é que esta Critical Section é necessária se a SHM é escrita pelo `central`? Há garantia de que os dados lidos são consistentes?

95. (Difícil) O `monitor` usa `WS_OVERLAPPEDWINDOW` ao criar a janela. Quais são as capacidades que este estilo confere? Porque é que `CW_USEDEFAULT` é usado para a posição inicial?

96. (Difícil) Quando o utilizador muda a configuração (nome da SHM, nome do evento, maxAlertas) no diálogo, o código chama `fecharRecursos` e depois `abrirRecursos`. Porque é que é necessário reconectar os recursos em vez de apenas atualizar os campos do estado?

---

## 9. Registry (Configuração)

### Fácil

97. (Fácil) O que é o Registry do Windows e qual é o seu papel neste projeto?

98. (Fácil) Que chaves e valores do Registry são usados pelo `placar`? Qual é o caminho da chave e o nome do valor?

99. (Fácil) Porque é que o `placar` guarda o nome do pipe no Registry? Que problema resolve esta decisão de design?

100. (Fácil) Quais são as funções Windows usadas para ler e escrever no Registry?

### Médio

101. (Médio) Descreve o fluxo de inicialização do `placar` em relação ao Registry: se o pipe name é passado como argumento, se é lido do Registry, e o que acontece em cada caso.

102. (Médio) Porque é que o `placar` usa `HKEY_CURRENT_USER` em vez de `HKEY_LOCAL_MACHINE` para guardar o nome do pipe? Quais são as implicações de segurança e de multi-utilizador?

103. (Médio) O `placar` usa `RegCreateKeyEx` para guardar o nome do pipe. Qual é a diferença entre `RegCreateKeyEx` e `RegOpenKeyEx`? Porque é que `RegCreateKeyEx` é apropriado aqui?

104. (Médio) O que acontece se o `placar` for iniciado sem argumento e o Registry não contiver o valor `NPIPE`? Qual é a sequência de erros?

### Difícil

105. (Difícil) Porque é que o `placar` usa `_tcscpy_s` e `_tcslen` para manipular strings em vez de `strcpy` e `strlen`? Que segurança adicional estas funções fornecem?

106. (Difícil) Explica o papel de `REG_SZ` como tipo de valor no Registry. Que outros tipos de valor existem e porque é que `REG_SZ` é o correto para o nome do pipe?

107. (Difícil) O `monitor` não usa o Registry — ele tem valores por defeito em `monitor.h`. Porque é que esta diferença de design existe entre `placar` e `monitor`? Que trade-off representa?

108. (Difícil) Se o `placar` fosse executado por múltiplos utilizadores no mesmo Windows, cada um com a sua própria instância, como é que o uso de `HKEY_CURRENT_USER` afeta a partilha do pipe name? É um problema ou uma vantagem?

---

## 10. Cenários de Falha e Encerramento

### Fácil

109. (Fácil) O que acontece quando o utilizador escreve `encerrar` no CLI do `central`? Qual é a sequência de ações?

110. (Fácil) Porque é que o `central` fecha o `hPipeListener` no `executarEncerrar`? O que aconteceria se não o fechasse?

111. (Fácil) Quando o `monitor` deteta `desligar = TRUE` na SHM, que mensagem mostra ao utilizador?

112. (Fácil) O que acontece se um `placar` tenta ligar-se mas o `central` não está iniciado? Como é que o `placar` deteta esta situação?

### Médio

113. (Médio) Descreve o mecanismo de encerramento do `central`: como é que o `terminar` flag é propagado, como as threads são notificadas, e como os recursos são libertados em `libertar_central`.

114. (Médio) Quando o `placar` recebe `MSG_TIPO_ENCERRAR`, define `estado->terminar = TRUE`. Como é que a `threadAlertas` deteta esta flag e qual é a sequência de limpeza?

115. (Médio) Porque é que o `monitor` faz `WaitForSingleObject(estado->hThreadEscuta, 3000)` em `fecharRecursos` com um timeout de 3 segundos? O que aconteceria se a thread não terminasse nesse tempo?

116. (Médio) Quando um `placar` chama `desliga`, envia `MSG_TIPO_DESLIGAR` ao `central`. Como é que o `central` reage e como é que o `placar` sabe que a remoção foi confirmada?

### Difícil

117. (Difícil) Descreve o cenário de falha: o `central` crasha enquanto vários `placar` estão ligados e alertas estão ativos. O que acontece a cada `placar`? A SHM fica num estado inconsistente? O `monitor` mostra dados obsoletos?

118. (Difícil) Quando o `central` executa `executarEncerrar`, define `pShm->desligar = TRUE` antes de `terminar = TRUE`. Porque é que esta ordem é importante? O que aconteceria se a ordem fosse invertida?

119. (Difícil) O `central` usa `WaitNamedPipe` com timeout de 5 segundos no `executarLigar`. Porque é que este timeout é necessário? O que acontece se o `central` estiver a iniciar e o `placar` tentar ligar antes do pipe estar pronto?

120. (Difícil) Descreve o cenário completo de encerramento ordenado: `central` envia `MSG_TIPO_ENCERRAR` → cada `placar` recebe e termina → `central` liberta recursos → `monitor` deteta `desligar = TRUE` e fecha. Porque é que esta sequência garante que nenhum recurso fica órfão?
