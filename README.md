# Plataforma de Alertas Rodoviárias - "Placar Informativo"

## Descrição do Projeto

Implementação do Trabalho Laboratorial da unidade curricular de **Sistemas Operativos 2**. O projeto consiste na criação de uma plataforma de distribuição de alertas informativos para utilização em vias rodoviárias, simulando um sistema real de gestão de informação rodoviária.

A arquitetura é composta por **três processos independentes** que comunicam através de mecanismos de **IPC (Inter-Process Communication)** do Windows, incluindo Named Pipes, Memória Partilhada e Eventos do Kernel.

## Arquitetura e Componentes

O sistema adota uma arquitetura híbrida Cliente-Servidor / Produtor-Consumidor, com os seguintes componentes:

| Componente | Tipo | Descrição | Direção |
|------------|------|-----------|---------|
| **central** | Servidor | Programa gestor. Atribui identificadores únicos, gere ligações, envia/cancela alertas e encerra a plataforma. | — |
| **placar** | Cliente | Simula um painel informativo rodoviário. Liga-se à central, recebe/exibe alertas com temporização, notifica fim de alerta. | ← central |
| **monitor** | Cliente (GUI) | Interface gráfica que monitoriza alertas ativos via memória partilhada, com atualização em tempo real por eventos. | ← central |

### Diagrama de Arquitetura

```
            +---------------------------------------+
            |         CENTRAL (central.exe)         |
            |  - Thread Listener (Named Pipe)       |
            |  - Thread Utilizador (CLI)            |
            |  - Threads Dedicadas por Placar       |
            +---+-------------------------------+---+
                |                               |
  (Named Pipes) |                               | (Memória Partilhada)
  Bidirecionais  |                               | + Evento "notificar"
                v                               v
      +--------------------+           +--------------------+
      | PLACAR (placar.exe)|           | MONITOR (monitor   |
      | - Thread CLI       |           |   .exe)            |
      | - Thread Alertas   |           | - Thread Escuta    |
      |   (Overlapped I/O) |           | - Interface Win32  |
      +--------------------+           +--------------------+
```

### Modelo de Threads

| Processo | Thread | Função |
|----------|--------|--------|
| `central` | `threadListener` | Aceita ligações de placares via Named Pipe |
| `central` | `threadPlacar[N]` | Gerencia comunicação com cada placar individualmente |
| `central` | `threadUtilizador_central` | Interface CLI do administrador |
| `placar` | `threadUtilizador` | Interface CLI do instalador |
| `placar` | `threadAlertas` | I/O assíncrono (overlapped) + temporização de alertas |
| `monitor` | `threadEscuta` | Espera por evento de atualização do SHM |
| `monitor` | `WndProc` | Interface gráfica Win32 |

## Pré-requisitos

- **Sistema Operativo**: Windows (API Win32 obrigatória) — **não é portátil para Linux/macOS**
- **Compilador**: MSVC (Visual Studio) — **toolset `v143` (VS 2022)**
- **Bibliotecas**: Windows SDK (`windows.h`, `winbase.h`, `winreg.h`)
- **Registo Windows**: Acesso a `HKEY_CURRENT_USER\Software\TrabSO2`

## Construção (Build)

### Visual Studio

Abrir `25-26_SO2_TP.sln` no Visual Studio e compilar (plataforma **x64**).

### Linha de comandos (MSVC `cl.exe`)

```bash
cl /DUNICODE /D_UNICODE /Iplacar /O2 placar/placar.c /link /OUT:placar.exe
```

> **Nota**: O suporte a GCC/MinGW mencionado em versões anteriores é **inválido** — as APIs Win32 utilizadas (Named Pipes, WaitableTimers, CreateFileMapping, RegOpenKeyEx, etc.) **não são portáteis**.

## Utilização

> **Ordem de arranque**: central → placar → monitor

### 1. Programa Central

