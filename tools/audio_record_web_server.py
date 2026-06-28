from __future__ import annotations

import argparse
import json
import posixpath
import threading
import wave
from datetime import datetime
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote


HTML_PAGE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Audio Record Uploads</title>
</head>
<body>
  <p id="status">Server online</p>
  <p>Latest file: <span id="filename">none</span></p>
  <p>Duration: <span id="duration">0.000</span>s</p>
  <p>Saved path: <span id="saved-path">n/a</span></p>
  <audio id="player" controls></audio>
  <script>
    async function refreshLatest() {
      const response = await fetch('/api/latest');
      const latest = await response.json();
      document.getElementById('filename').textContent = latest.filename || 'none';
      document.getElementById('duration').textContent = latest.duration_s ?? 0;
      document.getElementById('saved-path').textContent = latest.saved_path || 'n/a';
      const player = document.getElementById('player');
      if (latest.capture_url && player.dataset.src !== latest.capture_url) {
        player.src = latest.capture_url;
        player.dataset.src = latest.capture_url;
      }
    }
    refreshLatest();
    setInterval(refreshLatest, 1000);
  </script>
</body>
</html>
"""


class AudioRecordWebServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], captures_dir: Path):
        super().__init__(server_address, AudioRecordRequestHandler)
        self.captures_dir = Path(captures_dir)
        self.captures_dir.mkdir(parents=True, exist_ok=True)
        self._latest: dict[str, object] | None = None
        self._state_lock = threading.Lock()

    def build_capture_path(self) -> Path:
        while True:
            stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S_%f")
            path = self.captures_dir / f"capture_{stamp}.wav"
            if not path.exists():
                return path

    def set_latest(self, metadata: dict[str, object]) -> None:
        with self._state_lock:
            self._latest = dict(metadata)

    def get_latest(self) -> dict[str, object]:
        with self._state_lock:
            if self._latest is None:
                return {
                    "ok": True,
                    "filename": None,
                    "bytes": 0,
                    "duration_s": 0.0,
                    "saved_path": None,
                    "capture_url": None,
                }
            return dict(self._latest)


class AudioRecordRequestHandler(BaseHTTPRequestHandler):
    server: AudioRecordWebServer

    def do_GET(self) -> None:
        if self.path == "/":
            self._send_html(HTML_PAGE)
            return

        if self.path == "/api/latest":
            self._send_json(self.server.get_latest())
            return

        if self.path.startswith("/captures/"):
            self._serve_capture()
            return

        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        if self.path != "/api/upload":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        content_length = self.headers.get("Content-Length")
        if content_length is None:
            self._send_json({"ok": False, "error": "missing Content-Length"}, status=HTTPStatus.LENGTH_REQUIRED)
            return

        try:
            body_size = int(content_length)
        except ValueError:
            self._send_json({"ok": False, "error": "invalid Content-Length"}, status=HTTPStatus.BAD_REQUEST)
            return

        wav_bytes = self.rfile.read(body_size)
        try:
            duration_s = read_wav_duration_seconds(wav_bytes)
        except (wave.Error, EOFError) as exc:
            self._send_json({"ok": False, "error": f"invalid wav: {exc}"}, status=HTTPStatus.BAD_REQUEST)
            return

        capture_path = self.server.build_capture_path()
        capture_path.write_bytes(wav_bytes)
        metadata = {
            "ok": True,
            "filename": capture_path.name,
            "bytes": len(wav_bytes),
            "duration_s": duration_s,
            "saved_path": str(capture_path.resolve()),
            "capture_url": f"/captures/{capture_path.name}",
        }
        self.server.set_latest(metadata)
        self._send_json(metadata)

    def _serve_capture(self) -> None:
        raw_name = self.path.removeprefix("/captures/")
        filename = Path(unquote(posixpath.normpath(raw_name))).name
        if not filename:
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        capture_path = self.server.captures_dir / filename
        if not capture_path.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        data = capture_path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_json(self, payload: dict[str, object], status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_html(self, html: str) -> None:
        body = html.encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args) -> None:
        return


def read_wav_duration_seconds(wav_bytes: bytes) -> float:
    import io

    with wave.open(io.BytesIO(wav_bytes), "rb") as wav_file:
        frame_rate = wav_file.getframerate()
        frame_count = wav_file.getnframes()
        if frame_rate <= 0:
            return 0.0
        return frame_count / float(frame_rate)


def create_server(host: str, port: int, captures_dir: Path | str) -> AudioRecordWebServer:
    return AudioRecordWebServer((host, port), Path(captures_dir))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve uploaded audio recordings and a local playback page.")
    parser.add_argument("--host", default="127.0.0.1", help="Host interface to bind, default: 127.0.0.1")
    parser.add_argument("--port", type=int, default=8000, help="Port to listen on, default: 8000")
    parser.add_argument("--captures-dir", default="captures", help="Directory where uploaded WAV files are stored")
    return parser.parse_args()


def run() -> int:
    args = parse_args()
    server = create_server(args.host, args.port, Path(args.captures_dir))
    print(f"serving on http://{args.host}:{server.server_address[1]}")
    print(f"captures dir: {server.captures_dir.resolve()}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
