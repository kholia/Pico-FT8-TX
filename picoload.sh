#!/usr/bin/env bash

# Default to pico-wspr-tx.uf2 if no parameter given
UF2_FILE="${1:-pico-wspr-tx.uf2}"

if test -f "$UF2_FILE"; then
  echo "Flashing $UF2_FILE to Pico..."
  picotool load -f "$UF2_FILE"
  echo "✅ Flash complete!"
else
  echo "❌ Cannot find file '$UF2_FILE'"
  echo "Available UF2 files:"
  ls -1 *.uf2 2>/dev/null || echo "No UF2 files found"
  exit 1
fi