Deve ser executado **primeiro**. Recebe o nome base do Named Pipe como argumento.

```bash
central.exe tubo
```

O nome completo do pipe será `\\.\pipe\tubo`.

### 2. Programa Placar

Pode ser executado em **múltiplas instâncias**. O nome do pipe pode ser passado por linha de comandos ou lido automaticamente do Registo Windows.

```bash
placar.exe tubo
```

Se omitido, o programa consulta o valor `NPIPE` em `HKCU\Software\TrabSO2`. Na primeira execução, grava automaticamente o nome do pipe no registo.

### 3. Programa Monitor

Interface gráfica. **Não necessita de argumentos**.

```bash
monitor.exe
```

Liga-se automaticamente à memória partilhada `"SHM_PLACARES"` e ao evento `"notificar"` para exibir os alertas ativos em tempo real.

## Referência de Comandos

### Central (Administrador)

| Comando | Sintaxe | Descrição |
|---------|---------|-----------|
| `alerta` | `alerta <msg> <duracao_s> <id_placar>` | Envia alerta. `id_placar=0` envia a todos. |
| `cancelar` | `cancelar <id_placar>` | Cancela o alerta ativo num placar. |
| `listar` | `listar` | Mostra placares ligados, IDs e alertas ativos. |
| `encerrar` | `encerrar` | Fecha a plataforma e notifica todos os agentes. |
| `ajuda` | `ajuda` | Mostra esta mensagem. |

### Placar (Instalador)

| Comando | Descrição |
|---------|-----------|
| `liga` | Estabelece ligação à central, obtém ID único e começa a escutar. |
| `desliga` | Notifica a central e termina o processo de forma controlada. |
| `ajuda` | Mostra ajuda específica do placar. |

## Mecanismos de Comunicação e Sincronização

### Named Pipes (Placar ↔ Central)

- **Modo**: `PIPE_TYPE_MESSAGE` / `PIPE_READMODE_MESSAGE`
- **Buffer**: 512 bytes
- **Acesso**: Duplex (`PIPE_ACCESS_DUPLEX`)
- **I/O**: Overlapped (`FILE_FLAG_OVERLAPPED`) no central; `ReadFile` + `WaitForMultipleObjects` no placar
- **Nome**: `\\.\pipe\<pipe_name>`

### Memória Partilhada (Central → Monitor)

- **Nome do SHM**: `"SHM_PLACARES"`
- **Mecanismo**: `CreateFileMapping` + `MapViewOfFile` (pagefile-backed)
- **Escrita**: `atualizarSHM()` no central (linha ~735)
- **Leitura**: `abrirRecursos()` no monitor (linha ~174)

### Eventos do Kernel

| Evento | Utilização | Processo que cria |
|--------|------------|-------------------|
| `"notificar"` (`EVENT_NOTIFICAR`) | Sinaliza alterações no SHM | `central` (`CreateEvent` ~linha 129) |
| `hPipeEvent` | Sinaliza I/O overlapped disponível | `placar` |
| `hWaitableTimer` | Temporização de duração de alertas | `placar` (`CreateWaitableTimer`) |

### Sincronização

| Primitiva | Contexto | Escopo | Justificação |
|-----------|----------|--------|--------------|
| `CRITICAL_SECTION` | Proteção de arrays `placares[]`, contadores, flags | Intra-processo | Alta performance para sincronização local |
| Named Mutex `g_hMutexPipe` | Controlo de acesso ao Named Pipe | Inter-processo | Impede múltiplas instâncias do central com o mesmo pipe |
| Named Event `"notificar"` | Notificação de atualização SHM | Inter-processo | Desbloqueia threads dormentes no monitor |
| Waitable Timer | Duração de alertas no placar | Intra-processo | Precisão milimétrica sem desperdício de CPU |

## Estruturas de Dados Principais

Definidas em **`placar/common.h`**:

### Mensagens (Placar ↔ Central)

