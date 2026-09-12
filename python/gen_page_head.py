#!/usr/bin/env python3
"""
Генератор page_head.html для веб-интерфейса AVR-FOTA.

Создаёт меню навигации на основе списка включённых модулей.
Берёт за основу статический шаблон из data/page_head.html (с левой колонкой core-модулей)
и добавляет правую колонку с пунктами меню для модулей.

Пункты меню берутся из файла _menu.html в папке web каждого модуля.
Если _menu.html отсутствует, пуст или содержит невалидные ссылки —
генерируются ссылки по умолчанию из имён .html файлов модуля.

Использование:
    python gen_page_head.py --modules module_udp,module_prog_isp --src_dir src --output page_head.html
    python gen_page_head.py --modules module_udp,module_prog_swd,module_gpio --src_dir src --output page_head.html

Также может быть импортирован как модуль:
    from gen_page_head import generate_page_head
    html = generate_page_head(["module_udp", "module_prog_isp"], "src")
"""

import argparse
import os
import re
import sys
from pathlib import Path
from typing import List, Optional, Set, Tuple


# ============================================================
# КОНСТАНТЫ
# ============================================================

MENU_CONFIG_FILE = "_menu.html"       # имя файла конфига меню в папке web модуля
WEB_FOLDER_NAME = "web"               # имя папки с веб-файлами внутри модуля
MODULE_PREFIX = "module_"             # префикс модулей
DEVICE_PREFIX = "device_"             # префикс девайс-модулей

# Левая колонка меню (core-страницы). Единый источник: используется и при
# генерации page_head.html из шаблона, и в запасном меню _generate_fallback().
# При добавлении новой core-страницы править только здесь (шаблон data/page_head.html
# остаётся каркасом и его левая колонка при наличии маркера не используется).
STATIC_MENU_LINKS = """    <div>
        <a href="index.html">Main</a>
        <a href="ntp.html">NTP configuration</a>
        <a href="system.html">System configuration</a>
        <a href="wifi.html">WiFi Configuration</a>
        <a href="update.html">Esp Firmware & FS OTA update</a>
        <a href="state.html">Resources</a>
    </div>
"""


# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def log_info(msg: str) -> None:
    print(f"[gen_page_head] {msg}")


def log_warning(msg: str) -> None:
    print(f"[gen_page_head] WARNING: {msg}")


def log_error(msg: str) -> None:
    print(f"[gen_page_head] ERROR: {msg}")


def parse_menu_links(menu_html: str) -> List[Tuple[str, str, Optional[str], Optional[str]]]:
    """
    Парсит HTML-строку и извлекает все теги <a>.
    
    Возвращает список кортежей (href, text, id_or_None, target_or_None).
    """
    links = []
    # Ищем <a ... href="..." ...>...</a>
    pattern = r'<a\s+([^>]*?)href="([^"]*)"([^>]*?)>(.*?)</a>'
    
    for match in re.finditer(pattern, menu_html, re.IGNORECASE | re.DOTALL):
        href = match.group(2).strip()
        text = match.group(4).strip()
        
        # Извлекаем id из атрибутов
        all_attrs = match.group(1) + ' ' + match.group(3)
        id_match = re.search(r'id="([^"]*)"', all_attrs)
        elem_id = id_match.group(1).strip() if id_match else None

        # Извлекаем target (в кавычках или без)
        target_match = (re.search(r'target\s*=\s*"([^"]*)"', all_attrs)
                        or re.search(r'target\s*=\s*([^\s>]+)', all_attrs))
        target = target_match.group(1).strip() if target_match else None
        
        if href and text:
            links.append((href, text, elem_id, target))
    
    return links


def build_link_html(href: str, text: str, elem_id: Optional[str] = None,
                    target: Optional[str] = None) -> str:
    """Формирует HTML-строку ссылки для пункта меню."""
    attrs = ""
    if elem_id:
        attrs += f' id="{elem_id}"'
    if target:
        attrs += f' target="{target}"'
    return f'        <a{attrs} href="{href}">{text}</a>'


