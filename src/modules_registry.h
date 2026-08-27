#ifndef _MODULES_REGISTRY_h
#define _MODULES_REGISTRY_h

// Файл генерируется python/module_registry_gen.py под выбранный env.
// Не редактировать вручную. При смене env — перезапустить генератор.

#include "mod_context.h"

void core_begin(ModContext& ctx);
void modules_begin(ModContext& ctx);
void dev_begin(ModContext& ctx);

void core_web_Init();
void modules_web_Init();
void dev_web_Init();

void core_loop();
void modules_loop();
void dev_loop();

#endif // _MODULES_REGISTRY_h
