# Circuit Schematics and PCB

## Final Version 1.0 - Miniaturized 

Finally you can use the adapter without open your NES. Order the PCB, aseembly it and be happy ;-) Made by the awesome GH.

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/board1.0.png"/>
</p>

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/board1.0-assembled.png"/>
</p>

Schematics: (very poor version - we will upload a decent file soon)

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/schematic-v1.0.png"/>
</p>

## Prototype PCBs Sets 

We have two sets of PCBs used for development, implementing the minimal schematic shown above. Each has its pros and cons, but both feature large traces (which isn't ideal) and lack a ground plane. These were designed purely for development purposes. All prototype PCBs were created using Fritzing version 0.9.3b by odelot (a Computer Scientist—please forgive him for that! 😆).

Schematics:

<img src="https://github.com/odelot/nes-ra-adapter/blob/main/images/schematic-v0.1.png"/>

### Prototype v0.1

This set consists of four PCBs that can be connected like modules. You can remove one of them to test new ideas, such as swapping out the SN74HC4066 board for bus transceivers or inserting a logic level shifter between two modules. Female headers can be used to easily attach logic analyzers or oscilloscopes for debugging.

The PPU signal traces are relatively small, but the CPU signal traces are large to accommodate debugging and flexibility. Since the boards are connected using 90-degree headers, they can sometimes suffer from poor contact, requiring plastic supports to keep them level. Games with MMC3 may experience issues with the scanline detector due to the large trace sizes and the propagation delay introduced by the 4066.

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/prototype-v0.1.png"/>
</p>

### Prototype v0.2

To address contact issues, we created an almost all-in-one design (in a rush, right before a big vacation, to avoid waiting nearly 30 days for fabrication—we could've done it better!). The PPU traces are too large, which can cause interference on certain NES motherboards (detected with NES-CPU-02 and Rad Racer).

This version consists of two boards, similar to what we aim for in the miniaturized version:

- One board with the 4066s and the Raspberry Pi Pico.
- Another board with the ESP32, LCD screen, reset button, and buzzer.

With these two boards, we can experiment with different communication methods using flat cables already aiming the final version. MMC3 scanline detector issues are minimal with the 4066, and completely eliminated when using the SN74LVC1G3157DBVR.

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/prototype-v0.2.png"/>
</p>

### SN74HC4066 vs SN74LVC1G3157DBVR

To minimize propagation delay issues, we designed an adapter that replaces the 4066 with a set of four SN74LVC1G3157DBVR chips. This completely resolved the MMC3-related problems. One thing to keep in mind: the SN74LVC1G3157DBVR is an SMD component and is more challenging to solder.

<p align="center">
  <img width="70%"  src="https://github.com/odelot/nes-ra-adapter/blob/main/images/SN74HC4066vsSN74LVC1G3157DBVR.png"/>
</p>

## License

The PCB circuits are distributed under CC-BY-4.0 license.
