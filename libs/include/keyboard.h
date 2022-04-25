#ifndef KEYBOARD_H
#define KEYBOARD_H

enum EventClass {
    ASCII_KEY,
    PAUSE_KEY,
    ALT_KEY = 0x11,
    LSHIFT_KEY = 0x12,
    CONTROL_KEY = 0x14,
    CAPS_LOCK_KEY = 0x58,
    RSHIFT_KEY = 0x59,
    LEFT_ARROW_KEY = 0x6b,
    DOWN_ARROW_KEY = 0x72,
    RIGHT_ARROW_KEY = 0x74,
    UP_ARROW_KEY = 0x75,
    ESCAPE_KEY = 0x76
};
enum EventType { KEY_DOWN, KEY_HELD, KEY_UP };

typedef struct {
    enum EventClass class;
    enum EventType type;
    char value;
} KeyEvent;

void configure_keyboard(void);
const KeyEvent *get_keyboard_event(void);
char get_keyboard_character(void);

char get_shifted_key(char);
char get_control_key(char);
char get_caps_lock_key(char);

#endif
