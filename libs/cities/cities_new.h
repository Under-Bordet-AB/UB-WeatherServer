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
    cities_state state;
    char* buffer;
    int bytesread;
    void(*on_done);
} cities_t;

int cities_init(void** ctx, void(*ondone));
int cities_get_buffer(void** ctx, char** buffer);
int cities_work(void** ctx);
int cities_dispose(void** ctx);

#endif
