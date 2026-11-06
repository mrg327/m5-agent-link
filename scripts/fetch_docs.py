#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["beautifulsoup4", "markdownify", "requests"]
# ///
"""Download M5Stack StopWatch (SKU C152) documentation into docs/.

Usage: uv run scripts/fetch_docs.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import urljoin

import requests
from bs4 import BeautifulSoup
from markdownify import markdownify

ROOT = Path(__file__).resolve().parent.parent / "docs"
BASE = "https://docs.m5stack.com"

# (output path relative to docs/, docs.m5stack.com path)
PAGES = [
    ("StopWatch.md", "/en/core/StopWatch"),
    ("guide/usage.md", "/en/guide/stopwatch/usage"),
    ("guide/restore_factory.md", "/en/guide/stopwatch/restore_factory"),
    ("guide/xiaozhi_voice_assistant.md", "/en/guide/stopwatch/xiaozhi_voice_assistant"),
    ("arduino/setup/arduino_ide.md", "/en/arduino/arduino_ide"),
    ("arduino/setup/arduino_board.md", "/en/arduino/arduino_board"),
    ("arduino/setup/arduino_library.md", "/en/arduino/arduino_library"),
    ("arduino/program.md", "/en/arduino/stopwatch/program"),
    *[
        (f"arduino/api/{n}.md", f"/en/arduino/stopwatch/{n}")
        for n in (
            "battery", "button", "display", "imu", "mic", "rtc",
            "speaker", "touch", "vibration", "m5pm1_m5ioe1",
        )
    ],
    ("uiflow2/program.md", "/en/uiflow2/stopwatch/program"),
    ("uiflow2/uiflow_web.md", "/en/uiflow2/uiflow_web"),
    ("tools/m5burner.md", "/en/uiflow/m5burner/intro"),
    ("esp_idf/esp_idf.md", "/en/esp_idf/esp_idf"),
]

# (output path relative to docs/, raw README URL)
GITHUB_READMES = [
    ("github/M5Unified.md", "https://raw.githubusercontent.com/m5stack/M5Unified/master/README.md"),
    ("github/M5GFX.md", "https://raw.githubusercontent.com/m5stack/M5GFX/master/README.md"),
    ("github/M5PM1.md", "https://raw.githubusercontent.com/m5stack/M5PM1/main/README.md"),
    ("github/M5IOE1.md", "https://raw.githubusercontent.com/m5stack/M5IOE1/main/README.md"),
    ("github/M5StopWatch-UserDemo.md", "https://raw.githubusercontent.com/m5stack/M5StopWatch-UserDemo/main/README.md"),
    ("github/M5StopWatch-PowerDemo.md", "https://raw.githubusercontent.com/m5stack/M5StopWatch-PowerDemo/main/README.md"),
    ("github/hermes-gadget-sdk_STOPWATCH.md", "https://raw.githubusercontent.com/allinnb/hermes-gadget-sdk/main/STOPWATCH.md"),
]

OSS = "https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com"
PDFS = [
    ("StopWatch_product_page.pdf", "https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/pdf/static/en/core/StopWatch.pdf"),
    ("hardware/StopWatch_schematic.pdf", f"{OSS}/1242/C152-SCH_Stopwatch_PRJ_Main_VA_20251201_2026_04_24_17_46_22.pdf"),
    ("hardware/StopWatch_model_size.pdf", f"{OSS}/1242/C152-StopWatch-model-size.pdf"),
    ("datasheets/ESP32-S3_datasheet.pdf", f"{OSS}/472/esp32-s3_datasheet_en.pdf"),
    ("datasheets/M5PM1_power_management.pdf", f"{OSS}/1207/M5PM1_Datasheet_EN.pdf"),
    ("datasheets/M5IOE1_io_expander.pdf", f"{OSS}/1210/IO_Expander_Datasheet_EN.pdf"),
    ("datasheets/ES8311_audio_codec.pdf", "https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/atom/Atomic%20Echo%20Base/ES8311.pdf"),
    ("datasheets/RX8130CE_rtc.pdf", f"{OSS}/1132/RX8130CE_cn.pdf"),
    ("datasheets/RX8130CE_rtc_registers.pdf", f"{OSS}/1132/RX8130CE_cn-Register-Datasheet.pdf"),
    ("datasheets/CO5300_display_driver.pdf", f"{OSS}/1242/CO5300_Datasheet.pdf"),
    ("datasheets/CST820B_touch.pdf", f"{OSS}/1242/CST820B_datasheet.pdf"),
]

session = requests.Session()
session.headers["User-Agent"] = "Mozilla/5.0 (m5-agent-link docs fetcher)"


def get(url: str) -> requests.Response:
    r = session.get(url, timeout=60)
    r.raise_for_status()
    r.encoding = "utf-8"
    return r


def write_text(out: str, text: str) -> None:
    dest = ROOT / out
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text, encoding="utf-8")


def write_bytes(out: str, data: bytes) -> None:
    dest = ROOT / out
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)


def fetch_page(out: str, path: str) -> None:
    url = BASE + path
    soup = BeautifulSoup(get(url).text, "html.parser")
    body = soup.select_one("div.markdown")
    if body is None:
        raise RuntimeError(f"no div.markdown in {url}")
    for wrapper in body.select("div.code-block-wrapper"):
        raw = wrapper.select_one("button.copy-all-btn code") or wrapper.select_one("pre code")
        label = wrapper.select_one(".language-label")
        lang = label.get_text(strip=True) if label else ""
        pre = soup.new_tag("pre")
        code = soup.new_tag("code", attrs={"class": f"language-{lang}"} if lang else {})
        code.string = raw.get_text() if raw else ""
        pre.append(code)
        wrapper.replace_with(pre)
    for tag in body.select("script, style, .fa-copy, .markdown-action-menu, .carousel-container"):
        tag.decompose()
    for attr, name in (("href", "a"), ("src", "img")):
        for t in body.find_all(name):
            if t.get(attr):
                t[attr] = urljoin(url, t[attr])
    md = markdownify(
        str(body),
        heading_style="ATX",
        code_language_callback=lambda el: next(
            (c.removeprefix("language-") for c in (el.code or el).get("class", []) if c.startswith("language-")),
            None,
        ),
    )
    md = re.sub(r"\n{3,}", "\n\n", md).strip()
    write_text(out, f"> Source: {url}\n\n{md}\n")


def main() -> int:
    jobs = (
        [(o, lambda o=o, p=p: fetch_page(o, p)) for o, p in PAGES]
        + [(o, lambda o=o, u=u: write_text(o, f"> Source: {u}\n\n{get(u).text}")) for o, u in GITHUB_READMES]
        + [(o, lambda o=o, u=u: write_bytes(o, get(u).content)) for o, u in PDFS]
    )
    failures = []
    for out, job in jobs:
        try:
            job()
            print(f"  ok  {out}")
        except Exception as e:  # keep going; report all failures at the end
            failures.append(out)
            print(f"FAIL  {out}: {e}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
