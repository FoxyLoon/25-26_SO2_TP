# Plataforma de Alertas Rodoviários - "Placar Informativo"

## Descrição do Projeto
Implementação do Trabalho Laboratorial da unidade curricular de Sistemas Operativos 2. O projeto consiste na criação de uma plataforma de distribuição de alertas informativos para utilização em vias rodoviárias. A arquitetura é composta por processos independentes que comunicam através de mecanismos de IPC (Inter-Process Communication) do Windows, simulando um sistema real de gestão de informação rodoviária.

## Arquitetura e Componentes
O sistema é composto por três programas principais:

| Componente | Tipo | Descrição |
|------------|------|-----------|
| **central** | Servidor | Programa gestor. Atribui identificadores únicos aos placares, gere o ciclo de vida das ligações, envia alertas, cancela notificações e encerra a plataforma de forma controlada. |
| **placar** | Cliente | Simula um painel informativo rodoviário. Liga-se à central, recebe e exibe alertas com temporização, e notifica o fim da duração do alerta. |
| **monitor** | Cliente (GUI) | Interface gráfica que monitoriza os alertas ativos via memória partilhada, com atualização em tempo real por eventos do kernel. |

## Pré-requisitos
- Sistema Operativo: Windows (API Win32 obrigatória)
- Compilador: MSVC (Visual Studio) — toolset `v143` (VS 2022) para `central` e `placar`; toolset `v145` para `monitor`
- Bibliotecas: Windows SDK (`windows.h`, `winbase.h`, `winreg.h`)
- Acesso à chave de registo: `HKEY_CURRENT_USER\Software\TrabSO2`

## Construção (Build)

### Visual Studio
Abrir `25-26_SO2_TP.sln` no Visual Studio e compilar (plataforma x64).

### Linha de comandos (MSVC `cl.exe`)
```bash
cl /DUNICODE /D_UNICODE /O2 central.c /link /OUT:central.exe
cl /DUNICODE /D_UNICODE /O2 placar.c /link /OUT:placar.exe
```

### Linha de comandos (GCC/MinGW)
```bash
gcc -DUNICODE -D_UNICODE -O2 central.c -o central.exe -lkernel32 -ladvapi32
gcc -DUNICODE -D_UNICODE -O2 placar.c -o placar.exe -lkernel32 -ladvapi32
```

## Utilização

### 1. Programa Central
Deve ser executado primeiro. Recebe o nome base do Named Pipe como argumento.
```bash
central.exe tubo
```
O nome completo do pipe será `\\.\pipe\tubo`.

### 2. Programa Placar
Pode ser executado em múltiplas instâncias. O nome do pipe pode ser passado por linha de comandos ou lido automaticamente do Registo Windows.
```bash
placar.exe tubo
```
Se omitido, o programa consulta o valor `NPIPE` em `HKCU\Software\TrabSO2`.

### 3. Programa Monitor
Interface gráfica. Não necessita de argumentos.
```bash
monitor.exe
```
Liga-se automaticamente à memória partilhada e ao evento de notificação para exibir os alertas ativos em tempo real.

## Referência de Comandos

### Central (Administrador)
| Comando | Sintaxe | Descrição |
|---------|---------|-----------|
| alerta | `alerta <msg> <duracao_s> <id_placar>` | Envia alerta. `id_placar=0` envia a todos os registados. |
| cancelar | `cancelar <id_placar>` | Cancela o alerta ativo num placar específico. |
| listar | `listar` | Mostra lista de placares ligados, IDs e alertas ativos. |
| encerrar | `encerrar` | Fecha a plataforma e notifica todos os agentes. |
| ajuda | `ajuda` | Mostra esta mensagem. |

### Placar (Instalador)
| Comando | Descrição |
|---------|-----------|
| liga | Estabelece ligação à central, obtém ID único e começa a escutar. |
| desliga | Notifica a central e termina o processo de forma controlada. |
| ajuda | Mostra ajuda específica do placar. |

