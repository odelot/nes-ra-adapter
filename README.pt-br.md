# NES RA Adapter Project
[![en-us](https://img.shields.io/badge/lang-en--us-red.svg)](https://github.com/odelot/nes-ra-adapter/blob/main/README.md)

Repositório do projeto **NES RetroAchievements Adapter** – uma iniciativa maker estilo "faroeste cyberpunk" que transforma o console físico do NES (lançado em 1985) ao adicionar conectividade à Internet e a funcionalidade de conquistas, suportada pela incrível comunidade do RetroAchievements.

<p align="center">
  <a href="https://www.youtube.com/watch?v=SEKSnkoNz1k">
    <img width="70%" src="https://github.com/odelot/nes-ra-adapter/blob/main/images/video.png">
  </a>
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

| nome da parte                  | quantidade | valor total (U$)*      |
|--------------------------------|------------|------------------------|
| slot 72 pinos**                | 2x         | (2x ~U$2,50) ~ U$5,00  |
| SN74HC4066***                  | 7x         | (7x ~U$0,85) ~ U$6,00  |
| Raspberry Pi Pico (placa roxa) | 1x         | ~ U$3,00               |
| ESP32 C3 Supermini             | 1x         | ~ U$2,50               |
| LCD 240x240 driver ST7789      | 1x         | ~ U$2,75               |
| Buzzer 3v + Transistor BC548   | 1x         | ~ U$1,25               |
| **Total** (sem PCB, case 3d)   |            | ~ **U$20,50**          |

\* Fonte: Aliexpress - 2025-03-29 <br/>
\** Na versão miniaturizada, só será necessário 1 slot <br/>
\*** Alternativa mais estável ao SN74HC4066 seriam 26x SN74LVC1G3157DBVR (smd) - veja mais detalhes [aqui](https://github.com/odelot/nes-ra-adapter/tree/main/hardware#sn74hc4066-vs-sn74lvc1g3157dbvr)

### Monte o circuito

Atualmente, o projeto disponibiliza duas opções para montagem de circuito: 
1. **Protoboard/Placas Perfuradas:** Monte o circuito usando uma protoboard, seguindo o esquema disponível na pasta `hardware`.
2. **PCBs:** encomende as PCBs (v0.1 ou v0.2) usando os arquivos na pasta `hardware` para uma montagem mais robusta.

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
- **EXPERIMENTAL – Transmite o conteúdo da LCD para uma Web App** Utiliza WebSocket e mDNS para servir uma página web que espelha o conteúdo da telinha LCD e exibe eventos de conquistas adicionais que não cabem na tela física. (Mais detalhes disponíveis na pasta `misc`) O web app fica disponível assim que a imagem do jogo aparece na LCD e pode ser acessado em `http://nes-ra-adapter.local` — certifique-se de que seu smartphone esteja conectado à mesma rede Wi-Fi.

---

## Limitações

- **Testes com diferentes jogos**: Testamos o adaptador com cerca de 50 jogos e detectamos problemas em 2. Consulte a [página de compatibilidade](https://github.com/odelot/nes-ra-adapter/blob/main/Compatibility.md) para mais detalhes. Estamos trabalhando para melhorar a compatibilidade.

- **Detecção de frame e reset**: Não conseguimos detectar um frame apenas inspecionando os sinais do cartucho. Utilizamos uma heurística que, até agora, tem mostrado bons resultados, mas não há garantia de que funcione para todas as conquistas. Além disso, a detecção de RESET do console ainda não está implementada, exigindo que o console seja desligado e ligado para reinicialização.

- **Tamanho da Resposta do Servidor**: A RAM disponível para armazenar a resposta do RetroAchievements, que contém a lista de conquistas e endereços de memória a serem monitorados, é limitada a 32KB. Publicaremos um código-fonte de uma função AWS Lambda para remover campos desnecessários ou reduzir a lista de conquistas, garantindo que a resposta caiba dentro desse limite.


---

## Informações Gerais

- **Consumo Energético**: O adaptador consome tipicamente 0.105A (máximo ~0.220A). A tela LCD consome cerca de 0.035A. O consumo está dentro do limite do regulador de tensão 7805 presente no NES <br/>

- **Conversão de Níveis Lógicos (5V vs 3.3V)**: Nos protótipos, não usamos conversão de nível lógico para evitar atrasos nos sinais que possam comprometer o funcionamento. O barramento entre o cartucho e o NES está desativado quando o cartucho é identificado (não comprometendo o NES), e os níveis de corrente estão dentro dos limites do Pico. Durante a leitura do barramento, todas as portas ligadas ao barramento estão em modo leitura e são 5v tolerantes. Na versão final, o GH pretende limitar a corrente para reduzir o stress no Pico. Se alguém da comunidade identificar a necessidade de conversão de nível e desenvolver um esquema de teste sem comprometer os sinais, contribuições são bem-vindas.

---

## Próximos Passos

### 1 - Miniaturizar o Circuito:
Desenvolver uma placa do tamanho aproximado de um Game Genie que caiba no slot de cartuchos do NES 001. Incluir plano de terra, capacitores de desacoplamento, resistores limitadores de corrente, e trilhas menores para reduzir interferência.

### 2 - Adaptar o Case ao Circuito Miniaturizado:
Atualizar o case (atualmente inspirado no Game Genie) para se adequar ao novo formato do circuito.

---

## Histórico de Versões

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
Se você não se sente confortável em fazê-lo sozinho, lembre-se de que, assim como outros projetos open-source (como [GBSControl](https://github.com/ramapcsx2/gbs-control), [GB Interceptor](https://github.com/Staacks/gbinterceptor) e [Open Source Cartridge Reader](https://github.com/sanni/cartreader)), a própria comunidade pode se organizar para fabricar e comercializar o adaptador.
<br/>

- **P: Posso fabricar e vender o adaptador?**
- **R**: Sim! As licenças de software e hardware permitem o uso comercial. Vale notar que os cases 3D foram projetados no Fusion 360 para fins não lucrativos, mas podem ser remodelados futuramente para uso comercial em outros softwares.
<br/>

- **P: Preciso abrir meu NES para usar o adaptador?**
- **R**: Nossa meta é que o adaptador seja plug-and-play, semelhante ao Game Genie, encaixando-se no slot de cartuchos sem necessidade de abrir o console. Entretanto, os protótipos atuais exigem que o NES seja aberto para instalação.
<br/>

- **P: Preciso modificar meu NES para utilizar o adaptador?**
- **R**: Não, ele é plug-and-play e não requer modificações.
<br/>

- **P: O adaptador precisa estar conectado a um computador?**
- **R**: Não! Ele é autossuficiente, contando com conectividade Wi-Fi integrada. A configuração pode ser feita via smartphone, e todo o processamento dos achievements é realizado internamente.
<br/>

- **P: O adaptador funcionará com everdrives?**
- **R**: O adaptador precisa ler o cartucho para identifica-lo e, quando usado com everdrive, ele lê o firmware do everdrive e não o jogo. Essa é uma das razões para ser possivel suportar everdrive.
<br/>

- **P: O adaptador funcionará com modelos japoneses?**
- **R**: Ainda não testamos com adaptadores japoneses, mas os primeiros resultados indicam compatibilidade com adaptadores de 72 para 60 pinos. Jogos japoneses já fazem parte do mapeamento do RetroAchievements e já tivemos sucesso em testes com adaptações similares.
<br/>

- **P: O adaptador funcionará com Disk System?**
- **R**: Não testamos com Disk System, pois não dispomos desse hardware. Essa funcionalidade pode ser explorada futuramente, assim que conseguirmos o conjunto necessário para testes.
<br/>

- **P: O adaptador funciona com os famiclones brasileiros?**
- **R**: Nosso protótipo atual possui um slot fêmea compatível com o NES americano (NES-001). A versão miniaturizada terá um conector macho que poderá ser adaptado para uso com o NES-001, NES-101 (top loader) e possivelmente outros consoles, incluindo famiclones, por meio de adaptadores

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