```c
/* Placar → Central */
typedef struct { BYTE tipo; } MSG_CMD_PC;   /* MSG_TIPO_LIGAR(1) | DESLIGAR(2) | FIM_ALERTA(3) */

/* Central → Placar */
typedef struct { BYTE tipo; DWORD identificador; } MSG_ID;           /* MSG_TIPO_ID(7) */
typedef struct { BYTE tipo; TCHAR msg[MAX_MSG_LEN]; DWORD duracao; } MSG_ALERTA;  /* MSG_TIPO_ALERTA(4) */
typedef struct { BYTE tipo; } MSG_CMD_CP;   /* MSG_TIPO_CANCELAR(5) | ENCERRAR(6) */
```

### Memória Partilhada (Central → Monitor)

```c
typedef struct {
    struct {
        DWORD identificador;     /* ID do placar associado ao alerta */
        DWORD duracao;           /* Tempo total do alerta em segundos */
        TCHAR msg[MAX_MSG_LEN];  /* Texto descritivo (140 caracteres) */
    } placar[MAX_PLACARES];      /* Array de até 20 placares */
    BOOL desligar;              /* Flag global de encerramento */
} SHM_ALERTA;
```

## Constantes

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `MAX_PLACARES` | 20 | Número máximo de placares suportados |
| `MAX_MSG_LEN` | 140 | Comprimento máximo de mensagem (chars) |
| `EVENT_NOTIFICAR` | `"notificar"` | Nome do evento de atualização SHM |
| `PIPE_BUFFER_SIZE` | 512 | Buffer do Named Pipe (`central/central.h` e `placar/placar.h`) |

## Notas e Limitações

- **Windows apenas**: O projeto utiliza APIs nativas Win32 (`CreateNamedPipe`, `WaitableTimer`, `RegOpenKeyEx`, `CreateFileMapping`, etc.) — **não compila em Linux/macOS**.
- **Sem autenticação**: Todos os processos partilham o mesmo contexto de utilizador.
- **Limpeza de recursos**: O código segue boas práticas (`CloseHandle`, `DeleteCriticalSection`, `free`) e tratamento de erros.
- **Máximo de placares**: 20 (`MAX_PLACARES`).
- **Máximo de comprimento de mensagem**: 140 caracteres (`MAX_MSG_LEN`).
- **Unicode**: Todo o código usa `TCHAR`/`_T()` macros e APIs de caracteres largos (`_tprintf`, `_tcpy_s`, etc.).

## Estrutura do Projeto

```
25-26_SO2_TP/
├── 25-26_SO2_TP.sln          # Solution Visual Studio
├── central/
│   ├── central.c              # Servidor principal
│   ├── central.h              # Cabeçalho da central
│   └── central.vcxproj        # Projeto Visual Studio
├── placar/
│   ├── common.h               # Estruturas e constantes partilhadas
│   ├── placar.c               # Cliente painel informativo
│   ├── placar.h               # Cabeçalho do placar
│   └── placar.vcxproj         # Projeto Visual Studio
├── monitor/
│   ├── monitor.c              # Interface gráfica Win32
│   ├── monitor.h              # Cabeçalho do monitor
│   ├── monitor.rc             # Recurso da janela
│   ├── resource.h             # IDs de recursos
│   └── monitor.vcxproj        # Projeto Visual Studio
├── README.md                  # Este ficheiro
└── relatorio.md               # Relatório técnico detalhado
```

## Equipa

- **Membros**: `Diogo Simões Martins - 2024120298` | `Gabriel Dinis Paixão dos Santos - 2024134573`
- **Curso**: Engenharia Informática - DEIS
- **Ano Letivo**: 2025/2026
- **Disciplina**: Sistemas Operativos 2

---

Projeto académico desenvolvido no âmbito da UC de Sistemas Operativos 2. Para dúvidas ou feedback, contactar o docente responsável.
