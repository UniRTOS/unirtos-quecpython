#ifndef QUECPYTHON_PUBLIC_H
#define QUECPYTHON_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

void quecpython_init(void);
int quecpython_is_running(void);
int quecpython_exec_string(const char *code);

#ifdef __cplusplus
}
#endif

#endif
