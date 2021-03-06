# USER_FILES="src/main.c src/tetris.c"
# USER_DATA="src/images/tetris-sprites.c"
# USER_FILES="src/main.c src/snake.c"
# USER_DATA="src/images/snake-sprites.c"
# USER_FILES="src/main.c src/breakout.c"
# USER_DATA="src/images/breakout-sprites.c"
# USER_FILES="src/main.c src/touhou.c"
# USER_DATA="src/images/touhou-sprites.c"
USER_FILES="src/main.c src/ants.c"
USER_DATA="src/images/ant-sprites.c"

# USER_FILES="src/main.c"
# USER_DATA=""

# USER_SOUNDS="src/sounds/test-sound.c"
USER_SOUNDS=""

# OPTIMIZE="-g"
# OPTIMIZE="-O3 -g"
OPTIMIZE="-Os -g"

CORTEX_FLAGS="-mlittle-endian -mthumb -mthumb-interwork -mcpu=cortex-m0 -fsingle-precision-constant"
CFLAGS="-std=gnu99 -Wall -Wdouble-promotion -Wno-char-subscripts $CORTEX_FLAGS"

CORTEX_FILES="support/syscalls.c support/startup_stm32.s support/config.c"
FILES="$CORTEX_FILES $USER_FILES $USER_DATA $USER_SOUNDS libs/src/*.c libs/src/*.s"

SAVE_FLAGS="-save-temps -o build/main.elf"
INCLUDE_FLAGS="-I support/ -I libs/include/ -I src/include"

OPEN_OCD_FLAGS="-f interface/stlink.cfg -f target/stm32f0x.cfg"

mkdir -p ./build/intermediates/

arm-none-eabi-gcc $CFLAGS $OPTIMIZE $FILES $SAVE_FLAGS $INCLUDE_FLAGS -T linker-script.ld

SUC=$?
mv *.s build/intermediates
rm *.o *.i

if [ $SUC -eq 0 ]; then
    arm-none-eabi-objcopy -O binary build/main.elf build/main.bin
    openocd -c "adapter speed 950" $OPEN_OCD_FLAGS -c "program build/main.bin verify reset exit 0x08000000"
fi
