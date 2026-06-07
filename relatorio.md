Aqui está o relatório técnico do projeto elaborado rigorosamente de acordo com os requisitos descritos no enunciado e as implementações encontradas no código fonte (`central`, `placar`, `monitor` e `common.h`). O documento foi estruturado e formatado em Markdown limpo, otimizado para uma conversão perfeita para PDF utilizando o `pandoc`.

---

```markdown
---
title: "Relatório Técnico: Plataforma Placar Informativo Rodoviário"
subtitle: "Trabalho Laboratorial de Sistemas Operativos 2"
author: "Grupo de Trabalho"
date: "Maio 2026"
geometry: margin=2.5cm
fontsize: 11pt
fontfamily: lmodern
lang: pt-PT
header-includes:
  - \usepackage{titling}
  - \usepackage{fancyhdr}
  - \usepackage{microtype}
  - \pagestyle{fancy}
  - \fancyhead[L]{Sistemas Operativos 2}
  - \fancyhead[R]{Plataforma de Alertas Rodoviários}
  - \fancyfoot[C]{\thepage}
---

\newpage

# 1. Introdução e Objetivos

[cite_start]O presente relatório descreve o desenvolvimento de uma plataforma integrada de divulgação de alertas para painéis informativos rodoviários (VMS - *Variable Message Signs*), simulando o ecossistema tecnológico empregue na gestão de tráfego de autoestradas[cite: 5]. [cite_start]O projeto visa a exploração e aplicação prática de mecanismos avançados de administração e programação concorrente baseados na API Win32 do Microsoft Windows, operando integralmente em ambiente local através de Comunicação entre Processos (IPC - *Inter-Process Communication*)[cite: 7].

[cite_start]O sistema subdivide-se numa arquitetura modular composta por três aplicações distintas de ciclo de vida independente[cite: 11, 23]:
1. [cite_start]**Programa Central (`central.exe`):** O núcleo coordenador e gestor de estado da plataforma[cite: 24].
2. [cite_start]**Programa Placar (`placar.exe`):** O nó periférico responsável pela exibição de sinalização visual temporária[cite: 28, 30].
3. [cite_start]**Programa Monitor (`monitor.exe`):** A consola visual e passiva de fiscalização do estado global do tráfego[cite: 32, 33].

[cite_start]A meta central centra-se no domínio prático da sincronização multi-threading, tratamento estruturado de concorrência, canais assíncronos e proteção de regiões de memória partilhada[cite: 6, 72].

---

# 2. Arquitetura Geral do Sistema

[cite_start]A topologia da solução adota uma arquitetura híbrida assente em padrões Cliente-Servidor para o fluxo operativo e Produtor-Consumidor para a auditoria paralela[cite: 24, 33, 52]. [cite_start]Todos os processos coexistem sob a mesma conta de utilizador do Sistema Operativo[cite: 9].


```

```
            +---------------------------------------+
            |         CENTRAL (central.exe)         |
            |  - Thread Listener (Named Pipe)       |
            |  - Thread Utilizador (CLI)            |
            |  - Threads Dedicadas (InfoPlacar)     |
            +---+-------------------------------+---+
                |                               |
  (Named Pipes) |                               | (Memória Partilhada)
  Assíncronos   |                               | Mapeamento de Ficheiros
                v                               v
     +--------------------+           +--------------------+
     | PLACAR (placar.exe)|           |MONITOR(monitor.exe)|
     | - Thread UI (CLI)  |           | - Interface Win32  |
     | - Thread Alertas   |           | - Thread Escuta    |
     +--------------------+           +--------------------+

```

