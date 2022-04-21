USER_FILES="src/main.c src/sprites/*.c src/old-lcd.c src/keyboard.c src/console.c"

# OPTIMIZE="-Og"
# OPTIMIZE="-g"
OPTIMIZE="-O3 -g"

LIB_FILES="libs/*.c libs/*.s"
CORTEX_FLAGS="-mlittle-endian -mthumb -mthumb-interwork -mcpu=cortex-m0 -fsingle-precision-constant"
CFLAGS="-std=gnu99 -Wall -Wdouble-promotion -Wno-char-subscripts $CORTEX_FLAGS"
CORTEX_FILES="support/syscalls.c support/startup_stm32.s support/config.c"
FILES="$CORTEX_FILES $USER_FILES $LIB_FILES"
OPEN_OCD_FLAGS="-f interface/stlink.cfg -f target/stm32f0x.cfg"

arm-none-eabi-gcc $CFLAGS $OPTIMIZE $FILES -I support/ -I libs/ -T linker-script.ld -o build/main.elf

if [ $? -eq 0 ]; then
    arm-none-eabi-objcopy -O binary build/main.elf build/main.bin
    openocd -c "adapter speed 950" $OPEN_OCD_FLAGS -c "program build/main.bin verify reset exit 0x08000000"
fi
