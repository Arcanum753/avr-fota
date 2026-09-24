#include "ESP.h"

MockESP ESP;

static int g_restartCount = 0;

void MockESP::restart() { g_restartCount++; }

int mockEspRestartCount() { return g_restartCount; }
void mockEspRestartReset() { g_restartCount = 0; }
