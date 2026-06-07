# Placar Informativo — Sistemas Operativos 2

## Descrição

Projeto em C/Win32 composto por três programas:
- **central**: gere o registo dos placares, distribui alertas e encerra a plataforma;
- **placar**: simula um placar informativo instalado na via;
- **monitor**: apresenta os alertas ativos através de interface gráfica.

A comunicação entre **central** e **placar** é feita por **named pipes**.  
A comunicação entre **central** e **monitor** é feita por **memória partilhada**, com sinalização por **evento nomeado**.

## Requisitos implementados

### 1. Programa central
- Interface em consola para o utilizador “administrador”.
- Comandos implementados:
  - `alerta <msg> <duracao> <id_placar>`
  - `cancelar <id_placar>`
  - `listar`
  - `encerrar`
  - `ajuda`
- Envio de alertas para um placar específico ou para todos (`id_placar = 0`).
- Cancelamento de alertas ativos.
- Listagem dos placares registados e do estado dos respetivos alertas.
- Encerramento controlado da plataforma, notificando os placares e o monitor.
- Atribuição de identificadores únicos sequenciais aos placares, սկսando em `1`.
- Suporte para até **20 placares**.
- Uso de **mutex nomeado** para impedir múltiplas instâncias do central com o mesmo named pipe.
- Atualização da memória partilhada do monitor sempre que existe alteração relevante.

### 2. Programa placar
- Interface em consola para o utilizador “instalador”.
- Comandos implementados:
  - `liga`
  - `desliga`
  - `ajuda`
- Ligação ao central através de named pipe.
- Leitura do nome do named pipe:
  - pela linha de comandos, ou
  - pelo valor `NPIPE` no Registry, em `HKEY_CURRENT_USER\Software\TrabSO2`.
- Escrita do nome do pipe no Registry quando fornecido por linha de comandos.
- Duas threads:
  - uma para interação com o utilizador;
  - outra para receber alertas e gerir a temporização.
- Apresentação dos alertas com data/hora atual.
- Substituição de um alerta ativo por um novo alerta recebido.
- Cancelamento do alerta ativo quando solicitado pelo central.
- Terminação controlada quando o placar é desligado ou quando a plataforma é encerrada.
- Uso de `waitable timer` para controlar a duração de cada alerta.
- Uso de I/O assíncrono/overlapped na comunicação com o central.

### 3. Programa monitor
- Interface gráfica **Win32**.
- Leitura dos alertas ativos a partir de **memória partilhada**.
- Atualização automática da interface através de um **evento nomeado**.
- Lista dos alertas ativos, incluindo:
  - identificador do placar;
  - mensagem;
  - duração.
- Menu com:
  - configuração do número máximo de alertas visíveis;
  - caixa “Acerca”;
  - sair da aplicação.
- Navegação por páginas com **Page Up / Page Down**.
- Encerramento do monitor quando a plataforma é terminada.

## Comunicação entre programas

### Placar ↔ Central
Mensagens implementadas:
- `MSG_TIPO_LIGAR`
- `MSG_TIPO_DESLIGAR`
- `MSG_TIPO_FIM_ALERTA`
- `MSG_TIPO_ALERTA`
- `MSG_TIPO_CANCELAR`
- `MSG_TIPO_ENCERRAR`
- `MSG_TIPO_ID`

### Central ↔ Monitor
Estrutura partilhada implementada:
- `SHM_ALERTA`

## Observações de implementação
- O sistema usa sincronização com `CRITICAL_SECTION` para proteger o estado partilhado.
- A central mantém a informação dos placares ativos e dos respetivos alertas.
- O monitor apenas lê a memória partilhada, não interfere com a lógica dos placares.
- O encerramento é feito de forma controlada, libertando os recursos utilizados.

## Defesa oral
Pretende-se realizar a defesa oral: 
- Gabriel Dinis Paixão Dos Santos - 2024134573 - **Sim**.
- Diogo Simoes Martins - 2024120298 - **Sim**.
