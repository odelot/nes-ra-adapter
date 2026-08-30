# NES RA Adapter Project
[![en-us](https://img.shields.io/badge/lang-en--us-red.svg)](https://github.com/odelot/nes-ra-adapter/blob/main/README.md)

Repositório do projeto **NES RetroAchievements Adapter** – uma iniciativa maker estilo "faroeste cyberpunk" que transforma o console físico do NES (lançado em 1985) ao adicionar conectividade à Internet e a funcionalidade de conquistas, suportada pela incrível comunidade do RetroAchievements.

<p align="center">
  <a href="https://www.youtube.com/watch?v=SEKSnkoNz1k">
    <img width="70%" src="https://github.com/odelot/nes-ra-adapter/blob/main/images/video.png">
  </a>
</p>

**Agora você não precisa abrir seu console!!!**

<p align="center">  
    <img width="70%" src="https://github.com/odelot/nes-ra-adapter/blob/main/images/version1.0.jpg">  
</p>

---

## Índice

- [Aviso](#aviso)
- [Introdução](#introdução)
- [Getting Started (Como Construir)](#getting-started--como-construir)
- [Arquitetura e Funcionamento](#arquitetura-e-funcionamento)
- [Limitações](#limitações)
- [Informações Gerais](#informações-gerais)
- [Histórico de Versões](#histórico-de-versões)
- [Contribuições](#contribuições)
- [Licença](#licença)
- [FAQ](#faq)
- [Agradecimentos](#agradecimentos)
- [Projetos Semelhantes](#projetos-semelhantes)

---

## Aviso

O projeto não se responsabiliza pelo uso ou danos que ele possa eventualmente causar. Construa e use por sua conta e risco. Este é um **PROTÓTIPO** funcional, mas ainda precisa evoluir para uma versão final – com hardware revisado, extensivamente testado e facilmente replicável. Queremos que todos possam construir e usar a partir deste protótipo, mas só se aventure se souber o que está fazendo.

---

## Introdução

O **NES RA Adapter** transforma seu console NES original em uma máquina interativa conectada à Internet, permitindo o desbloqueio de conquistas em tempo real através da plataforma RetroAchievements. Com inspiração no Game Genie e um toque de faroeste cyberpunk, o adaptador:
- **Identifica o jogo** lendo o cartucho (através do cálculo de CRC).
- **Monitora a memória** do console para detectar eventos e conquistas.
- **Fornece conectividade e interface** por meio de uma tela TFT, buzzer e configuração via smartphone.

### Organização do repositório

- **nes-pico-firmware**: código fonte do firmware usado no Raspberry PI Pico (placa roxa chinesa).
- **nes-esp-firmware**: código fonte do firmware usado no ESP32 C3 Super Mini
- **hardware**: schematics e arquivos Gerber das placas v1.0 (NES 72 pinos pelo GH e Famicom 60 pinos pelo mi213), além das PCBs dos protótipos v0.1 e v0.2 usadas durante o desenvolvimento
- **3d parts**: arquivos STL das peças modeladas para o case idealizado, com inspiração no Game Genie (designs para NES 72 pinos e Famicom 60 pinos)
- **misc**ellaneous: arquivos e ferramentas auxiliares (Web App de espelhamento do LCD / criador do mapa de CRC32<->RA Hashes / lambda que encolhe response do RA / config do TFT)

---

## Getting Started / Como Construir

### Adquira as partes

#### Placa 1

| Nome da Parte                                                     | Quantidade |
|-------------------------------------------------------------------|------------|
| NES 72-pin slot*                                                  | 1x         |
| SN74LVC1G3157DBVR                                                 | 26x        |
| Raspberry Pi Pico (purple board)                                  | 1x         |
| Flat cable 20cm 1mm reverse 5pin                                  | 1x         |
| FFC & FPC Connectors 1.0 FFC Non ZIF SMT 5 pin - MOLEX 52808-0570 | 1x         |
| PCB set                                                           | 1x         |
| Electrolytic capacitor 10µF 16V                                   | 3x         |

\* para a placa do Famicom, use um slot de 60 pinos

#### Placa 2 - PTH (furo passante)

| Nome da Parte                                                     | Quantidade |
|-------------------------------------------------------------------|------------|
| ESP32 C3 Supermini PLUS (with external antenna)                   | 1x         |
| 1.3 inch LCD 240x240 ST7789 with 7 pins                           | 1x         |
| Passive Buzzer 16 ohms ac/2khz + Transistor BC548                 | 1x         |
| FFC & FPC Connectors 1.0 FFC Non ZIF SMT 5 pin - MOLEX 52808-0570 | 1x         |
| PCB set                                                           | 1x         |
| Electrolytic capacitor 10µF 16V                                   | 1x         |
| 1k ohm resistor (R1)                                              | 1x         |
| 10k ohm resistor (R2)                                             | 1x         |
| Push Button Switch 6x6x6                                          | 1x         |
| 90º degree female header pins (to connect the LCD 7 pins)         | 1x         |
| 100 ohm resistor (R3 and R4) - opcional                           | 2x         |
| Bi-color LED (Green / Red, Common Cathode) - opcional             | 1x         |

#### Placa 2 - SMD (componentes de montagem em superfície)

Esta é a placa 2 alternativa, projetada pelo mi213, é a exigida pelos cases 3D dele. Veja a BOM com os componentes SMD [aqui](<hardware/Famicom - v1.0 - by mi213/BOM-board2-famicom-v1.0 - by mi213.csv>) (a mesma placa 2 é usada pelas versões NES e Famicom).

#### Peças 3d

| Nome da Parte                                                     | Quantidade |
|-------------------------------------------------------------------|------------|
| Peças impressas em 3d                                             | 1x         |
| M2 screw 6mm flat head with nut                                   | 10x        |
| M2 screw 10mm cylinder head                                       | 4x         |
| Insertos de latão M2 de 3 mm (3,2 mm OD) - para o design do mi213 | 4x         |

Os arquivos STL estão em [3d-parts/STL/](3d-parts/STL/), organizados por console e por autor do design:
  - [NES - 72pin / 20250911 - odelot design](<3d-parts/STL/NES - 72pin/20250911 - odelot design>) → cases front-loader e top-loader para a placa 2 de furo passante
  - [NES - 72pin / 20260811 - mi213 design](<3d-parts/STL/NES - 72pin/20260811 - mi213 design>) → cases front-loader e top-loader, exigem a placa 2 SMD do mi213
  - [Famicom - 60pin / 20260811 - mi213 design](<3d-parts/STL/Famicom - 60pin/20260811 - mi213 design>) → case do Famicom, exige a placa 2 SMD do mi213

Os insertos de latão só são necessários nos designs do mi213.

### Monte o circuito

A partir da versão 1.0, graças às placas personalizadas do GH, você não precisa mais abrir o console para usar o adaptador. Com o case 3D que eu projetei, o adaptador funciona como um GameGenie, é só encaixar na gaveta de cartuchos e pronto. Também está disponível um case específico para o modelo top-loader.

Desde 2026-08 também existe uma **versão Famicom (60 pinos) das placas e designs de case melhorados, feitos pelo mi213**, valeu! ❤️ Os arquivos de hardware estão organizados assim:
  - [hardware/NES v1.0 - by GH/](<hardware/NES v1.0 - by GH>) → Gerbers das placas 1 e 2 do NES 72 pinos
  - [hardware/Famicom - v1.0 - by mi213/](<hardware/Famicom - v1.0 - by mi213>) → Gerbers das placas 1 e 2 do Famicom 60 pinos, mais a BOM SMD da placa 2
  - [hardware/schematics/](hardware/schematics/) → schematics das placas 1 e 2
  - [hardware/prototypes/](hardware/prototypes/) → as PCBs de desenvolvimento v0.1 / v0.2 (mantidas apenas como referência)

**Vídeo Guia de Montagem (com timecode)** - [https://www.youtube.com/watch?v=4uHbj2ckqv0](https://www.youtube.com/watch?v=4uHbj2ckqv0)
<br/>&nbsp;&nbsp;&nbsp;&nbsp; **\\-** Assista como o Pico precisa ser soldado para evitar problemas com o case impresso em 3D (altura do pico pode impactar a centralização da placa).
<br/>&nbsp;&nbsp;&nbsp;&nbsp; **\\-** Eu imprimi meu case usando PLA, com três paredes e preenchimento variando de 30% a 100%. Use suportes para o case da Placa 2.

O adaptador é composto por duas PCBs conectadas por um cabo flat:
  - **Placa principal**: possui 26 chaves analógicas SMD (a parte mais desafiadora da solda), um Raspberry Pi Pico RP2040 roxo, o slot de cartucho, conector do cabo flat e alguns capacitores de acoplamento.
  - **Placa secundária**: inclui um ESP32 C3 Supermini, display LCD ST7789, buzzer, transistor, capacitor de acoplamento, resistores de 10k e 1k, além de um LED bicolor opcional (verde/vermelho) com dois resistores de 100 ohms, que também pode ser usado como alternativa ao LCD.

**Sobre a espessura da placa de circuito impresso (NES 72 pinos)** - As placas de circuito impresso dos cartuchos de NES têm uma espessura de 1,2 mm. A placa do GameGenie tem uma espessura de 1,6 mm. Após alguns testes, aqui está o resultado:
  - **NES Torradeira**: peça a placa de circuito impresso com uma espessura de 1,6 mm, como a GameGenie.
  - **NES Top Loader**: peça a placa de circuito impresso com uma espessura de 1,2 mm, como a de um cartucho de NES original.

Tudo o que você precisa para montar está disponível no repositório:
  - **/hardware** → esquema e arquivos Gerber para produção das PCBs (NES e Famicom)
  - **/3d-parts** → arquivos STL para os cases front-loader, top-loader e Famicom

### Atualize os firmwares do Pico e ESP32

1. Baixe os firmwares na sessão *Releases*
   
2. **Raspberry Pi Pico** - ligue o Pico no computador via USB com o botão BOOTSEL da placa do Pico apertado. Uma pasta abrirá em seu Desktop. Copie o arquivo `nes-pico-firmware.uf2` para essa pasta. Este [guia](https://www.youtube.com/watch?v=os4mv_8jWfU) ajuda a entender o processo.

3. **ESP32 C3 Supermini** - Você precisará instalar o esptool. Aqui está um [guia](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html#installation). Se você estiver usando o Arduino IDE com ESP32, essa ferramenta já está instalada.
   - Primeiro, identifique a porta COM atribuída ao ESP32 quando conectado via USB (neste exemplo, é COM11).
   - Abra o prompt de comando, navegue até a pasta nes-esp-firmware dentro dos arquivos da release e execute os seguintes comandos (substitua a porta COM conforme necessário). No Linux/macOS, use `esptool.py` (ou `python -m esptool`) e uma porta como `/dev/ttyUSB0`:

```
esptool.exe --chip esp32c3 --port "COM11" --baud 921600  --before default_reset --after hard_reset write_flash  -z --flash_mode keep --flash_freq keep --flash_size keep 0x0 "nes-esp-firmware.ino.bootloader.bin" 0x8000 "nes-esp-firmware.ino.partitions.bin" 0xe000 "boot_app0.bin" 0x10000 "nes-esp-firmware.ino.bin" 

esptool.exe --chip esp32c3 --port COM11 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x210000 ra_rash_map.bin
```

O segundo comando grava o mapa CRC32↔hash do RA (`ra_rash_map.bin`) na partição LittleFS, no offset `0x210000` (2162688 em decimal).

## Arquitetura e Funcionamento

O adaptador utiliza dois microcontroladores trabalhando em conjunto:

<p align="center">
  <img width="80%"  src="images/architecture-v1.4.svg"/>
</p>

### Raspberry Pi Pico

- **Identificação do Jogo:** Lê parte do cartucho para calcular o CRC e identificar o jogo.
- **Monitoramento de Memória:** Utiliza dois núcleos para monitorar o barramento e processar escritas usando a biblioteca rcheevos:
  - **Core 0:** Calcula o CRC, executa rotinas do rcheevos e gerencia a comunicação serial com o ESP32.
  - **Core 1:** Usa quatro state machines do PIO (sem DMA): uma captura exatamente uma amostra do barramento por ciclo de escrita da CPU e mantém espelhos estáticos da RAM do NES e da SRAM do cartucho; uma observa a CPU buscando o vetor de NMI ($FFFA/$FFFB) para marcar o início exato do vblank; uma observa o vetor de RESET ($FFFC/$FFFD) para detectar reset do console; e uma roda um timer de frame que, junto com as escritas em $4014 (OAM DMA), serve de fallback para jogos que rodam com NMI desabilitado. A cada vblank, o core 1 tira um snapshot atômico dos espelhos de memória e sinaliza o core 0 para processar um frame do rcheevos.
- **Observação:** Durante o carregamento do jogo, o Pico reserva ~100KB de RAM para a resposta com a lista de conquistas; o buffer é reduzido assim que o jogo começa.

### ESP32

- **Conectividade e Interface:** Fornece Internet ao NES e gerencia uma tela TFT para exibir conquistas, além de um buzzer para efeitos sonoros. 
- **Gerenciamento de Configuração:** Armazena credenciais de Wi-Fi e RetroAchievements na EEPROM; configuração é feita via smartphone conectado ao ponto de acesso do ESP32. 
- **Sistema de Arquivos e Comunicação:** Utiliza LittleFS para armazenar uma tabela de hash (CRC32 para MD5) para identificação de cartuchos e imagens dos jogos. 
- **Transmite o conteúdo da LCD para uma Web App** Utiliza WebSocket e mDNS para servir uma página web que espelha o conteúdo da telinha LCD e exibe eventos de conquistas adicionais que não cabem na tela física. (Mais detalhes disponíveis na pasta `misc`) O web app fica disponível assim que a imagem do jogo aparece na LCD e pode ser acessado em `http://nes-ra-adapter.local`, certifique-se de que seu smartphone esteja conectado à mesma rede Wi-Fi.

---

## Limitações

- **Tamanho da Resposta do Servidor**: Durante o carregamento do jogo, o Pico reserva ~100KB de RAM para a resposta do RetroAchievements. O ESP32 limpa a resposta enquanto ela ainda está sendo baixada, então payloads brutos maiores que isso são suportados (o FF3, por exemplo, sai de 129KB brutos para ~83KB limpos). Antes de enviar o set para o Pico, o ESP32 também estima o pico de heap que o Pico vai precisar para carregá-lo; quando a estimativa passa do orçamento (~168KB), ele descarta primeiro o rich presence e depois as conquistas mais caras, conquistas marcadas como *progression* ou *win_condition* são sempre mantidas, para que o jogo ainda possa ser terminado. O código-fonte de uma função AWS Lambda também está disponível (pasta misc) caso você prefira encolher a resposta do lado do servidor.

- **Limitação rara: alguns jogos podem ser impossíveis de Masterizar** Uma única conquista com definição extremamente grande pode exceder a RAM do Raspberry Pi Pico ao ser expandida pela biblioteca rcheevos (uma conquista do Final Fantasy, sozinha, expande para ~140KB de estruturas em tempo de execução, mais do que o RP2040 consegue comportar junto com todo o resto). Até agora isso afeta FF1 e FF3, no FF3, 8 conquistas precisam ser descartadas para o set caber, então dá para terminar o jogo, mas não masterizá-lo. Uma futura revisão de hardware baseada no RP2350 (520KB de RAM) removerá esse limite.

- **Placar de líderes desativado** Para reduzir os dados entre o ESP32 e o Pico, o recurso de placar de líderes está desativado. Agora que a detecção de frames é precisa (v1.4), reativar os placares de líderes está no roadmap.


---

## Informações Gerais

- **Consumo Energético**: O adaptador consome tipicamente 0.105A (máximo ~0.220A). A tela LCD consome cerca de 0.035A. O consumo está dentro do limite do regulador de tensão 7805 presente no NES <br/>

- **Conversão de Níveis Lógicos (5V vs 3.3V)**: Nos protótipos, não usamos conversão de nível lógico para evitar atrasos nos sinais que possam comprometer o funcionamento. O barramento entre o cartucho e o NES está desativado quando o cartucho é identificado (não comprometendo o NES), e os níveis de corrente estão dentro dos limites do Pico. Durante a leitura do barramento, todas as portas ligadas ao barramento estão em modo leitura e são 5v tolerantes. Na versão final, o GH pretende limitar a corrente para reduzir o stress no Pico. Se alguém da comunidade identificar a necessidade de conversão de nível e desenvolver um esquema de teste sem comprometer os sinais, contribuições são bem-vindas.

---

## Histórico de Versões

- **Versão 1.4 (2026-08-30)**
  - Reescrita completa do motor de monitoramento do barramento no Pico: o PIO agora captura exatamente uma amostra por ciclo de escrita da CPU (substituindo o oversampling de ~10x e a heurística de valor estável), o DMA e seus buffers ping-pong foram removidos (~16KB de RAM liberados), e o core 1 drena os FIFOs do PIO diretamente, com latência muito menor.
  - Detecção precisa de vblank: uma state machine dedicada do PIO observa a CPU buscando o vetor de NMI ($FFFA/$FFFB), marcando o início exato do vblank, validado em hardware real a 60,099Hz com jitter de ±5µs. Jogos que nunca fazem OAM DMA (ex.: Mike Tyson's Punch-Out!!) agora são precisos frame a frame, em vez de dependerem de frames simulados. Escritas em $4014 e um timer permanecem como fallbacks para jogos que rodam com NMI desabilitado.
  - Detecção de RESET do console via vetor de RESET ($FFFC/$FFFD): apertar o botão de reset do console agora reinicia o estado de runtime do rcheevos (contadores de hits, indicadores), da mesma forma que os emuladores fazem, sem precisar desligar e ligar.
  - Snapshots de RAM verdadeiramente atômicos: os snapshots de memória são tirados no início do vblank com estacionamento de escritas, garantindo consistência pontual para a avaliação das conquistas.
  - Correção de bugs conhecidos (gerenciamento de credenciais).
- **Versão 1.3 (2026-05-15)**
  - Grande otimização no uso de RAM do Pico: o buffer circular de memória foi substituído por espelhos estáticos da RAM do NES e da SRAM do cartucho, com snapshots atômicos tirados durante o OAM DMA.
  - O buffer serial agora é alocado dinamicamente: ~100KB durante o download da lista de conquistas (antes 32KB), reduzido após o carregamento do jogo, suportando sets de conquistas muito maiores.
  - Inclui as correções lançadas na build 1.2-alpha.
- **Versão 1.2-alpha (2026-05-04)** - Nova heurística de detecção de frames e correção do problema de leitura de cartucho que afetava o Castlevania.
- **Versão 1.1 (2026-02-01)** - 
  - Grande otimização no uso de RAM do ESP32 (mais de 40% de economia), aumentando o tamanho máximo do payload e permitindo suporte a mais jogos, incluindo Super Mario Bros. 3, que havia deixado de ser compatível na versão 1.0 após receber novas conquistas no final de 2025.
  - Adicionado um indicador de progresso no canto superior esquerdo da tela (conquistas obtidas / conquistas totais) e um indicador de intensidade do sinal Wi-Fi no canto superior direito.
  - Implementada uma rotina de celebração quando um jogo é totalmente masterizado.
  - Correção de pequenos bugs. Agora não é mais necessário desligar e ligar o console após configurar o adaptador.
- **Versão 1.0 (2025-09-11)** - Nova versão miniaturizada da placa e do case 3d - não é necessário abrir o console. Pequenas mudanças na heuristica de detecção de frames. LED Bicolor (opcional) caso queira substituir a tela LCD. Opção de inverter o conteudo da tela LCD durante a compilação do firmware do ESP32 (necessario para a montagem do case)
- **Versão 0.7 (2025-07-09)** - Mudanças na heuristica de detecção de frame, usando microsegundos ao invés de milesegundos e fazendo frame skip, se necessário, para ficar o mais proximo possivel da cadencia de 60hz (ou 50hz, ao manipular um DEFINE durante a compilação).
- **Versão 0.6 (2025-06-25)** - Correção de bug na comunicação serial no ESP32, recurso "show password" para credenciais do RA durante a configuração, filtragem de algumas conquistas diretamente na API do RA.
- **Versão 0.5 (2025-06-24)** - Modo hardcore ativado. LED de status no formato de semáforo (verde, amarelo, vermelho) para tornar o LCD opcional. Pequenas correções de bugs.
- **Versão 0.4 (2025-05-15)** – Firmware do Pico mais estável quando ligada a funcionalidade experimental de espelhamento da tela no smartphone. Além disso, um app Android foi disponibilizado (APK, na sessão de release) para auxiliar no uso da nova funcionalidade. 
- **Versão 0.3 (2025-04-24)** – Melhorias no tratamento de erros, otimização do uso de RAM e implementação de uma funcionalidade experimental para transmitir o conteúdo da telinha LCD para um smartphone.
- **Versão 0.2 (2025-04-07)** – Otimizações para redução do consumo energético. 
- **Versão 0.1 (2025-03-28)** – Versão inicial do protótipo.
---

## Contribuições

Todos estão convidados a colaborar com o projeto!  
- Envie suas melhorias e pull requests via GitHub.  
- Participe do nosso canal no Discord: [https://discord.com/invite/6eYGq7NNfF](https://discord.com/invite/6eYGq7NNfF)  
- Ajude-nos [comprando um café](https://buymeacoffee.com/nes.ra.adapter) ou pelo [Patreon](https://www.patreon.com/RetroAchievementsHardwareLab).

---

## FAQ

- **P: Vocês pretendem vender o adaptador?**
- **R**: Não. Nosso projeto é uma iniciativa DIY 100% open-source para a comunidade.
<br/>

- **P: Como posso conseguir o adaptador?**
- **R**: É um projeto DIY – você mesmo pode construí-lo!
Se você não se sente confortável em fazê-lo sozinho, lembre-se de que, assim como outros projetos open-source (como [GBSControl](https://github.com/ramapcsx2/gbs-control), [GB Interceptor](https://github.com/Staacks/gbinterceptor) e [Open Source Cartridge Reader](https://github.com/sanni/cartreader)), a própria comunidade pode se organizar para fabricar e comercializar o adaptador. Além disso, a pessoa que modifica seus consoles é perfeitamente capaz de montar um para você.
<br/>

- **P: Preciso modificar meu NES para utilizar o adaptador?**
- **R**: Não, ele é plug-and-play e não requer modificações.
<br/>

- **P: O adaptador precisa estar conectado a um computador?**
- **R**: Não! Ele é autossuficiente, contando com conectividade Wi-Fi integrada. A configuração pode ser feita via smartphone, e todo o processamento dos achievements é realizado internamente.
<br/>

- **P: O adaptador funcionará com everdrives?**
- **R**: Não. O adaptador precisa ler o cartucho para identificá-lo e, quando usado com everdrive, ele lê o firmware do everdrive e não o jogo. Essa é uma das razões pelas quais não é possível suportar everdrive.
<br/>

- **P: O adaptador funcionará com modelos japoneses?**
- **R**: Sim, o mi213 projetou uma versão Famicom (60 pinos) das placas e um case 3D correspondente (veja as pastas `hardware` e `3d-parts`). Nós mesmos ainda não testamos em um console japonês original. A versão de NES também funciona com adaptadores de 72 para 60 pinos: jogos japoneses já fazem parte do mapeamento do RetroAchievements e já tivemos sucesso em testes com adaptações similares.
<br/>

- **P: O adaptador funcionará com Disk System?**
- **R**: Não testamos com Disk System, pois não dispomos desse hardware. Essa funcionalidade pode ser explorada futuramente, assim que conseguirmos o conjunto necessário para testes.
<br/>

- **P: O adaptador funciona com os famiclones brasileiros?**
- **R**: Testado com sucesso no ProSystem-8 Baby

---

## Licença

- O código fonte desse projeto é distribuído sob a licença **GPLv3**.
- Os desenhos das placas (PCBs) são distribuidos sob a linceça **CC-BY-4.0**.
- Os modelos 3d são distribuidos sob a licença **CC-BY-NC-4.0**.

---

## Agradecimentos

- Agradecemos ao **RetroAchievements** e a toda a sua comunidade por criar conquistas para quase todos os jogos e disponibilizar o sistema para todos. [https://retroachievements.org/](https://retroachievements.org/)
- Agradecemos também ao **NESDEV** por documentar o NES com detalhes incríveis e pelo fórum acolhedor. [https://nesdev.org/](https://nesdev.org/)
- Agradecemos ao **GH** pelas placas v1.0 de NES, que tornaram possível o adaptador plug-and-play, e ao **mi213** pelas placas v1.0 de Famicom e pelos designs de case 3D melhorados.
- Galera que nos ajudou durante a campanha de doação em Abril de 2025 - **Muito Obrigado**:
  - Daniel P.
  - Ricardo S. S.
  - Fabio H. A. F.
  - Anuar N.
  - Bryan D. S.
  - Giovanni M. C.
  - Paulo H. A. S.
  - Fernando G. S.
  - Rafael B. M.
  - Thiago G. O.
  - Jerome V. V.
  - Ricardo V. A. M.
  - Tiago T.
  - Leonardo P. K.
  - Fábio S.
  - Anderson A. B.
  - Rubens M. P.
  - Silvio L.
  - Carlos R. S.
  - Ademar S. J.
  - Marcel A. B. C.
  - Peterson F. I.
  - Denis D. F. F.
  - Luis A. S.
  - Ariovaldo P.
  - Theo M. O. C. P.
  - Thiago P. L.
  - André R.
  - Aaron P.
  - Stupid C. G.
  - Elaine B.
  - I F. S.
  - Ken G.
  - Kaffe S.
  - Jonathan
  - Sharon L.

## Projetos Semelhantes

- **RA2SNES**: [https://github.com/Factor-64/RA2Snes](https://github.com/Factor-64/RA2Snes) - RA2Snes é um programa desenvolvido em C++ e C usando Qt 6.7.3, que atua como uma ponte entre o servidor web QUsb2Snes e o cliente rcheevos, permitindo o desbloqueio de conquistas em um Super Nintendo real através da porta USB do SD2Snes.
