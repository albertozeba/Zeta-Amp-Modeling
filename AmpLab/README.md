# Amp Lab

Simulatore di amplificatore per chitarra (JUCE 8, C++17): gate, drive con oversampling 4x, EQ a 3 bande,
cabinet simulato, modulazione (chorus, flanger, phaser), delay e riverbero. Esce come app standalone,
VST3 e AU.

## Compilare su GitHub (senza installare nulla)
1. Carica tutto il contenuto di questa cartella nel tuo repository (branch `main`).
2. Apri la scheda **Actions**: parte da sola la build "Build macOS" (anche con "Run workflow").
3. A build finita scarica l'artefatto **AmpLab-macOS** (contiene `Amp Lab.app`).
4. Decomprimi e, al primo avvio, apri con clic destro > Apri. Se macOS lo blocca:
   `xattr -dr com.apple.quarantine "Amp Lab.app"`

## Uso con la MR18
Nella finestra Impostazioni dell'app scegli la MR18 come dispositivo, buffer 128 (poi 64) a 48 kHz,
e come ingresso il canale USB dove arriva la chitarra (il canale 1 viene usato per la chitarra).
Togli la spunta da "Mute audio input" e usa le cuffie.
