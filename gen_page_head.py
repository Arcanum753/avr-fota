#!/usr/bin/env python3
"""
Генератор page_head.html для веб-интерфейса AVR-FOTA.

Создаёт меню навигации на основе списка включённых модулей.
Берёт за основу статический шаблон из data/page_head.html (с левой колонкой core-модулей)
и добавляет правую колонку с пунктами меню для модулей, указанных в --modules.

Использование:
    python gen_page_head.py --modules module_udp,module_prog_isp --output page_head.html
    python gen_page_head.py --modules module_udp,module_prog_swd,module_gpio --output page_head.html

Также может быть импортирован как модуль:
    from gen_page_head import generate_page_head
    html = generate_page_head(["module_udp", "module_prog_isp"])
"""

import argparse
import os
import sys
from typing import List, Optional, Tuple


# ============================================================
# МАППИНГ: имя модуля -> (href, текст, [id])
# ============================================================
MODULE_MENU_ITEMS = {
    "module_udp":        ("udp.html",        "UDP configuration"),
    "module_prog_isp":   ("avr.html",        "AVR Programmer OTA",   "avr"),
    "module_prog_swd":   ("stm32.html",      "STM32 Programmer OTA", "stm32"),
    "module_gpio":       ("gpio.html",       "Esp gpio",             "gpio"),
    "module_otaclient":  ("otaclient.html",  "Firmware & FS OTA client"),
}


# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def log_info(msg: str) -> None:
    print(f"[gen_page_head] {msg}")


def log_warning(msg: str) -> None:
    print(f"[gen_page_head] WARNING: {msg}")


def log_error(msg: str) -> None:
    print(f"[gen_page_head] ERROR: {msg}")


def build_module_link(item: Tuple) -> str:
    """
    Формирует HTML-строку ссылки для пункта меню.
    item может быть: (href, text) или (href, text, id)
    """
    if len(item) == 3:
        href, text, elem_id = item
        return f'        <a id="{elem_id}" href="{href}">{text}</a>'
    else:
        href, text = item
        return f'        <a href="{href}">{text}</a>'


def generate_right_column(modules: List[str]) -> str:
    """
    Генерирует HTML-код для правой колонки меню (второй <div>)
    на основе списка имён модулей.
    """
    lines = ['    <div>']
    has_items = False

    for module_name in modules:
        if module_name in MODULE_MENU_ITEMS:
            link = build_module_link(MODULE_MENU_ITEMS[module_name])
            lines.append(link)
            has_items = True
            log_info(f"  + menu item: {module_name} -> {MODULE_MENU_ITEMS[module_name][0]}")
        else:
            log_warning(f"Unknown module '{module_name}' — no menu item defined")

    lines.append('    </div>')

    if not has_items:
        log_info("  No module menu items to add (right column will be empty)")

    return '\n'.join(lines)


# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ ГЕНЕРАЦИИ
# ============================================================

def generate_page_head(
    modules: List[str],
    template_path: Optional[str] = None
) -> str:
    """
    Генерирует полный HTML-код page_head.html.

    Аргументы:
        modules: список имён модулей (например, ["module_udp", "module_prog_isp"])
        template_path: путь к шаблону page_head.html.
                       Если None — ищет в data/page_head.html относительно
                       директории скрипта.

    Возвращает:
        Строку с полным HTML-кодом page_head.html
    """
    # Определяем путь к шаблону
    if template_path is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        template_path = os.path.join(script_dir, "data", "page_head.html")

    # Читаем шаблон
    if not os.path.exists(template_path):
        log_error(f"Template not found: {template_path}")
        # Возвращаем минимальный fallback
        return _generate_fallback(modules)

    try:
        with open(template_path, 'r', encoding='utf-8') as f:
            template = f.read()
    except Exception as e:
        log_error(f"Failed to read template {template_path}: {e}")
        return _generate_fallback(modules)

    # Генерируем правую колонку
    right_column = generate_right_column(modules)

    # Ищем маркер для замены: комментарий <!-- MODULES_RIGHT_COLUMN -->
    # вместе со следующим за ним пустым <div></div>
    marker_block = "<!-- MODULES_RIGHT_COLUMN -->\n    <div>\n    </div>"
    if marker_block in template:
        result = template.replace(marker_block, right_column)
        log_info("Replaced marker + empty div with generated menu")
    else:
        # Если маркера нет — ищем пустой <div></div> после первого <div>
        # и заменяем его на сгенерированную правую колонку
        import re
        pattern = r'(<div>\s*\n\s*</div>)'
        match = re.search(pattern, template)
        if match:
            result = template.replace(match.group(1), right_column, 1)
            log_info("Replaced empty second <div> with generated menu")
        else:
            log_warning("Could not find placeholder for right column, appending at end")
            # Вставляем перед закрывающим </div> основного контейнера
            result = template.replace('</div>\n</div>', f'{right_column}\n</div>', 1)

    return result


def _generate_fallback(modules: List[str]) -> str:
    """
    Генерирует минимальный page_head.html если шаблон не найден.
    """
    right = generate_right_column(modules)
    return f"""<h3 class="top">Device web-server.<sup>&copy;</sup></h3>
<div class="menu">
    <div>
        <a href="index.html">Main</a>
        <a href="system.html">System configuration</a>
        <a href="wifi.html">WiFi Configuration</a>
        <a target=_tab href="edit.html">SPIFFS File editor</a>
        <a href="update.html">Firmware & FS OTA self</a>
        <a href="project.html">Programmer configuration</a>
    </div>
{right}
</div>
"""


# ============================================================
# CLI ТОЧКА ВХОДА
# ============================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate page_head.html with dynamic menu based on included modules"
    )
    parser.add_argument(
        "--modules",
        required=True,
        help="Comma-separated list of module names (e.g. module_udp,module_prog_isp)"
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output file path (default: <script_dir>/data/page_head.html)"
    )
    parser.add_argument(
        "--template",
        default=None,
        help="Template file path (default: <script_dir>/data/page_head.html)"
    )

    args = parser.parse_args()

    # Парсим список модулей
    modules = [m.strip() for m in args.modules.split(",") if m.strip()]

    if not modules:
        log_error("No modules specified")
        sys.exit(1)

    log_info(f"Generating page_head.html for modules: {', '.join(modules)}")

    # Генерируем HTML
    html = generate_page_head(modules, args.template)

    # Определяем путь для сохранения
    if args.output:
        output_path = args.output
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        output_path = os.path.join(script_dir, "data", "page_head.html")

    # Сохраняем
    try:
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(html)
        log_info(f"Saved to: {output_path}")
    except Exception as e:
        log_error(f"Failed to write {output_path}: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
