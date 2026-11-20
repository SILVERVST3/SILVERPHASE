# SilverPhase — Multiband Correlation Meter (VST3)

Mede a correlação L/R **por banda** (1/3 ou 1/6 de oitava) e mostra quais frequências estão em anti-fase.

## Compilar no GitHub
1. Crie um repositório (ex.: `SILVERVST3/SilverPhase`) e envie pelo site: `CMakeLists.txt`, `LEIA-ME.md` e a pasta `Source`.
2. O workflow fica numa pasta oculta, que o upload do site não aceita. Crie assim:
   **Add file → Create new file** → no nome digite `.github/workflows/build.yml` → cole o conteúdo de `build.yml.txt` → **Commit**.
3. Aba **Actions** → "Build SilverPhase VST3" → ao terminar, baixe o artefato **SilverPhase-VST3**.
4. Copie `SilverPhase.vst3` para `C:\Program Files\Common Files\VST3\` e faça um rescan no REAPER.

## Como usar
- Coloque na master (ou na faixa suspeita) e toque a música.
- **Barras**: correlação atual de cada banda (+1 em fase, 0 sem relação, −1 anti-fase).
- **Traço laranja**: pior valor desde o último **Limpar**.
- **Lista à direita**: bandas cujo pior valor ficou abaixo do **Limiar**, da pior para a melhor.
- **Espectro Mid x Side**: onde o Side passa do Mid (vermelho) há anti-fase.
- **Clique numa banda** (no gráfico ou na lista) para ouvi-la:
  - *Banda L/R*: a banda em estéreo; ligando **Mono**, você ouve ela cancelar.
  - *Banda Side* / *Banda Mid*: só a parte Side ou Mid daquela banda.
- **Resposta**: tempo de média (curto = mais rápido, longo = mais estável).
- **Congelar**: trava a leitura.
- Abaixo de ~40 Hz a resolução fica limitada pelo tamanho da FFT (8192 pontos).

O medidor não altera o áudio: com "Ouvir" desligado e Mono desligado o sinal passa intacto.

## Seção CORREÇÃO
Liga um corretor de anti-fase por bandas. Ele age **só** nas bandas cuja correlação está abaixo do **Alvo**, e só o mínimo necessário para chegar lá.

- **Rotação de fase** (azul na faixa "Correção aplicada"): gira a fase de L e R, bin a bin, em sentidos opostos. O nível de L e de R não muda, então o pan e o volume de cada lado ficam preservados. Corrige polaridade invertida, atraso entre microfones e efeito Haas.
- **Redução de Side** (laranja): para som difuso (reverb, widener) ou para completar o que a rotação máxima não alcançou. Limitada por **Side máx**, com ajuste fino que mede a própria saída.
- **Modo**: Auto (escolhe por banda), Só fase, Só Side.
- **Intensidade**: quanto do caminho até o alvo percorrer.
- **Velocidade**: tempo de reação da correção.
- **Mono grave**: deixa tudo abaixo da frequência escolhida em mono.
- **Delta**: ouvir só o que está sendo removido.
- Com a correção ligada, as barras mostram o **depois** e o contorno branco mostra o **antes**.
- Latência de 4096 amostras (~85 ms a 48 kHz), informada ao REAPER, que compensa automaticamente. Desligada, a latência volta a zero.
