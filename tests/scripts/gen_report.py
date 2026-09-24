#!/usr/bin/env python3
"""tests/scripts/gen_report.py

Собирает JUnit XML из каталога отчётов ядра, формирует сводный junit.xml
и HTML-отчёт. Зависимости — только stdlib.
"""

import argparse
import glob
import os
import xml.etree.ElementTree as ET
from datetime import datetime


def parse_junit(path):
    try:
        tree = ET.parse(path)
    except Exception:
        return None
    root = tree.getroot()
    suites = [root] if root.tag == "testsuite" else root.findall("testsuite")
    stats = {"tests": 0, "failures": 0, "errors": 0, "skipped": 0, "time": 0.0}
    cases = []
    for suite in suites:
        for case in suite.findall("testcase"):
            name = case.get("name", "?")
            classname = case.get("classname", "")
            status = "pass"
            if case.find("failure") is not None:
                status = "fail"
            elif case.find("error") is not None:
                status = "error"
            elif case.find("skipped") is not None:
                status = "skip"
            cases.append({"classname": classname, "name": name, "status": status})
            stats["tests"] += 1
            if status == "fail":
                stats["failures"] += 1
            elif status == "error":
                stats["errors"] += 1
            elif status == "skip":
                stats["skipped"] += 1
            try:
                stats["time"] += float(case.get("time", 0) or 0)
            except ValueError:
                pass
    return stats, cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="indir", required=True)
    ap.add_argument("--out", dest="out", required=True)
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.indir, "**", "*junit*.xml"), recursive=True))
    # Не читать собственный сводный junit.xml из _summary — иначе на повторном
    # прогоне ошибки прошлого раза засчитываются снова (двойной учёт).
    summary_dir = os.path.dirname(os.path.abspath(args.out))
    files = [f for f in files
             if os.path.dirname(os.path.abspath(f)) != summary_dir]
    reports = []
    for f in files:
        parsed = parse_junit(f)
        if parsed:
            rel = os.path.relpath(f, args.indir)
            reports.append((rel, parsed[0], parsed[1]))

    total = {"tests": 0, "failures": 0, "errors": 0, "skipped": 0}
    for _, s, _ in reports:
        for k in total:
            total[k] += s[k]

    # Сводный JUnit
    suite = ET.Element("testsuite", {
        "name": "core-summary",
        "tests": str(total["tests"]),
        "failures": str(total["failures"]),
        "errors": str(total["errors"]),
        "skipped": str(total["skipped"]),
        "timestamp": datetime.now().isoformat(timespec="seconds"),
    })
    for rel, s, cases in reports:
        for c in cases:
            tc = ET.SubElement(suite, "testcase", {
                "classname": rel.replace(os.sep, "/"),
                "name": c["name"],
            })
            if c["status"] == "fail":
                ET.SubElement(tc, "failure")
            elif c["status"] == "error":
                ET.SubElement(tc, "error")
            elif c["status"] == "skip":
                ET.SubElement(tc, "skipped")

    summary_dir = os.path.dirname(args.out)
    if summary_dir:
        os.makedirs(summary_dir, exist_ok=True)
    ET.ElementTree(suite).write(os.path.join(summary_dir, "junit.xml"),
                                encoding="utf-8", xml_declaration=True)

    # HTML
    rows = []
    for rel, s, _ in reports:
        rows.append(
            "<tr><td>{}</td><td>{}</td><td class='fail'>{}</td>"
            "<td class='err'>{}</td><td>{}</td></tr>".format(
                rel.replace(os.sep, "/"), s["tests"], s["failures"], s["errors"], s["skipped"])
        )
    html = """<!DOCTYPE html>
<html lang="ru"><head><meta charset="utf-8"><title>Core tests report</title>
<style>
body {{ font-family: monospace; margin: 24px; }}
table {{ border-collapse: collapse; }}
td, th {{ border: 1px solid #999; padding: 4px 10px; }}
.fail {{ color: #b00; font-weight: bold; }}
.err {{ color: #b00; }}
</style></head><body>
<h1>Ядро avr-fota — сводка тестов</h1>
<p>Сформировано: {ts}</p>
<p>Всего: {tests}; упало: {failures}; ошибок: {errors}; пропущено: {skipped}</p>
<table><thead><tr><th>Отчёт</th><th>Тестов</th><th>Fail</th><th>Error</th><th>Skip</th></tr></thead>
<tbody>
{rows}
</tbody></table>
</body></html>
""".format(ts=datetime.now().isoformat(timespec="seconds"),
           tests=total["tests"], failures=total["failures"],
           errors=total["errors"], skipped=total["skipped"],
           rows="\n".join(rows))

    with open(args.out, "w", encoding="utf-8") as f:
        f.write(html)

    print("[gen_report] junit-файлов: {}, тестов: {}, fail: {}, error: {}, skip: {}".format(
        len(reports), total["tests"], total["failures"], total["errors"], total["skipped"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
