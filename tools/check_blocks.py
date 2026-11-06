"""Check that blocks/gamepad.json still generates src/v1/main.cpp.

The block editor is JavaScript, so this runs it in headless Chrome/Edge and
reads the generated files from the page. It also compares the saved workspace
with the workspace the editor loads back, ignoring Blockly's random block IDs
and layout positions.

    python tools/check_blocks.py              # check the saved project
    python tools/check_blocks.py --default    # also require the saved project to be the built-in default
"""
import argparse
import difflib
import functools
import html.parser
import http.server
import json
import pathlib
import subprocess
import sys
import tempfile
import threading

from browser import find_browser, run

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROGRAM = ROOT / "blocks" / "gamepad.json"
MAIN_CPP = ROOT / "src" / "v1" / "main.cpp"
EDITOR = "/tools/blocks_editor/index.html"


class PreParser(html.parser.HTMLParser):
    """Collect the text of every <pre id="..."> element in a DOM dump."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.current = None
        self.text = {}

    def handle_starttag(self, tag, attrs):
        if tag == "pre":
            self.current = dict(attrs).get("id")

    def handle_endtag(self, tag):
        if tag == "pre":
            self.current = None

    def handle_data(self, data):
        if self.current:
            self.text.setdefault(self.current, []).append(data)

    def get(self, name):
        return "".join(self.text.get(name, []))


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass


def canonical(program):
    """Drop Blockly IDs and positions so only the program structure remains."""
    if isinstance(program, dict):
        return {
            key: canonical(value)
            for key, value in program.items()
            if key not in ("id", "x", "y")
        }
    if isinstance(program, list):
        return [canonical(item) for item in program]
    return program


def normalized(text):
    return text.replace("\r\n", "\n").replace("\r", "\n")


def show_diff(expected, actual, expected_name, actual_name):
    diff = difflib.unified_diff(
        normalized(expected).splitlines(),
        normalized(actual).splitlines(),
        fromfile=expected_name,
        tofile=actual_name,
        lineterm="",
    )
    print("\n".join(diff), file=sys.stderr)


def serve_repository():
    """Serve the repository on localhost so the editor can fetch gamepad.json."""
    handler = functools.partial(QuietHandler, directory=str(ROOT))
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd, thread


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--default", action="store_true",
                        help="require blocks/gamepad.json to be the built-in default program")
    parser.add_argument("--browser", help="path to Chrome/Edge (default: auto-detect)")
    args = parser.parse_args()

    browser = args.browser or find_browser()
    if not browser:
        sys.exit("No Chrome/Edge found. Install Chrome/Chromium or pass --browser PATH.")

    try:
        saved = json.loads(PROGRAM.read_text(encoding="utf-8"))
    except FileNotFoundError:
        sys.exit("blocks/gamepad.json not found; save the project from the block editor first.")
    except ValueError as err:
        sys.exit("blocks/gamepad.json is not valid JSON: %s" % err)

    try:
        expected_cpp = MAIN_CPP.read_text(encoding="utf-8")
    except FileNotFoundError:
        sys.exit("src/v1/main.cpp not found; save the project from the block editor first.")

    mode = "default" if args.default else "saved"
    httpd, thread = serve_repository()
    url = "http://127.0.0.1:%d%s?generate=%s" % (httpd.server_address[1], EDITOR, mode)

    try:
        try:
            with tempfile.TemporaryDirectory() as profile:
                result = run(
                    browser,
                    profile,
                    ["--dump-dom", url],
                    timeout=120,
                    virtual_time_ms=20000,
                    extra=("--no-sandbox", "--disable-dev-shm-usage"),
                )
        except subprocess.TimeoutExpired:
            sys.exit("headless browser timed out")
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=5)

    if result.returncode != 0:
        print(result.stderr.strip(), file=sys.stderr)
        sys.exit("headless browser failed (exit %d)" % result.returncode)

    dom = result.stdout
    if 'data-generated="yes"' not in dom:
        parser_ = PreParser()
        parser_.feed(dom)
        detail = parser_.get("gp-generated").strip()
        print(result.stderr.strip(), file=sys.stderr)
        sys.exit("the editor did not generate code%s" % (": " + detail if detail else ""))

    parsed = PreParser()
    parsed.feed(dom)
    generated_cpp = parsed.get("gp-generated")
    generated_program_text = parsed.get("gp-program")
    if not generated_cpp or not generated_program_text:
        sys.exit("the editor did not return gp-generated/gp-program")

    failed = False
    if normalized(generated_cpp) != normalized(expected_cpp):
        show_diff(expected_cpp, generated_cpp, "src/v1/main.cpp", "generated main.cpp")
        failed = True

    try:
        generated_program = json.loads(generated_program_text)
    except ValueError as err:
        print("generated workspace is not valid JSON: %s" % err, file=sys.stderr)
        failed = True
    else:
        if canonical(generated_program) != canonical(saved):
            show_diff(
                json.dumps(canonical(saved), indent=2, sort_keys=True),
                json.dumps(canonical(generated_program), indent=2, sort_keys=True),
                "blocks/gamepad.json",
                "loaded workspace",
            )
            failed = True

    if failed:
        sys.exit("\nThe block program and src/v1/main.cpp are out of sync.")

    print("OK: blocks/gamepad.json -> src/v1/main.cpp are in sync (%s program)" % mode)


if __name__ == "__main__":
    main()
