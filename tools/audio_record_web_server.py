from __future__ import annotations

import argparse
import base64
import os
import json
import posixpath
import struct
import threading
import urllib.error
import urllib.request
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
  <p>Preview gain: <span id="preview-gain">1.00</span>x</p>
  <p>Peak abs: <span id="peak-abs">0</span></p>
  <p>ASR status: <span id="asr-status">disabled</span></p>
  <p>ASR model: <span id="asr-model">n/a</span></p>
  <p>Language: <span id="asr-language">n/a</span></p>
  <p>Emotion: <span id="asr-emotion">n/a</span></p>
  <p>Transcript: <span id="transcript-text">n/a</span></p>
  <p>ASR error: <span id="asr-error">n/a</span></p>
  <p>Preview playback uses an auto-boosted WAV for easier listening. Raw upload still stays untouched on disk.</p>
  <audio id="player" controls></audio>
  <script>
    async function refreshLatest() {
      const response = await fetch('/api/latest');
      const latest = await response.json();
      document.getElementById('filename').textContent = latest.filename || 'none';
      document.getElementById('duration').textContent = latest.duration_s ?? 0;
      document.getElementById('saved-path').textContent = latest.saved_path || 'n/a';
      document.getElementById('preview-gain').textContent = (latest.preview_gain ?? 1).toFixed(2);
      document.getElementById('peak-abs').textContent = latest.peak_abs ?? 0;
      document.getElementById('asr-status').textContent = latest.asr_status || 'n/a';
      document.getElementById('asr-model').textContent = latest.asr_model || 'n/a';
      document.getElementById('asr-language').textContent = latest.asr_language || 'n/a';
      document.getElementById('asr-emotion').textContent = latest.asr_emotion || 'n/a';
      document.getElementById('transcript-text').textContent = latest.transcript_text || 'n/a';
      document.getElementById('asr-error').textContent = latest.asr_error || 'n/a';
      const player = document.getElementById('player');
      const playerUrl = latest.player_url || latest.capture_url;
      if (playerUrl && player.dataset.src !== playerUrl) {
        player.src = playerUrl;
        player.dataset.src = playerUrl;
      }
    }
    refreshLatest();
    setInterval(refreshLatest, 1000);
  </script>
