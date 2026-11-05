# Amp Lab

Simulatore di amplificatore per chitarra (JUCE 8 + NeuralAmpModelerCore). App standalone, VST3 e AU.

- Catena: gate, ingresso, ampli interno (drive con oversampling 4x) oppure modello NAM, EQ a 3 bande,
  cabinet (nessuno, simulato, IR), modulazione (chorus, flanger, phaser, tremolo), delay, riverbero.
- Sorgenti: ingresso audio, riff di prova, file audio in loop.
- Modelli `.nam`: pulsante "Carica NAM...". Se il modello include il cabinet, cabinet e IR si disattivano.

## Compilare su GitHub
La build parte da sola a ogni modifica (scheda Actions, workflow "Build macOS") e produce l'artefatto AmpLab-macOS.
Al primo avvio: clic destro > Apri, oppure `xattr -dr com.apple.quarantine "Amp Lab.app"`.
