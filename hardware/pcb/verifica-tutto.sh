#!/bin/bash
# Controlla la scheda unica con KiCad e prepara i file di produzione nella cartella "uscita".
cd "$(dirname "$0")"
K=$(ls -d /Applications/KiCad*/KiCad.app/Contents/MacOS/kicad-cli "$HOME"/Applications/KiCad*/KiCad.app/Contents/MacOS/kicad-cli 2>/dev/null | head -1)
mkdir -p uscita/gerber
if [ -z "$K" ]; then echo "KiCad non trovato in Applicazioni" | tee uscita/log.txt; exit 1; fi
P=trackpad-tutto.kicad_pcb
LY=F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts
{
  echo "== versione"; "$K" version
  echo "== aiuto drc"; "$K" pcb drc --help 2>&1 | head -40
  echo "== DRC (riempie le zone di massa e salva)"
  "$K" pcb drc --format json --severity-all --units mm --refill-zones --save-board -o uscita/drc.json "$P"; R=$?
  echo "esito: $R"
  if [ $R -ne 0 ] && [ ! -s uscita/drc.json ]; then
    echo "== DRC (senza riempimento)"; "$K" pcb drc --format json --severity-all --units mm -o uscita/drc.json "$P"; echo "esito: $?"
  fi
  echo "== gerber"; "$K" pcb export gerbers --check-zones --layers "$LY" -o uscita/gerber/ "$P"; echo "esito: $?"
  echo "== forature"; "$K" pcb export drill --format excellon --excellon-units mm --excellon-separate-th -o uscita/gerber/ "$P"; echo "esito: $?"
  echo "== posizioni"; "$K" pcb export pos --format csv --units mm --side both -o uscita/posizioni-kicad.csv "$P"; echo "esito: $?"
  echo "== immagini"
  "$K" pcb render --side top -o uscita/render-sopra.png "$P"; echo "esito: $?"
  "$K" pcb render --side bottom -o uscita/render-sotto.png "$P"; echo "esito: $?"
  "$K" pcb export svg --mode-single --layers F.Cu,F.SilkS,F.Mask,Edge.Cuts --exclude-drawing-sheet --page-size-mode 2 -o uscita/sopra.svg "$P"; echo "esito: $?"
  "$K" pcb export svg --mode-single --layers B.Cu,B.SilkS,Edge.Cuts --exclude-drawing-sheet --page-size-mode 2 -o uscita/sotto.svg "$P"; echo "esito: $?"
  echo "== zip"; (cd uscita/gerber && rm -f ../gerber-trackpad-tutto.zip && zip -q ../gerber-trackpad-tutto.zip *); echo "esito: $?"
  ls -la uscita uscita/gerber
} > uscita/log.txt 2>&1
echo "Fatto. Risultati in $(pwd)/uscita"
