"""Locate a headless Chrome/Edge and run it for screenshots or DOM dumps."""
import os
import shutil
import subprocess

CANDIDATES = [
    r"%ProgramFiles%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe",
    r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe",
    r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    "google-chrome",
    "chromium",
    "chromium-browser",
    "microsoft-edge",
    "msedge",
]


def find_browser():
    """Return the path to Chrome/Edge, or None if it isn't installed."""
    for candidate in CANDIDATES:
        path = os.path.expandvars(candidate)
        if os.path.isfile(path):
            return path
        found = shutil.which(path)
        if found:
            return found
    return None


def run(browser, profile, args, timeout=180, virtual_time_ms=8000, extra=()):
    """Run Chrome/Edge with a temporary-ish profile and the given arguments."""
    cmd = [
        browser,
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        "--no-first-run",
        "--no-default-browser-check",
        "--user-data-dir=" + str(profile),
        "--virtual-time-budget=%d" % virtual_time_ms,
    ] + list(extra) + list(args)
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                          encoding="utf-8", errors="replace")