## Mecanismos de Comunicação e Sincronização
O projeto aplica rigorosamente os conceitos de Sistemas Operativos 2:

| Mecanismo | Utilização no Projeto |
|-----------|-----------------------|
| **Named Pipes** | IPC bidirecional `central` <-> `placar`. Configurados em `PIPE_TYPE_MESSAGE` e `PIPE_READMODE_MESSAGE`. Buffer: 512 bytes. |
| **Memória Partilhada** | `central` <-> `monitor`: `CreateFileMapping` + `MapViewOfFile` com nome `NOME_SHM_PADRAO`. Permite ao monitor ler o estado dos alertas em tempo real. |
| **Eventos do Kernel** | `EVENT_NOTIFICAR` (`"notificar"`): `central` sinaliza com `SetEvent` sempre que há alteração de estado SHM; `monitor` espera com `WaitForSingleObject`. |
| **I/O Assíncrono (Overlapped)** | `placar`: `ReadFile` com `OVERLAPPED` + `HANDLE` de evento para evitar bloqueios na thread de alertas. `central`: `ConnectNamedPipe` bloqueante por slot. |
| **Sincronização de Estado** | `CRITICAL_SECTION` protege o array `placares[]`, contadores e flags de estado partilhadas entre threads. |
| **Temporização** | `CreateWaitableTimer` gerencia a duração dos alertas no placar. O timer é sinalizado quando a duração expira, desencadeando a limpeza e notificação `MSG_TIPO_FIM_ALERTA`. |
| **Gestão de Threads** | `central`: Modelo *Thread-per-Client* (`threadListener` aceita + `threadPlacar` por placar + `threadUtilizador_central` para CLI). `placar`: 2 threads (`threadUtilizador` para E/S consola, `threadAlertas` para I/O + timers). `monitor`: Thread GUI principal + `threadEscuta` (evento-driven). |
| **Registo Windows** | Persistência do nome do pipe via `RegCreateKeyEx`/`RegSetValueEx` e `RegOpenKeyEx`/`RegQueryValueEx`. |

## Estruturas de Dados Principais
Todas definidas em `placar/common.h`:

```c
typedef struct {
    DWORD identificador;     /* ID do placar associado ao alerta */
    DWORD duracao;           /* Tempo total do alerta em segundos */
    TCHAR msg[MAX_MSG_LEN];  /* Texto descritivo (140 caracteres) */
} PlacarAlerta;

typedef struct {
    PlacarAlerta placar[MAX_PLACARES]; /* Array de até 20 placares */
    BOOL desligar;              /* Flag global de encerramento */
} SHM_ALERTA;
```

## Notas e Limitações
- O projeto está estritamente limitado ao ambiente Windows devido ao uso de APIs nativas (`CreateNamedPipe`, `WaitableTimer`, `RegOpenKeyEx`, `CreateFileMapping`, etc.).
- Não é necessária autenticação ou passwords. Todos os processos partilham o mesmo contexto de utilizador.
- O código segue boas práticas de limpeza de recursos (`CloseHandle`, `DeleteCriticalSection`, `free`) e tratamento de erros.
- Máximo de placares suportados: 20 (`MAX_PLACARES`).
- Máximo de comprimento de mensagem: 140 caracteres (`MAX_MSG_LEN`).

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
├── readme.md                  # Este ficheiro
└── relatorio.md               # Relatório técnico
```

## Equipa
- **Membros:** `Diogo Simões Martins - 2024120298` | `Gabriel Dinis Paixão dos Santos - 2024134573`
- **Curso:** Engenharia Informática - DEIS
- **Ano Letivo:** 2025/2026
- **Disciplina:** Sistemas Operativos 2

---
Projeto académico desenvolvido no âmbito da UC de Sistemas Operativos 2. Para dúvidas ou feedback, contactar o docente responsável.