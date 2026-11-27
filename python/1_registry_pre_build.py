import subprocess
import sys
from pathlib import Path

Import("env")

# Предварительная генерация modules_registry.cpp/.h под выбранный env.
# module_registry_gen.py должен запускаться под каждый env: registry не содержит
# #if defined(...) и привязан к ОДНОМУ env. Хук устраняет дрейф при сборке
# другого env без ручного запуска генератора.

env_name = env.subst("$PIOENV")
project_dir = Path(env.subst("$PROJECT_DIR"))
script = project_dir / "python" / "module_registry_gen.py"

print(f"\n[registry_pre_build] Regenerating modules_registry for env: {env_name}")

result = subprocess.run(
    [sys.executable, str(script), "--env", env_name],
    cwd=str(project_dir),
    capture_output=True,
    text=True,
)

if result.stdout:
    sys.stdout.write(result.stdout)
if result.stderr:
    sys.stderr.write(result.stderr)

if result.returncode != 0:
    raise SystemExit(f"[registry_pre_build] ERROR: module_registry_gen failed for env {env_name}")
