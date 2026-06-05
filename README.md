# Plataforma de Alertas Rodoviários - "Placar Informativo"

## Descrição do Projeto
Implementação do Trabalho Laboratorial 1 da unidade curricular de Sistemas Operativos 2. O projeto consiste na criação de uma plataforma de distribuição de alertas informativos para utilização em vias rodoviárias. A arquitetura é composta por processos independentes que comunicam através de mecanismos de IPC (Inter-Process Communication) do Windows, simulando um sistema real de gestão de informação rodoviária.

## Arquitetura e Componentes
O sistema é composto por três programas principais:
- **central**: Programa servidor/gestor. Responsável por atribuir identificadores únicos aos placares, gerir o ciclo de vida das ligações, enviar alertas, cancelar notificações e encerrar a plataforma de forma controlada.
- **placar**: Programa cliente. Simula um painel informativo. Liga-se à central, recebe e exibe alertas com temporização, e notifica o fim da duração do alerta.
- **monitor**: Programa cliente (M3). Interface gráfica que monitoriza os alertas ativos via memória partilhada.
  *(Nota: Este componente não está incluído neste repositório, conforme a entrega incremental do trabalho. A estrutura de dados SHM_ALERTA e o protocolo de memória partilhada encontram-se definidos em common.h para a próxima entrega.)*

## Pré-requisitos
- Sistema Operativo: Windows (API Win32 obrigatória)
- Compilador: MSVC (Visual Studio) ou MinGW-w64
- Bibliotecas: Windows SDK (windows.h, winbase.h, winreg.h)
- Acesso à chave de registo: HKEY_CURRENT_USER\Software\TrabSO2

## Construção (Build)
Recomenda-se a compilação com suporte a Unicode (`/D UNICODE /D _UNICODE`).

**Exemplo com MSVC (`cl.exe`):**
```bash
cl /DUNICODE /D_UNICODE /O2 central.c /link /OUT:central.exe
cl /DUNICODE /D_UNICODE /O2 placar.c /link /OUT:placar.exe
```

**Exemplo com GCC/MinGW:**
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
| **I/O Assíncrono (Overlapped)** | `placar`: `ReadFile` com `OVERLAPPED` + `HANDLE` de evento para evitar bloqueios na thread de alertas. `central`: `ConnectNamedPipe` bloqueante por slot, mas com gestão assíncrona de leitura. |
| **Sincronização de Estado** | `CRITICAL_SECTION` protege o array `placares[]`, contadores e flags de estado partilhadas entre threads. |
| **Temporização** | `CreateWaitableTimer` gerencia a duração dos alertas no placar. O timer é sinalizado quando a duração expira, desencadeando a limpeza e notificação `MSG_TIPO_FIM_ALERTA`. |
| **Gestão de Threads** | `central`: Modelo *Thread-per-Client* (`threadListener` aceita + `threadPlacar` por placar). `placar`: 2 threads (`threadUtilizador` para E/S consola, `threadAlertas` para I/O + timers). |
| **Registo Windows** | Persistência do nome do pipe via `RegCreateKeyEx`/`RegSetValueEx` e `RegOpenKeyEx`/`RegQueryValueEx`. |

## Notas e Limitações
- O projeto está estritamente limitado ao ambiente Windows devido ao uso de APIs nativas (`CreateNamedPipe`, `WaitableTimer`, `RegOpenKeyEx`, etc.).
- A comunicação com o `monitor` (memória partilhada) encontra-se definida no enunciado e nas estruturas `common.h`, mas a sua implementação gráfica e lógica de atualização aguarda a entrega da M3.
- Não é necessária autenticação ou passwords. Todos os processos partilham o mesmo contexto de utilizador.
- O código segue boas práticas de limpeza de recursos (`CloseHandle`, `DeleteCriticalSection`, `free`) e tratamento de erros básico conforme especificado.
- Máximo de placares suportados: 20 (`MAX_PLACARES`).

## Equipa
- **Membros:** `Diogo Simões Martins - 2024120298` | `Gabriel Dinis Paixão dos Santos - 2024134573`
- **Curso:** Engenharia Informática - DEIS
- **Ano Letivo:** 2025/2026
- **Disciplina:** Sistemas Operativos 2

---
Projeto académico desenvolvido no âmbito da UC de Sistemas Operativos 2. Para dúvidas ou feedback, contactar o docente responsável.