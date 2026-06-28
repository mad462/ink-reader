from __future__ import annotations

import http.client
import importlib
import io
import json
import sys
import tempfile
import threading
import wave
from contextlib import contextmanager
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))


def load_server_module():
    try:
        return importlib.import_module("audio_record_web_server")
    except ModuleNotFoundError as exc:
        pytest.fail(f"audio_record_web_server module missing: {exc}")


def make_wav_bytes(*, sample_rate: int = 8000, channels: int = 1, sample_width: int = 2, frames: int = 800) -> bytes:
    payload = b"\x00" * (frames * channels * sample_width)
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav_file:
        wav_file.setnchannels(channels)
        wav_file.setsampwidth(sample_width)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(payload)
    return buffer.getvalue()


@contextmanager
def running_server(captures_dir: Path):
    server_module = load_server_module()
    server = server_module.create_server("127.0.0.1", 0, captures_dir)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        thread.join(timeout=5)
        server.server_close()


def request(server, method: str, path: str, body: bytes | None = None, headers: dict[str, str] | None = None):
    connection = http.client.HTTPConnection(server.server_address[0], server.server_address[1], timeout=5)
    try:
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        return response.status, response.read(), dict(response.getheaders())
    finally:
        connection.close()


def test_upload_latest_and_capture_download_roundtrip():
    wav_bytes = make_wav_bytes()

    with tempfile.TemporaryDirectory() as temp_dir:
        captures_dir = Path(temp_dir)
        with running_server(captures_dir) as server:
            status, body, _headers = request(
                server,
                "POST",
                "/api/upload",
                body=wav_bytes,
                headers={
                    "Content-Type": "audio/wav",
                    "Content-Length": str(len(wav_bytes)),
                },
            )

            assert status == 200
            upload_payload = json.loads(body)
            assert upload_payload["ok"] is True

            saved_files = list(captures_dir.glob("capture_*.wav"))
            assert len(saved_files) == 1
            saved_path = saved_files[0]
            assert saved_path.name == upload_payload["filename"]
            assert saved_path.read_bytes() == wav_bytes

            latest_status, latest_body, _headers = request(server, "GET", "/api/latest")
            assert latest_status == 200
            latest_payload = json.loads(latest_body)
            assert latest_payload["filename"] == saved_path.name
            assert latest_payload["duration_s"] == pytest.approx(0.1, rel=1e-6)

            capture_status, capture_body, capture_headers = request(server, "GET", f"/captures/{saved_path.name}")
            assert capture_status == 200
            assert capture_headers["Content-Type"] == "audio/wav"
            assert capture_body == wav_bytes


def test_root_page_contains_audio_player():
    with tempfile.TemporaryDirectory() as temp_dir:
        with running_server(Path(temp_dir)) as server:
            status, body, headers = request(server, "GET", "/")

            assert status == 200
            assert headers["Content-Type"].startswith("text/html")
            assert b'<audio id="player" controls></audio>' in body