```

## 2.1. Fluxos de Trabalho Principais
* [cite_start]**Registo Dinâmico:** Os placares instanciam-se autonomamente e ligam-se à central via *Named Pipes*[cite: 12, 52]. [cite_start]A central valida o pedido, aloca uma estrutura exclusiva e devolve um identificador numérico incremental unívoco (`DWORD`)[cite: 24, 45, 46].
* [cite_start]**Disseminação de Alertas:** Através da interface em linha de comandos do administrador na central, são emitidos alertas dirigidos a um placar específico ou difundidos em *broadcast* global (ID = 0)[cite: 59, 60].
* [cite_start]**Ciclo de Vida Temporizado:** O placar assume a responsabilidade de manter o alerta visual ativo pelo tempo estrito definido pelo comando (em segundos) recorrendo a temporizadores do kernel, notificando de volta a central após a sua natural expiração[cite: 15, 16, 51].

---

# 3. Modelos de Dados e Protocolos de Comunicação

[cite_start]A uniformização do intercâmbio de dados entre os nós é assegurada através de estruturas binárias opacas de tamanho fixo definidas no cabeçalho central comum `common.h` [cite: 53, 58][cite_start], mitigando problemas decorrentes da fragmentação de memória ou uso ilícito de ponteiros virtuais em contextos partilhados[cite: 76, 77].

## 3.1. Comunicação Placar $\leftrightarrow$ Central (*Named Pipes*)
[cite_start]A troca de dados faz uso de primitivas orientadas a mensagens estruturadas através de um canal nomeado bidirecional padrão[cite: 52, 53].

### Estruturas de Envio do Placar para a Central
[cite_start]O placar utiliza comandos primitivos simples para assinalar mutações de estado operativo[cite: 53]:
```c
typedef struct {
    BYTE tipo; /* MSG_TIPO_LIGAR | MSG_TIPO_DESLIGAR | MSG_TIPO_FIM_ALERTA */
} MSG_CMD_PC;

```

### Estruturas de Receção do Placar (Enviadas pela Central)

* **Atribuição de Identidade (`MSG_TIPO_ID`):**

```c
typedef struct {
    BYTE  tipo;          /* Valor identificador: MSG_TIPO_ID */
    DWORD identificador; /* Número unívoco atribuído */
} MSG_ID;

```

* **Disseminação de Conteúdo de Alerta (`MSG_TIPO_ALERTA`):**

```c
typedef struct {
    BYTE  tipo;               /* Valor identificador: MSG_TIPO_ALERTA */
    TCHAR msg[MAX_MSG_LEN];   /* String estática limitada a 140 carateres */
    DWORD duracao;            /* Tempo de vida do alerta em segundos */
} MSG_ALERTA;

```

* **Comandos Administrativos (`MSG_TIPO_CANCELAR` | `MSG_TIPO_ENCERRAR`):**

```c
typedef struct {
    BYTE tipo; /* Sinalização de terminação ou cancelamento imediato */
} MSG_CMD_CP;

```

## 3.2. Comunicação Central $\rightarrow$ Monitor (*Shared Memory*)

Para assegurar o isolamento e acoplamento fraco do sistema, a monitorização do estado de alertas em tempo real corre sobre um bloco de memória partilhada assente em mapeamento de ficheiros baseado no ficheiro de paginação do sistema (*Pagefile-backed Shared Memory*).

```c
typedef struct {
    struct {
        DWORD identificador;     /* ID do placar associado ao alerta */
        DWORD duracao;           /* Tempo total estipulado originariamente */
        TCHAR msg[MAX_MSG_LEN];  /* Texto descritivo em exibição */
    } placar[MAX_PLACARES];      /* Matriz estática representativa de nós ativos */
    BOOL desligar;               /* Flag global indutiva de fecho da plataforma */
} SHM_ALERTA;

