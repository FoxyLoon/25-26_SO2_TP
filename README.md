# Trabalho Laboratorial - SO2 2025/26

# Grupo
2024120298 Diogo Marins
2024134573 Gabriel Santos

## Estrutura do projeto

```
placar/
  common.h   - tipos e estruturas partilhadas entre os tres programas
  placar.h   - constantes, estado e prototipos especificos do placar
  placar.c   - implementacao do programa placar
  readme.md  - este ficheiro
```

# Placar Informativo - Meta 1

Sistemas Operativos 2 - 2025/26  
Engenharia Informática - DEIS

---

## Requisitos implementados

### 1. Argumentos e Registry

- Receção do nome do named pipe via argumento da linha de comandos (`placar.exe <nome_pipe>`).
- Armazenamento do nome do pipe no par nome-valor `NPIPE` (REG_SZ) da chave `HKEY_CURRENT_USER\Software\TrabSO2` do Registry.
- Em caso de omissão do argumento, leitura do nome do pipe a partir do Registry.
- Caso o nome não esteja disponível em nenhuma das duas fontes, o programa termina com mensagem de erro.
- Apresentação na consola do nome do pipe utilizado (`NamedPipe = '<nome>'`).

### 2. Interface com o utilizador (thread de utilizador)

- Leitura contínua de comandos da consola sem bloquear a receção de alertas (duas threads independentes).
- Comando `liga`: gera um identificador aleatório (entre 1 e 100), define o placar como ligado e apresenta o identificador (`Identificador = <id>`). Caso o placar já esteja ligado, informa o utilizador.
- Comando `desliga`: cancela qualquer alerta ativo, define o placar como desligado e termina o programa de forma controlada, libertando todos os recursos. Caso o placar já esteja desligado, informa o utilizador. Porém, se o placar já estiver ligado, desliga o placar e termina o programa.

### 3. Thread de alertas

- Aguarda um evento de reset manual com o nome `notificar` (`WaitForMultipleObjects`).
- Após receção do evento, lê o alerta a partir do par nome-valor do Registry (tipo BINARY, nome igual ao do pipe), guarda-o no estado e apresenta-o na consola com a hora do sistema e o texto da mensagem (`DD/MM/AAAA (HH:MM:SS): '<msg>' (X segundos)`).
- Utiliza um waitable timer para gerir a duração do alerta. Após expirar o tempo, apresenta a mensagem de fim de alerta (`DD/MM/AAAA (HH:MM:SS): '---'`).
- Se um novo alerta for recebido antes de o anterior terminar, o alerta anterior é cancelado e substituído pelo novo.

### 4. Sincronização e gestão de recursos

- Secção crítica (`CRITICAL_SECTION`) para proteger os acessos concorrentes ao estado partilhado entre as duas threads.
- Evento manual-reset (`CreateEvent`) para notificação de novos alertas.
- Waitable timer manual-reset (`CreateWaitableTimer`) para gestão da duração dos alertas.
- Libertação correta de todos os recursos no encerramento (`CloseHandle`, `DeleteCriticalSection`).