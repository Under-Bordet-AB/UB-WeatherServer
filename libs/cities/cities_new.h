#ifndef _CITIES_H
#define _CITIES_H

typedef enum {
    Cities_State_Init,
    Cities_State_ReadFiles,
    Cities_State_ReadString,
    Cities_State_Convert,
    Cities_State_Done
} cities_state;

typedef struct cities_t {
    // linkedlist cities_l;
    // void* context;
    cities_state state;
    char* buffer;
    int bytesread;
    void(*on_done);
} cities_t;

int cities_init(void** context);
int cities_work();
char* cities_get_buffer();
int cities_dispose();

#endif
