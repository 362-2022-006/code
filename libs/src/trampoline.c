#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "delay.h"
#include "fat.h"
#include "gpu.h"
#include "hexdump.h"
#include "hook.h"
#include "keyboard.h"
#include "lcd.h"
#include "random.h"
#include "sd.h"
#include "text.h"

int _kill(int pid, int sig);
void *_sbrk(int incr);
int _wait(int *status);
int _unlink(char *name);

void __aeabi_idiv(void);
void __aeabi_uidiv(void);
void __aeabi_uidivmod(void);
void __aeabi_idivmod(void);
void __gnu_thumb1_case_uqi(void);

const static struct {
    char *name;
    void *function;
} fn_map[] = {{"__aeabi_idiv", __aeabi_idiv},
              {"__aeabi_idivmod", __aeabi_idivmod},
              {"__aeabi_uidiv", __aeabi_uidiv},
              {"__aeabi_uidivmod", __aeabi_uidivmod},
              {"__gnu_thumb1_case_uqi", __gnu_thumb1_case_uqi},
              {"_kill", _kill},
              {"_sbrk", _sbrk},
              {"_unlink", _unlink},
              {"_wait", _wait},
              {"check_lcd_flag", check_lcd_flag},
              {"clear_lcd_flag", clear_lcd_flag},
              {"close_sd", close_sd},
              {"configure_keyboard", configure_keyboard},
              {"delay_ms", delay_ms},
              {"delay_ns", delay_ns},
              {"delay_us", delay_us},
              {"disable_gpu", disable_gpu},
              {"fclose", fclose},
              {"fflush", fflush},
              {"free", free},
              {"get_caps_lock_key", get_caps_lock_key},
              {"get_control_key", get_control_key},
              {"get_file_next_sector", get_file_next_sector},
              {"get_keyboard_character", get_keyboard_character},
              {"get_keyboard_event", get_keyboard_event},
              {"get_random", get_random},
              {"get_shifted_key", get_shifted_key},
              {"gpu_buffer_add", gpu_buffer_add},
              {"hexdump", hexdump},
              {"hook_timer", hook_timer},
              {"init_fat", init_fat},
              {"init_gpu", init_gpu},
              {"init_lcd_dma", init_lcd_dma},
              {"init_lcd_spi", init_lcd_spi},
              {"init_screen", init_screen},
              {"is_in_insert_mode", is_in_insert_mode},
              {"ls", ls},
              {"malloc", malloc},
              {"memchr", memchr},
              {"memcpy", memcpy},
              {"memmove", memmove},
              {"memset", memset},
              {"mix_random", mix_random},
              {"open", open},
              {"open_root", open_root},
              {"print_console_prompt", print_console_prompt},
              {"printf", printf},
              {"putchar", putchar},
              {"puts", puts},
              {"read_sector", read_sector},
              {"read_sector_dma", read_sector_dma},
              {"realloc", realloc},
              {"reenable_gpu", reenable_gpu},
              {"reset_file", reset_file},
              {"set_lcd_flag", set_lcd_flag},
              {"set_screen_text_buffer", set_screen_text_buffer},
              {"start_console", start_console},
              {"strlen", strlen},
              {"unhook_timer", unhook_timer},
              {"update_console", update_console}};

const static int fn_map_length = sizeof(fn_map) / sizeof(*fn_map);

void *SVC_function(void *addr) {
    int index;
    for (index = 0; index < fn_map_length; index++) {
        if (!strcmp(fn_map[index].name, (char *)addr))
            break;
    }
    if (index == fn_map_length) {
        for (;;)
            ; // Basically a HardFault
    }

    // rewrite code
    uint32_t *code_addr = addr - 4;

    code_addr[0] = 0x4C02B410; // push {r4}     // ldr  r4, [pc, #8]
    code_addr[1] = 0xBC1046A4; // mov  ip, r4   // pop  {r4}
    code_addr[2] = 0x00004760; // bx   ip
    code_addr[3] = ((uint32_t)fn_map[index].function) | 1;

    return (void *)code_addr[3];
}

// provide hard fault handler for easier debugging
void HardFault_Handler(void) {
    for (;;)
        ;
}