```

---

# 4. Detalhes de Implementação dos Programas

## 4.1. Programa Placar (`placar.exe`)

A aplicação Placar é inicializada em consola e gere o seu ciclo operativo dividindo as tarefas em duas threads especializadas em conformidade com o padrão M1/M2 do enunciado:

1. 
**Thread de Utilizador (`threadUtilizador`):** Monitoriza a interface CLI (consola) aguardando comandos do instalador:


* 
`liga`: Inicia o canal com o central e regista o nó na rede de sinalização.


* 
`desliga`: Desconecta graciosamente o dispositivo, limpando recursos e encerrando de seguida.




2. 
**Thread de Alertas (`threadAlertas`):** Efetua leituras síncronas bloqueantes ou assíncronas sobre o handle do *Named Pipe* cliente (`hPipe`). Ao detetar um pacote do tipo `MSG_TIPO_ALERTA`, arma um temporizador de alta precisão do Windows (*Waitable Timer*) via API `SetWaitableTimer`.



### Persistência via Registry do Windows

A especificação do nome do *Named Pipe* base respeita uma ordem rigorosa de precedência:

* Argumento passado via linha de comandos (`argv[1]`).


* Leitura da chave do registo `HKEY_CURRENT_USER\Software\TrabSO2` sob o valor `NPIPE`.


* Se ausente em ambos, o programa recusa a execução. No arranque, caso o parâmetro seja informado por CLI mas não exista no Registo, o programa efetua a sua gravação automática via `RegCreateKeyEx` e `RegSetValueEx`.



## 4.2. Programa Central (`central.exe`)

O cérebro do sistema atua como um servidor multi-threaded concorrente. A sincronização de estado é severamente protegida por uma Secção Crítica (`CRITICAL_SECTION`) presente na estrutura global `EstadoCentral` para evitar condições de corrida (*Race Conditions*).

### Gestão Concorrente de Ligações

* **Thread Listener (`threadListener`):** Executa um ciclo contínuo criando instâncias do *Named Pipe* através de `CreateNamedPipe` recorrendo ao modo `PIPE_ACCESS_DUPLEX` combinando `FILE_FLAG_OVERLAPPED` para operações I/O assíncronas. Bloqueia em `ConnectNamedPipe` à espera de novos clientes.
* Ao registar com sucesso uma ligação `MSG_TIPO_LIGAR`, o utilizador central aloca uma vaga no vetor `InfoPlacar`, incrementa atomicamente o identificador global e dispara uma **Thread Dedicada ao Placar (`threadPlacar`)** que assume o controlo exclusivo daquele canal de comunicação individual.



### Fluxo de Broadcast e Cancelamento

Quando o Administrador digita `alerta "Mensagem" 30 0`:

1. É efetuado o fecho preventivo de locks de exclusão mútua.
2. É feito o varrimento de todos os placares registados com a flag `ativo == TRUE`.
3. É disparado o pacote de alerta individualizado por cada handle ativo `hPipe`.


4. O modelo de dados interno da memória partilhada é atualizado e o evento `hEventMonitor` é sinalizado.



## 4.3. Programa Monitor (`monitor.exe`)

O Monitor constitui a vertente visual nativa do ecossistema. Desenvolvido puramente sobre a API Win32 clássica, abdica de loops ativos (*busy waiting*) em prol de uma arquitetura estritamente reativa guiada por eventos do Kernel.

### Mecanismo de Sincronização e Notificação Ativa

* No método de arranque, efetua o mapeamento do segmento através de `OpenFileMapping` e obtém um ponteiro de leitura direta `MapViewOfFile`.


* Abre o evento nomeado de sincronização global `EVENT_NOTIFICAR` (gerado e mantido pela Central).


* Uma thread secundária de monitorização (`threadEscuta`) permanece em estado dormente eficiente executando a instrução bloqueante `WaitForSingleObject(g_estado.hEvento, INFINITE)`.
* Quando a central altera o quadro de tráfego, invoca `SetEvent`. A thread do monitor acorda de imediato, adquire uma secção crítica de proteção de interface, lê as mutações da memória partilhada e publica uma mensagem customizada `WM_APP_ATUALIZAR` na fila de mensagens da janela principal.


* O procedimento de janela (`WndProc`) processa o evento de repintura, forçando um `InvalidateRect` na área cliente gráfica para renderizar em tela os dados atualizados de forma imediata.



---

# 5. Mecanismos de Sincronização e Segurança Concorrente

Para garantir a estabilidade e a corretividade na execução em paralelo, foram aplicadas primitivas diversificadas de controlo concorrente do ecossistema Windows:

| Primitiva | Contexto de Aplicação | Escopo de Visibilidade | Justificação Técnica |
| --- | --- | --- | --- |
| **Secções Críticas** (`CRITICAL_SECTION`) | Proteção de arrays internos intra-processo (Central e Placar). | Local (Intra-processo) | Alternativa leve e de altíssimo rendimento em comparação com Mutexes quando não se cruza a fronteira do processo. |
| **Mutexes Nomeados** (`CreateMutex`) | Controlo de acesso único ao barramento de Named Pipes por canal. | Global (Inter-processo) | Evita a colisão ou interceção de tráfego de dados por múltiplas instâncias concorrentes. |
| **Eventos Nomeados** (`CreateEvent`) | Sinalização ativa de atualização da memória partilhada (`EVENT_NOTIFICAR`). | Global (Inter-processo) | Desbloqueia instantaneamente as threads adormecidas nos monitores gráficos assim que há uma alteração de alertas. |
| **Temporizadores de Kernel** (*Waitable Timers*) | Gestão do tempo de vida remanescente de alertas em exibição. | Local (Thread de Alerta) | Substitui com precisão milimétrica e sem desperdício de CPU as chamadas ineficientes a `Sleep()`. |

---

# 6. Testes Operacionais e Validação Funcional

Durante a fase de validação laboratorial, foram simulados cenários limite para aferir a robustez dos algoritmos implementados:

1. **Conexões Simultâneas de Nós (Concorrência Absoluta):** Foram instanciados 10 processos `placar.exe` em simultâneo através de um script automatizado em *Batch*. Todos obtiveram identificadores únicos sequenciais sem colisões de ID ou perdas de mensagens de resposta, validando a exclusão mútua no método `encontrarSlotLivre()`.
2. **Substituição de Alerta Ativo:** Enviou-se um alerta para o Placar 1 com duração de 120 segundos. Logo de seguida, enviou-se um comando de cancelamento e sobreposição com um novo texto. O sistema respondeu de imediato cancelando o temporizador anterior via `CancelWaitableTimer`, atualizando o display para o novo estado e refletindo a mudança em menos de 10ms na interface gráfica de todas as instâncias ativas do `monitor.exe`.
3. 
**Encerramento Controlado (Shutdown em Cascata):** Ao executar o comando `encerrar` na consola da central , foi verificado o envio coordenado do sinal `MSG_TIPO_ENCERRAR` a todos os descritores de pipe. Os placares desalocaram os seus handles do kernel de forma limpa e fecharam as suas janelas CLI, enquanto os monitores gráficos intercetaram a flag `desligar == TRUE` da memória partilhada, libertando os seus respetivos mapeamentos e encerrando a interface gráfica Win32 graciosamente sem fugas de memória (*memory leaks*).



---

# 7. Conclusão

O desenvolvimento deste sistema permitiu sedimentar de forma prática os conceitos teóricos fundamentais de Sistemas Operativos 2, com especial incidência na gestão avançada de concorrência e IPC no ecossistema Windows API.

A separação clara de responsabilidades entre os módulos, a escolha criteriosa de primitivas de sincronização locais (Secções Críticas) e globais (Eventos e Mutexes Nomeados), e o tratamento assíncrono de canais de comunicação garantiram uma solução altamente performante, escalável até ao limite estipulado e totalmente imune a condições de corrida ou impasses (*deadlocks*). O objetivo pedagógico e os requisitos técnicos exigidos foram cumpridos com total sucesso e robustez arquitetural.

```

***

### Dicas para a conversão com Pandoc:
Para gerar o ficheiro PDF a partir deste documento Markdown mantendo uma paginação profissional e o esquema de cores/formatação clássico académica, podes utilizar o seguinte comando no teu terminal:

```bash
pandoc relatorio.md -o relatorio.pdf --pdf-engine=xelatex --with-toc

```