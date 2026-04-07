# Trabalho Laboratorial - SO2 2025/26

## Grupo
- ...
- ...

## Estrutura do projeto

```
placar/
  common.h   - tipos e estruturas partilhadas entre os tres programas
  placar.h   - constantes, estado e prototipos especificos do placar
  placar.c   - implementacao do programa placar
  readme.md  - este ficheiro
```

## Requisitos implementados - 1a Entrega (M1)

- [ ] 1. Recebe comandos do utilizador "instalador" (`ligar` / `desligar`)
- [ ] 2. Le o nome do named pipe da linha de comandos ou do Registry; cria o valor NPIPE no Registry se nao existir
- [ ] 3. Utiliza duas threads (utilizador e alertas)
- [ ] 4. Recepcao de alertas via evento `notificar` + leitura do Registry
- [ ] 5. Gestao da duracao do alerta via waitable timer