def get_default_links(web_dir: Path) -> List[Tuple[str, str, Optional[str], Optional[str]]]:
    """
    Генерирует ссылки по умолчанию из .html файлов в папке web модуля.
    Исключает файлы, начинающиеся с '_'.
    Для каждого файла: <a href="filename.html">filename</a>
    """
    links = []
    
    if not web_dir.exists() or not web_dir.is_dir():
        return links
    
    try:
        for item in sorted(web_dir.iterdir()):
            if not item.is_file():
                continue
            if item.name.startswith("_"):
                continue
            if not item.name.lower().endswith(".html"):
                continue
            
            name_without_ext = item.name[:-5]  # удаляем .html
            links.append((item.name, name_without_ext, None, None))
    except Exception as e:
        log_warning(f"Error scanning {web_dir} for default links: {e}")
    
    return links


def get_module_menu_links(module_name: str, src_dir: Path) -> List[Tuple[str, str, Optional[str], Optional[str]]]:
    """
    Получает пункты меню для указанного модуля.
    
    Приоритет:
    1. _menu.html в папке web модуля (с проверкой существования файлов)
    2. Если _menu.html нет/пуст/все ссылки невалидны — автогенерация из .html файлов
    
    Возвращает список кортежей (href, text, id_or_None, target_or_None).
    """
    web_dir = src_dir / module_name / WEB_FOLDER_NAME
    
    if not web_dir.exists() or not web_dir.is_dir():
        log_warning(f"Module '{module_name}' has no web folder: {web_dir}")
        return []
    
    menu_config = web_dir / MENU_CONFIG_FILE
    
    # Пробуем прочитать _menu.html
    if menu_config.exists() and menu_config.is_file():
        try:
            content = menu_config.read_text(encoding='utf-8').strip()
            
            if not content:
                log_warning(f"  {module_name}: {MENU_CONFIG_FILE} is empty, using default links")
                return get_default_links(web_dir)
            
            # Парсим ссылки
            parsed_links = parse_menu_links(content)
            
            if not parsed_links:
                log_warning(f"  {module_name}: {MENU_CONFIG_FILE} has no valid <a> tags, using default links")
                return get_default_links(web_dir)
            
            # Проверяем существование файлов, на которые ссылаются
            valid_links = []
            for href, text, elem_id, target in parsed_links:
                target_file = web_dir / href
                if target_file.exists() and target_file.is_file():
                    valid_links.append((href, text, elem_id, target))
                else:
                    log_warning(f"  {module_name}: file '{href}' not found in web folder, skipping menu link")
            
            if not valid_links:
                log_warning(f"  {module_name}: no valid files referenced in {MENU_CONFIG_FILE}, using default links")
                return get_default_links(web_dir)
            
            log_info(f"  {module_name}: {len(valid_links)} menu items from {MENU_CONFIG_FILE}")
            return valid_links
            
        except Exception as e:
            log_warning(f"  {module_name}: error reading {MENU_CONFIG_FILE}: {e}, using default links")
            return get_default_links(web_dir)
    
    # _menu.html нет — используем автогенерацию
    log_info(f"  {module_name}: no {MENU_CONFIG_FILE}, using auto-generated links from .html files")
    return get_default_links(web_dir)


# ============================================================
# ОСНОВНЫЕ ФУНКЦИИ
# ============================================================

def generate_right_column(modules: List[str], src_dir: Path) -> str:
    """
    Генерирует HTML-код для правой колонки меню (второй <div>)
    на основе списка имён модулей.
    
    Аргументы:
        modules: список имён модулей (например, ["module_udp", "module_prog_isp"])
        src_dir: путь к папке src/ проекта
    
    Возвращает:
        Строку с HTML-кодом правой колонки меню
    """
    lines = ['    <div>']
    has_items = False
    
    for module_name in modules:
        module_links = get_module_menu_links(module_name, src_dir)
        
        for href, text, elem_id, target in module_links:
            link_html = build_link_html(href, text, elem_id, target)
            lines.append(link_html)
            has_items = True
    
    lines.append('    </div>')
    
    if not has_items:
        log_info("  No module menu items to add (right column will be empty)")
    
    return '\n'.join(lines)


