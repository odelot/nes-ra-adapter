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
- [Próximos Passos](#próximos-passos)
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
- **hardware**: schematics para construir sua versão em protoboard / placa perfurada e arquivos das PCBs dos protótipos v0.1 e v0.2
- **3d parts**: arquivos do fusion 360 e STL das peças modeladas para o case idealizado, com inspiração no Game Genie
- **misc**ellaneous: arquivos e ferramentas auxiliares (Web App de espelhamento do LCD / criador do mapa de CRC32<->RA Hashes / lambda que encolhe response do RA / config do TFT)

---

## Getting Started / Como Construir

### Adquira as partes

| Nome da Parte                                         | Quantidade | Custo (USD)*         |
|-------------------------------------------------------|------------|----------------------|
| 72-pin slot                                           | 1x         | ~ $1.25              |
| SN74LVC1G3157DBVR                                     | 26x        | ~ $3.25              |
| Raspberry Pi Pico (purple board)                      | 1x         | ~ $3.00              |
| ESP32 C3 Supermini                                    | 1x         | ~ $2.50              |
| LCD 240x240 driver ST7789                             | 1x         | ~ $2.75              |
| Buzzer 3V + Transistor BC548                          | 1x         | ~ $1.25              |
| Flat cable 20cm 1mm reverse 5pin                      | 1x         | ~ $0.20              |
| M2 screw 6mm flat head with nut                       | 10x        | ~ $0.30              |
| M2 screw 10mm                                         | 4x         | ~ $0.15              |
| FPC FFC SMT SMD Flat Cable Connector 1mm Pitch 5 pins | 2x         | ~ $0.30              |
| PCB set                                               | 1x         | ~ $2.00              |
| **Total** (without 3D case)                           |            | ~ **$17.00**         |

\* Fonte: Aliexpress - 2025-03-29 <br/>

### Monte o circuito

A partir da versão 1.0, graças às placas personalizadas do GH, você não precisa mais abrir o console para usar o adaptador. Com o case 3D que eu projetei, o adaptador funciona como um GameGenie — é só encaixar na gaveta de cartuchos e pronto. Também está disponível um case específico para o modelo top-loader.

O adaptador é composto por duas PCBs conectadas por um cabo flat:
  - **Placa principal**: possui 26 chaves analógicas SMD (a parte mais desafiadora da solda), um Raspberry Pi Pico RP2040 roxo, o slot de cartucho, conector do cabo flat e alguns capacitores de acoplamento.
  - **Placa secundária**: inclui um ESP32 C3 Supermini, display LCD ST7789, buzzer, transistor, capacitor de acoplamento, resistores de 10k e 1k, além de um LED bicolor opcional (verde/vermelho) com dois resistores de 100k, que também pode ser usado como alternativa ao LCD.

Tudo o que você precisa para montar está disponível no repositório:
  - **/hardware** → esquema e arquivos Gerber para produção das PCBs
  - **/3d-parts** → arquivos STL para os cases das versões front-loader e top-loader

### Atualize os firmwares do Pico e ESP32

1. Baixe os firmwares na sessão *Releases*
   
2. **Raspberry Pi Pico** - ligue o Pico no computador via USB com o botão BOOTSEL da placa do Pico apertado. Uma pasta abrirá em seu Desktop. Copie o arquivo pico.uf2 para essa pasta.

3. **ESP32 C3 Supermini** - Você precisará instalar o esptool. Aqui está um guia. Se você estiver usando o Arduino IDE com ESP32, essa ferramenta já está instalada.
   - Primeiro, identifique a porta COM atribuída ao ESP32 quando conectado via USB (neste exemplo, é COM11).
   - Abra o prompt de comando, navegue até a pasta nes-esp-firmware dentro dos arquivos da release e execute os seguintes comandos (substitua a porta COM conforme necessário):

```
esptool.exe --chip esp32c3 --port "COM11" --baud 921600  --before default_reset --after hard_reset write_flash  -z --flash_mode keep --flash_freq keep --flash_size keep 0x0 "nes-esp-firmware.ino.bootloader.bin" 0x8000 "nes-esp-firmware.ino.partitions.bin" 0xe000 "boot_app0.bin" 0x10000 "nes-esp-firmware.ino.bin" 

esptool.exe --chip esp32c3 --port COM11 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 2162688 ra_rash_map.bin
```

## Arquitetura e Funcionamento

O adaptador utiliza dois microcontroladores trabalhando em conjunto:

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/architecture.png"/>
</p>

### Raspberry Pi Pico

- **Identificação do Jogo:** Lê parte do cartucho para calcular o CRC e identificar o jogo.
- **Monitoramento de Memória:** Utiliza dois núcleos para monitorar o barramento e processar escritas usando a biblioteca rcheevos:
  - **Core 0:** Calcula o CRC, executa rotinas do rcheevos e gerencia a comunicação serial com o ESP32.
  - **Core 1:** Usa PIO para interceptar o barramento e detectar valores estáveis em operações de escrita. Emprega DMA para transferir os dados para um buffer ping-pong e encaminha os endereços relevantes para o Core 0 via buffer circular.
- **Observação:** A comunicação serial possui limite de aproximadamente 32KB, restringindo o tamanho da resposta da lista de conquistas.

### ESP32

- **Conectividade e Interface:** Fornece Internet ao NES e gerencia uma tela TFT para exibir conquistas, além de um buzzer para efeitos sonoros. 
- **Gerenciamento de Configuração:** Armazena credenciais de Wi-Fi e RetroAchievements na EEPROM; configuração é feita via smartphone conectado ao ponto de acesso do ESP32. 
- **Sistema de Arquivos e Comunicação:** Utiliza LittleFS para armazenar uma tabela de hash (CRC32 para MD5) para identificação de cartuchos e imagens dos jogos. 
- **Transmite o conteúdo da LCD para uma Web App** Utiliza WebSocket e mDNS para servir uma página web que espelha o conteúdo da telinha LCD e exibe eventos de conquistas adicionais que não cabem na tela física. (Mais detalhes disponíveis na pasta `misc`) O web app fica disponível assim que a imagem do jogo aparece na LCD e pode ser acessado em `http://nes-ra-adapter.local` — certifique-se de que seu smartphone esteja conectado à mesma rede Wi-Fi.

---

## Limitações

- **Testes com diferentes jogos**: Testamos o adaptador com cerca de 50 jogos e detectamos problemas em 2. Consulte a [página de compatibilidade](https://github.com/odelot/nes-ra-adapter/blob/main/Compatibility.md) para mais detalhes. Estamos trabalhando para melhorar a compatibilidade.

- **Detecção de frame e reset**: Não conseguimos detectar um frame apenas inspecionando os sinais do cartucho. Utilizamos uma heurística que, até agora, tem mostrado bons resultados, mas não há garantia de que funcione para todas as conquistas. Além disso, a detecção de RESET do console ainda não está implementada, exigindo que o console seja desligado e ligado para reinicialização.

- **Tamanho da Resposta do Servidor**: A RAM disponível para armazenar a resposta do RetroAchievements, que contém a lista de conquistas e endereços de memória a serem monitorados, é limitada a 32KB. Publicaremos um código-fonte de uma função AWS Lambda para remover campos desnecessários ou reduzir a lista de conquistas, garantindo que a resposta caiba dentro desse limite.

- **Limitação rara: alguns jogos podem ser impossíveis de Masterizar** É possível que o ESP32 (com um limite de ~62 KB) não apenas remova atributos não utilizados das conquistas, mas também filtre conquistas muito grandes se a lista completa exceder o limite de 32 KB do Raspberry Pi Pico. Isso significa que alguns jogos podem não ser totalmente masterizáveis ​​usando o adaptador. Em versões futuras, analisarei a possibilidade de adicionar um aviso ao usuário quando essa filtragem ocorrer. (de 50 jogos testados, essa filtragem aconteceu em dois - FF1 perdeu um achievement missible e Guardian Legend perdeu alguns achievements grandes)

- **Placar de líderes desativado** Para reduzir os dados entre o ESP32 e o Pico e como estamos usando uma heurística para detectar quadros, o recurso de placar de líderes está desativado.


---

## Informações Gerais

- **Consumo Energético**: O adaptador consome tipicamente 0.105A (máximo ~0.220A). A tela LCD consome cerca de 0.035A. O consumo está dentro do limite do regulador de tensão 7805 presente no NES <br/>

- **Conversão de Níveis Lógicos (5V vs 3.3V)**: Nos protótipos, não usamos conversão de nível lógico para evitar atrasos nos sinais que possam comprometer o funcionamento. O barramento entre o cartucho e o NES está desativado quando o cartucho é identificado (não comprometendo o NES), e os níveis de corrente estão dentro dos limites do Pico. Durante a leitura do barramento, todas as portas ligadas ao barramento estão em modo leitura e são 5v tolerantes. Na versão final, o GH pretende limitar a corrente para reduzir o stress no Pico. Se alguém da comunidade identificar a necessidade de conversão de nível e desenvolver um esquema de teste sem comprometer os sinais, contribuições são bem-vindas.

---

## Histórico de Versões

- **Version 1.0 (2025-11-09)** - Nova versão miniaturizada da placa e do case 3d - não é necessário abrir o console. Pequenas mudanças na heuristica de detecção de frames. LED Bicolor (opcional) caso queira substituir a tela LCD. Opção de inverter o conteudo da tela LCD durante a compilação do firmware do ESP32 (necessario para a montagem do case)
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
- Participe do nosso canal no Discord: [https://discord.gg/baM7y3xbsA](https://discord.gg/baM7y3xbsA)  
- Ajude-nos [comprando um café](https://buymeacoffee.com/nes.ra.adapter) para auxiliar nos custos dos protótipos de hardware.

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
- **R**: Não. O adaptador precisa ler o cartucho para identifica-lo e, quando usado com everdrive, ele lê o firmware do everdrive e não o jogo. Essa é uma das razões para ser possivel suportar everdrive.
<br/>

- **P: O adaptador funcionará com modelos japoneses?**
- **R**: Ainda não testamos com adaptadores japoneses, mas os primeiros resultados indicam compatibilidade com adaptadores de 72 para 60 pinos. Jogos japoneses já fazem parte do mapeamento do RetroAchievements e já tivemos sucesso em testes com adaptações similares.
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
