/*
 * tcl.h — заголовок для встраиваемого pTcl (пример example/partcl-master).
 * Реализация в tcl.c. Заголовок повторяет layout struct tcl и константы
 * потока управления для использования из C++ кода модуля.
 */

#ifndef TCL_H_EMBED
#define TCL_H_EMBED

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char tcl_value_t;

struct tcl_env;
struct tcl_var;
struct tcl_cmd;

typedef int (*tcl_cmd_fn_t)(struct tcl*, tcl_value_t*, void*);

struct tcl {
  struct tcl_env* env;
  struct tcl_cmd* cmds;
  tcl_value_t* result;
};

/* Коды потока управления (должны совпадать с enum в tcl.c) */
enum { FERROR = -1, FNORMAL = 0, FRETURN = 1, FBREAK = 2, FAGAIN = 3 };

/* Управление интерпретатором */
void tcl_init(struct tcl* tcl);
void tcl_destroy(struct tcl* tcl);
void tcl_register(struct tcl* tcl, const char* name, tcl_cmd_fn_t fn, int arity, void* arg);
int tcl_eval(struct tcl* tcl, const char* s, size_t len);
int tcl_subst(struct tcl* tcl, const char* s, size_t len);
int tcl_result(struct tcl* tcl, int flow, tcl_value_t* result);
tcl_value_t* tcl_var(struct tcl* tcl, tcl_value_t* name, tcl_value_t* v);

/* Работа со значениями */
const char* tcl_string(tcl_value_t* v);
int tcl_int(tcl_value_t* v);
int tcl_length(tcl_value_t* v);
tcl_value_t* tcl_alloc(const char* s, size_t len);
tcl_value_t* tcl_dup(tcl_value_t* v);
tcl_value_t* tcl_append_string(tcl_value_t* v, const char* s, size_t len);
void tcl_free(tcl_value_t* v);

/* Списки */
tcl_value_t* tcl_list_at(tcl_value_t* v, int index);
int tcl_list_length(tcl_value_t* v);

#ifdef __cplusplus
}
#endif

#endif /* TCL_H_EMBED */
