#!/bin/bash

# Automatic STM32 SPI Gap Fix Script
# This script applies the fix to eliminate SPI transmission gaps

set -e

MAIN_FILE="/home/user/2/ok/Core/Src/main.c"
BACKUP_FILE="/home/user/2/ok/Core/Src/main.c.backup"

echo "🔧 STM32 SPI Gap Fix - Auto Patcher"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Check if file exists
if [ ! -f "$MAIN_FILE" ]; then
    echo "❌ ERROR: $MAIN_FILE not found!"
    exit 1
fi

# Backup original file
echo "📦 Creating backup..."
cp "$MAIN_FILE" "$BACKUP_FILE"
echo "✅ Backup saved: $BACKUP_FILE"
echo ""

# Apply fixes
echo "🔨 Applying fixes..."

# Fix 1: Add spi_complete variable (after line 27)
sed -i '/volatile uint8_t spi_busy = 0;/a volatile uint8_t spi_complete = 0;' "$MAIN_FILE"

# Fix 2: Modify SPI callback (replace lines 72-78)
# This is tricky with sed, so we use awk
awk '
/void HAL_SPI_TxRxCpltCallback\(SPI_HandleTypeDef \*hspi\)/ {
    print $0
    getline; print $0  # print {
    # Skip old content until }
    while (getline && !/^}/) {
        if (/spi_busy = 0;/) {
            print "  spi_complete = 1;  // Signal main loop to restart"
        } else if (/spi_count\+\+;/) {
            print $0
        }
        # Skip the HAL_SPI_TransmitReceive_DMA line
    }
    print "  // Do NOT restart here - will restart in main loop"
    print $0  # print }
    next
}
{print}
' "$MAIN_FILE" > "$MAIN_FILE.tmp" && mv "$MAIN_FILE.tmp" "$MAIN_FILE"

# Fix 3: Add restart code in main loop (before "if (adc_complete)")
awk '
/if \(adc_complete\)/ && !found {
    print "    // Restart SPI when transfer complete"
    print "    if (spi_complete) {"
    print "      spi_complete = 0;"
    print "      HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);"
    print "    }"
    print ""
    found=1
}
{print}
' "$MAIN_FILE" > "$MAIN_FILE.tmp" && mv "$MAIN_FILE.tmp" "$MAIN_FILE"

echo "✅ Fixes applied successfully!"
echo ""

# Show diff
echo "📊 Changes summary:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
diff -u "$BACKUP_FILE" "$MAIN_FILE" | head -50 || true
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "✅ Fix complete!"
echo ""
echo "Next steps:"
echo "  1. Rebuild STM32 firmware:"
echo "     cd /home/user/2/ok/Debug && make clean && make"
echo ""
echo "  2. Flash to STM32:"
echo "     st-flash write ok.elf 0x8000000"
echo ""
echo "  3. Test on Pi4:"
echo "     ./scope"
echo ""
echo "Expected result: Marker fails drop from 40% to <5%"
