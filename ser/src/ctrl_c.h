#ifndef CTRL_C_H
#define CTRL_C_H

#include <stdbool.h>

typedef bool (*ctrl_c_callback_t)(void);

void ctrl_c_register(ctrl_c_callback_t callback);

#endif // CTRL_C_H
