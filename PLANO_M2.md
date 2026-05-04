# Plano de Trabalho - Meta 2 (M2)
**Entrega:** 10 de maio  
**Grupo:** 2 pessoas  
**Novos ficheiros:** `central.h`, `central.c`  
**Ficheiros modificados:** `placar.h`, `placar.c`

---

## O que muda face a M1

Na M1 o placar simulava a receção de alertas via Registry e evento local.
Na M2 a comunicação é real e bidirecional via **named pipes** (modo message).

- O **central** e um novo programa que cria o pipe servidor e gere todos os placares.
- O **placar** deixa de usar o evento `notificar` e o Registry para alertas
  e passa a ler mensagens diretamente do named pipe.

---

## Pessoa A - Programa central (`central.h` + `central.c`)

### Dia 1-2: Estruturas e inicializacao
- Ler e compreender `central.h` (já fornecido): `EstadoCentral`, `InfoPlacar`, `ArgThreadPlacar`.
- Implementar `inicializar_central()`:
  - Verificar `argv[1]`; construir `nomePipeCompleto = "\\.\pipe\" + argv[1]`.
  - Inicializar `CRITICAL_SECTION`, `proximoId = 1`, `numPlacares = 0`, `terminar = FALSE`.
- Implementar `libertar_central()`:
  - Fechar todos os `hPipe` dos slots ativos.
  - `DeleteCriticalSection`.
- Implementar `obterDataHoraAtual_central()` (igual ao placar).
- Implementar `encontrarSlotLivre()` e `encontrarPlacaPorId()`.

### Dia 3-4: Thread listener e thread por placar
- Implementar `threadListener()`:
  - Loop: `CreateNamedPipe` + `ConnectNamedPipe` (bloqueante).
  - Ao ligar um cliente: encontrar slot livre, preencher `InfoPlacar`.
  - Criar `ArgThreadPlacar` com `malloc` e lançar `threadPlacar`.
- Implementar `threadPlacar()`:
  - Loop de leitura com `receberMensagem()`.
  - `MSG_TIPO_LIGAR (1)`: atribuir ID, responder com `MSG_ID`.
  - `MSG_TIPO_DESLIGAR (2)`: confirmar, chamar `removerPlacar()`, terminar thread.
  - `MSG_TIPO_FIM_ALERTA (3)`: limpar `temAlerta` no slot.
  - Erro de pipe: `removerPlacar()` e terminar.
- Implementar `removerPlacar()`.
- Implementar `enviarMensagem()` e `receberMensagem()` (wrappers de `WriteFile`/`ReadFile`).

### Dia 5-7: Thread do utilizador e testes
- Implementar `threadUtilizador_central()` (loop de leitura da consola).
- Implementar `processarComando_central()` (parse da linha com `_tcstok_s`).
- Implementar `executarAlerta()`, `executarCancelar()`, `executarListar()`, `executarEncerrar()`.
- Testar com o placar da pessoa B:
  - Ligar/desligar um placar.
  - Enviar alerta, verificar eco de confirmacao.
  - Cancelar alerta.
  - Encerrar plataforma.

---

## Pessoa B - Alteracoes ao placar (`placar.h` + `placar.c`)

### Dia 1-2: Ligacao ao pipe e inicializacao
- Ler o novo `placar.h`: campo `hPipe` e `hPipeEvent` adicionados a `EstadoPlacar`.
- Implementar em `inicializar()`:
  - Construir `nomePipeCompleto`.
  - Criar `hPipeEvent = CreateEvent(NULL, TRUE, FALSE, NULL)` (overlapped).
- Implementar em `libertar()`:
  - Fechar `hPipe` e `hPipeEvent`.
- Implementar `enviarMensagem_placar()` e `receberMensagem_placar()`.

### Dia 3-4: executarLigar e executarDesligar
- Implementar `executarLigar()` com comunicacao real:
  - `CreateFile` para abrir o pipe (com `FILE_FLAG_OVERLAPPED`).
  - `SetNamedPipeHandleState` para modo message.
  - Enviar `MSG_CMD_PC {MSG_TIPO_LIGAR}`.
  - Receber `MSG_ID` e mostrar `"Identificador = %lu"`.
- Implementar `executarDesligar()` com comunicacao real:
  - Enviar `MSG_CMD_PC {MSG_TIPO_DESLIGAR}`.
  - Aguardar confirmacao.
  - Setar `ligado = FALSE`, `terminar = TRUE`.

### Dia 5-7: threadAlertas com overlapped I/O e testes
- Implementar o loop de `threadAlertas()`:
  - `ReadFile` com `OVERLAPPED` (hEvent = `hPipeEvent`).
  - `WaitForMultipleObjects([hPipeEvent, hWaitableTimer], FALSE, 500)`.
  - Ao chegar mensagem do pipe: `GetOverlappedResult` + `ResetEvent` + `receberAlerta()`.
  - Ao expirar o timer: `apresentarFimAlerta()`, limpar `temAlerta`, enviar `MSG_TIPO_FIM_ALERTA`.
- Implementar `receberAlerta()`:
  - `MSG_TIPO_ALERTA (4)`: eco + mostrar + armar timer.
  - `MSG_TIPO_CANCELAR (5)`: eco + `cancelarAlertaAtivo()` + `apresentarFimAlerta()`.
  - `MSG_TIPO_ENCERRAR (6)`: setar `terminar = TRUE`.
- Testar em conjunto com o central da pessoa A.

---

## Pontos de integracao (ambos)

| Momento | O que validar |
|---------|---------------|
| Fim dia 2 | `central.exe tubo` arranca sem erros; `placar.exe tubo` arranca sem erros |
| Fim dia 4 | `liga` no placar -> ID aparece em ambas as consolas |
| Fim dia 6 | `alerta`, `cancelar`, `encerrar` funcionam corretamente ponta a ponta |
| Dia 7    | Testes com 2+ placares em simultaneo; limpeza de memoria e recursos |

---

## Notas importantes

- O pipe e criado em **modo message** (`PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE`),
  o que significa que cada `WriteFile` corresponde exatamente a um `ReadFile`.
  Nao ha necessidade de framing manual das mensagens.
- A leitura continua em `threadAlertas` usa **overlapped I/O** para nao bloquear
  o thread e ao mesmo tempo poder aguardar o `hWaitableTimer`.
  As leituras pontuais em `executarLigar`/`executarDesligar` podem ser sincronas.
- O `hPipeEvent` (evento overlapped do placar) substitui o `hEventNotificar` da M1,
  que pode ser removido do estado.
- Proteger SEMPRE os acessos ao array `placares[]` do central com a `CRITICAL_SECTION`.
- Quando `executarEncerrar` e chamado no central, o central nao aguarda confirmacao
  dos placares (conforme o enunciado). Cada placar termina ao receber `MSG_TIPO_ENCERRAR`.
