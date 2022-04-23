import os
import shutil
import subprocess

function_list = [
    # stdlib
    '__aeabi_idiv',
    '__aeabi_idivmod',
    '__aeabi_uidiv',
    '__aeabi_uidivmod',
    '__gnu_thumb1_case_uqi',
    '_kill',
    '_sbrk',
    '_unlink',
    '_wait',
    'fclose',
    'fflush',
    'free',
    'malloc',
    'memchr',
    'memcpy',
    'memmove',
    'memset',
    'printf',
    'putchar',
    'puts',
    'realloc',
    'strlen',

    # utilities
    'delay_ms',
    'delay_ns',
    'delay_us',
    'hexdump',

    # FAT
    'close_sd',
    'get_file_next_sector',
    'init_fat',
    'ls',
    'open',
    'open_root',
    'read_sector',
    'read_sector_dma',
    'reset_file',

    # gpu
    'init_gpu',
    'gpu_buffer_add',
    'disable_gpu',
    'reenable_gpu',

    # timer
    'hook_timer',
    'unhook_timer',

    # keyboard
    'configure_keyboard',
    'get_keyboard_event',
    'get_shifted_key',
    'get_control_key',

    # LCD
    'init_screen',
    'init_lcd_spi',
    'init_lcd_dma',
    'set_lcd_flag',
    'clear_lcd_flag',
    'check_lcd_flag',

    # random
    'mix_random',
    'get_random'
]

asm_file = '''
.cpu cortex-m0
.thumb
.syntax unified
.fpu softvfp

.text
.balign 4
.global {}
{}:
    push {{r4}}
    svc  0
    .asciz "{}"

'''

asm_space = ' ' * 4 + '.space {}'

command = ['arm-none-eabi-as', '-mlittle-endian', '-mthumb', '-mcpu=cortex-m0']

os.chdir(os.path.dirname(os.path.abspath(__file__)))

if os.path.exists('temp/'):
    shutil.rmtree('temp/')
if os.path.exists('libtrampoline.a'):
    os.remove('libtrampoline.a')

os.mkdir('temp/')

o_files = []

for func in function_list:
    s_file = 'temp/' + func + '.s'
    o_file = 'temp/' + func + '.o'

    with open(s_file, 'w') as f:
        f.write(asm_file.format(func, func, func).strip())
        if len(func) < 11:
            f.write('\n' + asm_space.format(11 - len(func)))
        f.write('\n')

    subprocess.run(command + [s_file, '-o', o_file])

    o_files.append(o_file)

subprocess.run(['arm-none-eabi-ar', 'r', 'libtrampoline.a'] + o_files)

for f in o_files:
    os.remove(f)

print(f'{len(function_list)} functions generated')

function_list.sort()
print(','.join(f'{{"{func}", {func}}}' for func in function_list))