def generate_page_head(
    modules: List[str],
    src_dir: Optional[str] = None,
    template_path: Optional[str] = None
) -> str:
    """
    Генерирует полный HTML-код page_head.html.
    
    Аргументы:
        modules: список имён модулей (например, ["module_udp", "module_prog_isp"])
        src_dir: путь к папке src/ проекта.
                 Если None — ищет <корень проекта>/src.
        template_path: путь к шаблону page_head.html.
                       Если None — ищет <корень проекта>/data/page_head.html.
    
    Возвращает:
        Строку с полным HTML-кодом page_head.html
    """
    # Корень проекта — родитель директории скрипта (python/), где лежит platformio.ini
    script_dir = Path(os.path.dirname(os.path.abspath(__file__)))
    project_root = script_dir.parent

    # Определяем путь к src/
    if src_dir is None:
        src_dir = project_root / "src"
    else:
        src_dir = Path(src_dir)
    
    # Определяем путь к шаблону
    if template_path is None:
        template_path = str(project_root / "src" / "core_web" / "web" / "page_head.html")
    
    # Читаем шаблон
    if not os.path.exists(template_path):
        log_error(f"Template not found: {template_path}")
        return _generate_fallback(modules, src_dir)
    
    try:
        with open(template_path, 'r', encoding='utf-8') as f:
            template = f.read()
    except Exception as e:
        log_error(f"Failed to read template {template_path}: {e}")
        return _generate_fallback(modules, src_dir)
    
    # Генерируем правую колонку
    right_column = generate_right_column(modules, src_dir)
    
    # Ищем маркер для замены: комментарий <!-- MODULES_RIGHT_COLUMN -->
    # вместе со следующим за ним пустым <div></div>
    marker_block = "<!-- MODULES_RIGHT_COLUMN -->\n    <div>\n    </div>"
    if '<div class="menu">' in template:
        # Меню целиком строится из STATIC_MENU_LINKS (левая колонка) и правой
        # колонки модулей — левая колонка шаблона не используется как источник
        # истины, чтобы списки в page_head.html и STATIC_MENU_LINKS не расходились.
        head = template.split('<div class="menu">', 1)[0]
        result = head + '<div class="menu">\n' + STATIC_MENU_LINKS + right_column + '\n</div>\n'
        log_info("Generated menu from STATIC_MENU_LINKS + module links")
    elif marker_block in template:
        result = template.replace(marker_block, right_column)
        log_info("Replaced marker + empty div with generated menu")
    else:
        # Если шаблон неизвестной структуры — ищем пустой <div></div> после первого
        # <div> и заменяем его на сгенерированную правую колонку
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


def _generate_fallback(modules: List[str], src_dir: Path) -> str:
    """
    Генерирует минимальный page_head.html если шаблон не найден.
    """
    right = generate_right_column(modules, src_dir)
    return f"""<h3 class="top">Device web-server.<sup>&copy;</sup></h3>
<div class="menu">
{STATIC_MENU_LINKS}
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
        "--src_dir",
        default=None,
        help="Path to src/ directory (default: <project_root>/src)"
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output file path (default: <project_root>/data/page_head.html)"
    )
    parser.add_argument(
        "--template",
        default=None,
        help="Template file path (default: <project_root>/data/page_head.html)"
    )

    args = parser.parse_args()

    # Парсим список модулей
    modules = [m.strip() for m in args.modules.split(",") if m.strip()]

    if not modules:
        log_error("No modules specified")
        sys.exit(1)

    log_info(f"Generating page_head.html for modules: {', '.join(modules)}")

    # Генерируем HTML
    html = generate_page_head(modules, args.src_dir, args.template)

    # Определяем путь для сохранения
    if args.output:
        output_path = args.output
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        output_path = os.path.join(script_dir, "..", "src", "core_web", "web", "page_head.html")

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
