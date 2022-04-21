#ifndef __HOOK_H__
#define __HOOK_H__

void hook_timer(int rate, void (*call_fn)(void));
void unhook_timer(void);

#endif
