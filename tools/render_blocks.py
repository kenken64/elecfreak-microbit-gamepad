"""Render docs/blocks/firmware-blocks.html into the PNG images used by README.md.

Each section of the block page (start, loop, bluetooth, functions, legend) is
rendered on its own, in a light and a dark version, with headless Chrome or
Edge:

    python tools/render_blocks.py

Re-run it after editing docs/blocks/firmware-blocks.html.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGE = ROOT / "docs" / "blocks" / "firmware-blocks.html"
OUT = PAGE.parent

SECTIONS = ["start", "loop", "bluetooth", "functions", "legend"]
THEMES = ["light", "dark"]
WIDTH = 860  # CSS px; below the page's 900 px breakpoint, so notes sit under the blocks
SCALE = 2    # device pixel ratio for crisp images

CANDIDATES = [
    r"%ProgramFiles%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe",
    r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe",
    r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe",
    "google-chrome", "chromium", "chromium-browser", "microsoft-edge",
]


def find_browser():
    for c in CANDIDATES:
        path = os.path.expandvars(c)
        if os.path.isfile(path):
            return path
        found = shutil.which(path)
        if found:
            return found
    sys.exit("No Chrome/Edge found for headless rendering")


def run(browser, profile, args):
    cmd = [browser, "--headless=new", "--disable-gpu", "--hide-scrollbars",
           "--no-first-run", "--no-default-browser-check",
           "--user-data-dir=" + profile, "--virtual-time-budget=8000"] + args
    return subprocess.run(cmd, capture_output=True, text=True, timeout=180,
                          encoding="utf-8", errors="replace")


def main():
    browser = find_browser()
    print("browser:", browser)
    with tempfile.TemporaryDirectory() as profile:
        for section in SECTIONS:
            for theme in THEMES:
                url = PAGE.as_uri() + "?section=%s&theme=%s" % (section, theme)
                dom = run(browser, profile, ["--window-size=%d,600" % WIDTH, "--dump-dom", url]).stdout
                m = re.search(r'data-height="(\d+)"', dom)
                if not m:
                    sys.exit("could not measure section %r" % section)
                height = int(m.group(1))
                png = OUT / ("%s-%s.png" % (section, theme))
                run(browser, profile, ["--window-size=%d,%d" % (WIDTH, height),
                                       "--force-device-scale-factor=%d" % SCALE,
                                       "--screenshot=" + str(png), url])
                if not png.is_file():
                    sys.exit("screenshot failed for %s" % png.name)
                print("%-22s %dx%d css px" % (png.name, WIDTH, height))


if __name__ == "__main__":
    main()