</body>
</html>
"""

MAX_UPLOAD_BYTES = 512 * 1024
PREVIEW_TARGET_PEAK = 12000
PREVIEW_MAX_GAIN = 512.0
DEFAULT_ASR_BASE_URL = "https://your-workspace.cn-beijing.maas.aliyuncs.com/compatible-mode/v1"
DEFAULT_ASR_MODEL = "qwen3-asr-flash"


def load_asr_config() -> dict[str, object]:
    api_key = os.environ.get("DASHSCOPE_API_KEY")
    return {
        "api_key": api_key,
        "enabled": bool(api_key),
        "base_url": os.environ.get("ASR_BASE_URL", DEFAULT_ASR_BASE_URL),
        "model": os.environ.get("ASR_MODEL", DEFAULT_ASR_MODEL),
    }


def make_default_asr_fields(config: dict[str, object]) -> dict[str, object]:
    if config.get("enabled"):
        status = "idle"
        error = None
        model = config.get("model")
    else:
        status = "disabled"
        error = "ASR disabled: missing DASHSCOPE_API_KEY"
        model = None
    return {
        "asr_status": status,
        "transcript_text": None,
        "asr_error": error,
        "asr_model": model,
        "asr_language": None,
        "asr_emotion": None,
    }


def transcribe_wav_with_asr(wav_bytes: bytes, config: dict[str, object]) -> dict[str, object]:
    if not config.get("enabled"):
        raise RuntimeError("ASR disabled: missing DASHSCOPE_API_KEY")

    data_url = "data:audio/wav;base64," + base64.b64encode(wav_bytes).decode("ascii")
    payload = {
        "model": config.get("model", DEFAULT_ASR_MODEL),
        "messages": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_audio",
                        "input_audio": {
                            "data": data_url,
                        },
                    }
                ],
            }
        ],
        "stream": False,
        "asr_options": {
            "enable_itn": False,
        },
    }

    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        str(config.get("base_url", DEFAULT_ASR_BASE_URL)).rstrip("/") + "/chat/completions",
        data=body,
        headers={
            "Authorization": f"Bearer {config.get('api_key')}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            response_body = response.read()
    except urllib.error.HTTPError as exc:
        details = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"ASR HTTP {exc.code}: {details}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"ASR request failed: {exc}") from exc

    try:
        response_payload = json.loads(response_body.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise RuntimeError("ASR returned invalid JSON") from exc

    if isinstance(response_payload, dict) and "error" in response_payload:
        raise RuntimeError(str(response_payload["error"]))

    choices = response_payload.get("choices")
    if not isinstance(choices, list) or not choices:
        raise RuntimeError("ASR response missing choices")
    message = choices[0].get("message")
    if not isinstance(message, dict):
        raise RuntimeError("ASR response missing message")
    transcript_text = message.get("content")
    if not isinstance(transcript_text, str):
        raise RuntimeError("ASR response missing transcript text")

    asr_language = None
    asr_emotion = None
    annotations = message.get("annotations")
    if isinstance(annotations, list) and annotations:
        first = annotations[0]
        if isinstance(first, dict):
            asr_language = first.get("language")
            asr_emotion = first.get("emotion")

    return {
        "transcript_text": transcript_text,
        "asr_model": payload["model"],
        "asr_language": asr_language,
        "asr_emotion": asr_emotion,
    }


class AudioRecordWebServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], captures_dir: Path):
        super().__init__(server_address, AudioRecordRequestHandler)
        self.captures_dir = Path(captures_dir)
        self.captures_dir.mkdir(parents=True, exist_ok=True)
        self.asr_config = load_asr_config()
        self._latest: dict[str, object] | None = None
        self._state_lock = threading.Lock()
        self._recover_latest_from_disk()

    def store_capture(self, wav_bytes: bytes) -> Path:
        while True:
            stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S_%f")
            path = self.captures_dir / f"capture_{stamp}.wav"
            try:
                with path.open("xb") as capture_file:
                    capture_file.write(wav_bytes)
                return path
            except FileExistsError:
                continue

    def store_capture_variant(self, filename: str, data: bytes) -> Path:
        path = self.captures_dir / filename
        path.write_bytes(data)
        return path

    def set_latest(self, metadata: dict[str, object]) -> None:
        with self._state_lock:
            self._latest = dict(metadata)

    def get_latest(self) -> dict[str, object]:
        with self._state_lock:
            if self._latest is None:
                payload = {
                    "ok": True,
                    "filename": None,
                    "bytes": 0,
                    "duration_s": 0.0,
                    "saved_path": None,
                    "capture_url": None,
                    "preview_url": None,
                    "player_url": None,
                    "peak_abs": 0,
                    "preview_gain": 1.0,
                }
                payload.update(make_default_asr_fields(self.asr_config))
                return payload
            return dict(self._latest)

    def sidecar_path_for_capture(self, capture_path: Path) -> Path:
        return capture_path.with_suffix(".asr.json")

    def write_asr_sidecar(self, capture_path: Path, metadata: dict[str, object]) -> None:
        payload = {
            "asr_status": metadata.get("asr_status"),
            "transcript_text": metadata.get("transcript_text"),
            "asr_error": metadata.get("asr_error"),
            "asr_model": metadata.get("asr_model"),
            "asr_language": metadata.get("asr_language"),
            "asr_emotion": metadata.get("asr_emotion"),
        }
        self.sidecar_path_for_capture(capture_path).write_text(
            json.dumps(payload, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

    def update_latest_asr(self, capture_filename: str, asr_fields: dict[str, object]) -> None:
        with self._state_lock:
            if self._latest is None or self._latest.get("filename") != capture_filename:
                return
            self._latest.update(asr_fields)
            latest_copy = dict(self._latest)
        capture_path = self.captures_dir / capture_filename
        try:
            self.write_asr_sidecar(capture_path, latest_copy)
        except OSError:
            return

    def start_asr_job(self, capture_path: Path, wav_bytes: bytes) -> None:
        capture_filename = capture_path.name

        def worker() -> None:
            try:
                result = transcribe_wav_with_asr(wav_bytes, self.asr_config)
                asr_fields = {
                    "asr_status": "done",
                    "transcript_text": result.get("transcript_text"),
                    "asr_error": None,
                    "asr_model": result.get("asr_model", self.asr_config.get("model")),
                    "asr_language": result.get("asr_language"),
                    "asr_emotion": result.get("asr_emotion"),
                }
            except Exception as exc:
                asr_fields = {
                    "asr_status": "error",
                    "transcript_text": None,
                    "asr_error": str(exc),
                    "asr_model": self.asr_config.get("model"),
                    "asr_language": None,
                    "asr_emotion": None,
                }
            self.update_latest_asr(capture_filename, asr_fields)

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()

    def _recover_latest_from_disk(self) -> None:
        raw_files = sorted(
            (
                path
                for path in self.captures_dir.glob("capture_*.wav")
                if not path.name.endswith("_preview.wav")
            ),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
        if not raw_files:
            return

        capture_path = raw_files[0]
        try:
            wav_bytes = capture_path.read_bytes()
            duration_s = read_wav_duration_seconds(wav_bytes)
            peak_abs = compute_wav_peak_abs(wav_bytes)
            preview_gain = compute_preview_gain(peak_abs)
            preview_path = self.captures_dir / f"{capture_path.stem}_preview.wav"
            if preview_path.is_file():
                preview_bytes = preview_path.read_bytes()
            else:
                preview_bytes = build_preview_wav(wav_bytes, preview_gain)
                preview_path.write_bytes(preview_bytes)
        except (OSError, wave.Error, EOFError):
            return

        latest = {
            "ok": True,
            "filename": capture_path.name,
            "bytes": len(wav_bytes),
            "duration_s": duration_s,
            "saved_path": f"captures/{capture_path.name}",
            "capture_url": f"/captures/{capture_path.name}",
            "preview_url": f"/captures/{preview_path.name}",
            "player_url": f"/captures/{preview_path.name}",
            "peak_abs": peak_abs,
            "preview_gain": preview_gain,
        }
        latest.update(make_default_asr_fields(self.asr_config))

        sidecar_path = self.sidecar_path_for_capture(capture_path)
        if sidecar_path.is_file():
            try:
                sidecar_payload = json.loads(sidecar_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                sidecar_payload = None
            if isinstance(sidecar_payload, dict):
                for key in ("asr_status", "transcript_text", "asr_error", "asr_model", "asr_language", "asr_emotion"):
                    if key in sidecar_payload:
                        latest[key] = sidecar_payload[key]

        self._latest = latest

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

        if body_size <= 0:
            self._send_json({"ok": False, "error": "Content-Length must be positive"}, status=HTTPStatus.BAD_REQUEST)
            return
        if body_size > MAX_UPLOAD_BYTES:
            self._send_json(
                {"ok": False, "error": f"Content-Length exceeds limit {MAX_UPLOAD_BYTES}"},
                status=HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
            )
            return

        wav_bytes = self.rfile.read(body_size)
        if len(wav_bytes) != body_size:
            self._send_json(
                {
                    "ok": False,
                    "error": f"incomplete upload body: expected {body_size} bytes, got {len(wav_bytes)}",
                },
                status=HTTPStatus.BAD_REQUEST,
            )
            return
        try:
            duration_s = read_wav_duration_seconds(wav_bytes)
            peak_abs = compute_wav_peak_abs(wav_bytes)
            preview_gain = compute_preview_gain(peak_abs)
            preview_bytes = build_preview_wav(wav_bytes, preview_gain)
        except (wave.Error, EOFError) as exc:
            self._send_json({"ok": False, "error": f"invalid wav: {exc}"}, status=HTTPStatus.BAD_REQUEST)
            return

        capture_path = self.server.store_capture(wav_bytes)
        preview_path = self.server.store_capture_variant(capture_path.stem + "_preview.wav", preview_bytes)
        metadata = {
            "ok": True,
            "filename": capture_path.name,
            "bytes": len(wav_bytes),
            "duration_s": duration_s,
            "saved_path": f"captures/{capture_path.name}",
            "capture_url": f"/captures/{capture_path.name}",
            "preview_url": f"/captures/{preview_path.name}",
            "player_url": f"/captures/{preview_path.name}",
            "peak_abs": peak_abs,
            "preview_gain": preview_gain,
        }
        metadata.update(make_default_asr_fields(self.server.asr_config))

        if duration_s <= 0.0:
            metadata.update(
                {
                    "asr_status": "empty_audio",
                    "transcript_text": None,
                    "asr_error": "empty audio, ASR skipped",
                    "asr_model": self.server.asr_config.get("model") if self.server.asr_config.get("enabled") else None,
                }
            )
        elif self.server.asr_config.get("enabled"):
            metadata.update(
                {
                    "asr_status": "pending",
                    "transcript_text": None,
                    "asr_error": None,
                    "asr_model": self.server.asr_config.get("model"),
                }
            )

        self.server.set_latest(metadata)
        if metadata["asr_status"] in ("disabled", "empty_audio"):
            self.server.write_asr_sidecar(capture_path, metadata)
        elif metadata["asr_status"] == "pending":
            self.server.start_asr_job(capture_path, wav_bytes)
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


def compute_wav_peak_abs(wav_bytes: bytes) -> int:
    import io

    with wave.open(io.BytesIO(wav_bytes), "rb") as wav_file:
        sample_width = wav_file.getsampwidth()
        if sample_width != 2:
            return 0
        frames = wav_file.readframes(wav_file.getnframes())
    if not frames:
        return 0
    samples = struct.unpack("<%dh" % (len(frames) // 2), frames)
    return max(abs(sample) for sample in samples)


def compute_preview_gain(peak_abs: int) -> float:
    if peak_abs <= 0:
        return 1.0
    gain = PREVIEW_TARGET_PEAK / float(peak_abs)
    if gain < 1.0:
        return 1.0
    if gain > PREVIEW_MAX_GAIN:
        return PREVIEW_MAX_GAIN
    return gain


def build_preview_wav(wav_bytes: bytes, gain: float) -> bytes:
    import io

    with wave.open(io.BytesIO(wav_bytes), "rb") as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        sample_rate = wav_file.getframerate()
        frames = wav_file.readframes(wav_file.getnframes())

    if sample_width != 2 or not frames or gain == 1.0:
        return wav_bytes

    samples = struct.unpack("<%dh" % (len(frames) // 2), frames)
    boosted = []
    for sample in samples:
        value = int(round(sample * gain))
        if value > 32767:
            value = 32767
        elif value < -32768:
            value = -32768
        boosted.append(value)

    preview_buffer = io.BytesIO()
    with wave.open(preview_buffer, "wb") as preview_wav:
        preview_wav.setnchannels(channels)
        preview_wav.setsampwidth(sample_width)
        preview_wav.setframerate(sample_rate)
        preview_wav.writeframes(struct.pack("<%dh" % len(boosted), *boosted))
    return preview_buffer.getvalue()


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
