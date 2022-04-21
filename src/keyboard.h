#ifndef KEYBOARD_H
#define KEYBOARD_H

enum EventClass {
    ASCII_KEY,
    LSHIFT_KEY = 0x12,
    RSHIFT_KEY = 0x59,
    CONTROL_KEY = 0x14,
    ALT_KEY = 0x11,
    ESCAPE_KEY = 0x76,
    LEFT_ARROW_KEY = 0x6b,
    RIGHT_ARROW_KEY = 0x74,
    UP_ARROW_KEY = 0x75,
    DOWN_ARROW_KEY = 0x72
};
enum EventType { KEY_DOWN, KEY_HELD, KEY_UP };

typedef struct {
    enum EventClass class;
    enum EventType type;
    char value;
} KeyEvent;

void configure_keyboard(void);
const KeyEvent *get_keyboard_event(void);

char get_shifted_key(char);
char get_control_key(char);

#endif
