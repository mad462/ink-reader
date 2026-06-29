from __future__ import annotations

import http.client
import importlib
import io
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
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


def reserve_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def wait_for_http_ready(url: str, *, timeout_s: float = 10.0) -> None:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                if response.status == 200:
                    return
        except Exception as exc:  # pragma: no cover - transient polling path
            last_error = exc
            time.sleep(0.1)
    raise AssertionError(f"server did not become ready for {url}: {last_error}")


@contextmanager
def temporary_env(**updates: str | None):
    old_values = {key: os.environ.get(key) for key in updates}
    try:
        for key, value in updates.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        yield
    finally:
        for key, value in old_values.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


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

            saved_files = sorted(captures_dir.glob("capture_*.wav"))
            raw_files = [path for path in saved_files if not path.name.endswith("_preview.wav")]
            preview_files = [path for path in saved_files if path.name.endswith("_preview.wav")]
            assert len(raw_files) == 1
            assert len(preview_files) == 1
            saved_path = raw_files[0]
            assert saved_path.name == upload_payload["filename"]
            assert saved_path.read_bytes() == wav_bytes

            latest_status, latest_body, _headers = request(server, "GET", "/api/latest")
            assert latest_status == 200
            latest_payload = json.loads(latest_body)
            assert latest_payload["filename"] == saved_path.name
            assert latest_payload["duration_s"] == pytest.approx(0.1, rel=1e-6)
            assert latest_payload["saved_path"] == f"captures/{saved_path.name}"
            assert latest_payload["preview_url"] == f"/captures/{saved_path.stem}_preview.wav"
            assert latest_payload["player_url"] == latest_payload["preview_url"]
            assert latest_payload["peak_abs"] == 0
            assert latest_payload["preview_gain"] == pytest.approx(1.0, rel=1e-6)

            capture_status, capture_body, capture_headers = request(server, "GET", f"/captures/{saved_path.name}")
            assert capture_status == 200
            assert capture_headers["Content-Type"] == "audio/wav"
            assert capture_body == wav_bytes

            preview_status, preview_body, preview_headers = request(server, "GET", latest_payload["preview_url"])
            assert preview_status == 200
            assert preview_headers["Content-Type"] == "audio/wav"
            with wave.open(io.BytesIO(preview_body), "rb") as preview_wav:
                assert preview_wav.getnframes() == 800
                assert preview_wav.getframerate() == 8000


def test_root_page_contains_audio_player():
    with tempfile.TemporaryDirectory() as temp_dir:
        with running_server(Path(temp_dir)) as server:
            status, body, headers = request(server, "GET", "/")

            assert status == 200
            assert headers["Content-Type"].startswith("text/html")
            assert b'<audio id="player" controls></audio>' in body
            assert b'Preview playback uses an auto-boosted WAV' in body


def test_latest_is_empty_before_any_upload():
    with tempfile.TemporaryDirectory() as temp_dir:
        with running_server(Path(temp_dir)) as server:
            status, body, _headers = request(server, "GET", "/api/latest")

            assert status == 200
            payload = json.loads(body)
            assert payload == {
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
                "asr_status": "disabled",
                "transcript_text": None,
                "asr_error": "ASR disabled: missing DASHSCOPE_API_KEY",
                "asr_model": None,
                "asr_language": None,
                "asr_emotion": None,
            }


def test_upload_marks_asr_disabled_when_api_key_missing():
    wav_bytes = make_wav_bytes()

    with temporary_env(DASHSCOPE_API_KEY=None):
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
                payload = json.loads(body)
                assert payload["asr_status"] == "disabled"
                assert payload["transcript_text"] is None
                assert payload["asr_error"] == "ASR disabled: missing DASHSCOPE_API_KEY"
                assert payload["asr_model"] is None


def test_upload_marks_empty_audio_and_skips_asr():
    wav_bytes = make_wav_bytes(frames=0)

    with temporary_env(DASHSCOPE_API_KEY="test-key"):
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
                payload = json.loads(body)
                assert payload["asr_status"] == "empty_audio"
                assert payload["transcript_text"] is None
                assert payload["asr_error"] == "empty audio, ASR skipped"


def test_server_recovers_latest_asr_sidecar_from_disk_on_startup():
    wav_bytes = make_wav_bytes()

    with tempfile.TemporaryDirectory() as temp_dir:
        captures_dir = Path(temp_dir)
        raw_path = captures_dir / "capture_2026-06-29_001500_000001.wav"
        raw_path.write_bytes(wav_bytes)
        sidecar_path = captures_dir / "capture_2026-06-29_001500_000001.asr.json"
        sidecar_path.write_text(
            json.dumps(
                {
                    "asr_status": "done",
                    "transcript_text": "测试转写文本",
                    "asr_error": None,
                    "asr_model": "qwen3-asr-flash",
                    "asr_language": "zh",
                    "asr_emotion": "neutral",
                }
            ),
            encoding="utf-8",
        )

        with temporary_env(DASHSCOPE_API_KEY="test-key"):
            with running_server(captures_dir) as server:
                latest_status, latest_body, _headers = request(server, "GET", "/api/latest")

                assert latest_status == 200
                latest_payload = json.loads(latest_body)
                assert latest_payload["filename"] == raw_path.name
                assert latest_payload["asr_status"] == "done"
                assert latest_payload["transcript_text"] == "测试转写文本"
                assert latest_payload["asr_model"] == "qwen3-asr-flash"
                assert latest_payload["asr_language"] == "zh"
                assert latest_payload["asr_emotion"] == "neutral"


def test_upload_runs_asr_worker_and_updates_latest_on_success():
    wav_bytes = make_wav_bytes()
    server_module = load_server_module()

    def fake_asr_transcribe(_wav_bytes: bytes, config: dict[str, object]) -> dict[str, object]:
        assert config["enabled"] is True
        return {
            "transcript_text": "你好，这是识别结果",
            "asr_model": "qwen3-asr-flash",
            "asr_language": "zh",
            "asr_emotion": "neutral",
        }

    with temporary_env(DASHSCOPE_API_KEY="test-key"):
        with tempfile.TemporaryDirectory() as temp_dir:
            captures_dir = Path(temp_dir)
            with running_server(captures_dir) as server:
                original = server_module.transcribe_wav_with_asr
                server_module.transcribe_wav_with_asr = fake_asr_transcribe
                try:
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
                    payload = json.loads(body)
                    assert payload["asr_status"] == "pending"

                    deadline = time.monotonic() + 3.0
                    latest_payload = None
                    while time.monotonic() < deadline:
                        latest_status, latest_body, _headers = request(server, "GET", "/api/latest")
                        assert latest_status == 200
                        latest_payload = json.loads(latest_body)
                        if latest_payload["asr_status"] == "done":
                            break
                        time.sleep(0.05)

                    assert latest_payload is not None
                    assert latest_payload["asr_status"] == "done"
                    assert latest_payload["transcript_text"] == "你好，这是识别结果"
                    assert latest_payload["asr_model"] == "qwen3-asr-flash"
                    assert latest_payload["asr_language"] == "zh"
                    assert latest_payload["asr_emotion"] == "neutral"
                finally:
                    server_module.transcribe_wav_with_asr = original


def test_upload_runs_asr_worker_and_updates_latest_on_error():
    wav_bytes = make_wav_bytes()
    server_module = load_server_module()

    def fake_asr_transcribe(_wav_bytes: bytes, _config: dict[str, object]) -> dict[str, object]:
        raise RuntimeError("mock asr failed")

    with temporary_env(DASHSCOPE_API_KEY="test-key"):
        with tempfile.TemporaryDirectory() as temp_dir:
            captures_dir = Path(temp_dir)
            with running_server(captures_dir) as server:
                original = server_module.transcribe_wav_with_asr
                server_module.transcribe_wav_with_asr = fake_asr_transcribe
                try:
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
                    payload = json.loads(body)
                    assert payload["asr_status"] == "pending"

                    deadline = time.monotonic() + 3.0
                    latest_payload = None
                    while time.monotonic() < deadline:
                        latest_status, latest_body, _headers = request(server, "GET", "/api/latest")
                        assert latest_status == 200
                        latest_payload = json.loads(latest_body)
                        if latest_payload["asr_status"] == "error":
                            break
                        time.sleep(0.05)

                    assert latest_payload is not None
                    assert latest_payload["asr_status"] == "error"
                    assert "mock asr failed" in latest_payload["asr_error"]
                finally:
                    server_module.transcribe_wav_with_asr = original


def test_real_asr_request_helper_parses_openai_compatible_response():
    server_module = load_server_module()

    captured = {}

    class FakeResponse:
        def __init__(self, body: bytes):
            self._body = body

        def read(self) -> bytes:
            return self._body

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb):
            return False

    def fake_urlopen(request_obj, timeout=0):
        captured["url"] = request_obj.full_url
        header_map = {key.lower(): value for key, value in request_obj.header_items()}
        captured["authorization"] = header_map.get("authorization")
        captured["content_type"] = header_map.get("content-type")
        captured["timeout"] = timeout
        captured["body"] = json.loads(request_obj.data.decode("utf-8"))
        response_body = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": "今天天气不错",
                            "annotations": [
                                {
                                    "language": "zh",
                                    "emotion": "neutral",
                                }
                            ],
                        }
                    }
                ]
            }
        ).encode("utf-8")
        return FakeResponse(response_body)

    original_urlopen = server_module.urllib.request.urlopen
    server_module.urllib.request.urlopen = fake_urlopen
    try:
        result = server_module.transcribe_wav_with_asr(
            make_wav_bytes(),
            {
                "enabled": True,
                "api_key": "secret-key",
                "base_url": "https://example.com/compatible-mode/v1",
                "model": "qwen3-asr-flash",
            },
        )
    finally:
        server_module.urllib.request.urlopen = original_urlopen

    assert captured["url"] == "https://example.com/compatible-mode/v1/chat/completions"
    assert captured["authorization"] == "Bearer secret-key"
    assert captured["content_type"] == "application/json"
    assert captured["body"]["model"] == "qwen3-asr-flash"
    assert captured["body"]["stream"] is False
    assert captured["body"]["messages"][0]["content"][0]["type"] == "input_audio"
    assert captured["body"]["messages"][0]["content"][0]["input_audio"]["data"].startswith("data:audio/wav;base64,")
    assert result == {
        "transcript_text": "今天天气不错",
        "asr_model": "qwen3-asr-flash",
        "asr_language": "zh",
        "asr_emotion": "neutral",
    }


def test_server_recovers_latest_capture_from_disk_on_startup():
    sample_values = [40, -40, 20, -20] * 200
    payload = struct.pack("<%dh" % len(sample_values), *sample_values)
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(16000)
        wav_file.writeframes(payload)
    wav_bytes = buffer.getvalue()

    with tempfile.TemporaryDirectory() as temp_dir:
        captures_dir = Path(temp_dir)
        raw_path = captures_dir / "capture_2026-06-28_235500_000001.wav"
        raw_path.write_bytes(wav_bytes)

        with running_server(captures_dir) as server:
            latest_status, latest_body, _headers = request(server, "GET", "/api/latest")

            assert latest_status == 200
            latest_payload = json.loads(latest_body)
            assert latest_payload["filename"] == raw_path.name
            assert latest_payload["capture_url"] == f"/captures/{raw_path.name}"
            assert latest_payload["preview_url"] == f"/captures/{raw_path.stem}_preview.wav"
            assert latest_payload["player_url"] == latest_payload["preview_url"]
            assert latest_payload["peak_abs"] == 40
            assert latest_payload["preview_gain"] == pytest.approx(300.0, rel=1e-6)

            preview_path = captures_dir / f"{raw_path.stem}_preview.wav"
            assert preview_path.is_file()


def test_upload_rejects_missing_content_length():
    with tempfile.TemporaryDirectory() as temp_dir:
        with running_server(Path(temp_dir)) as server:
            connection = http.client.HTTPConnection(server.server_address[0], server.server_address[1], timeout=5)
            try:
                connection.putrequest("POST", "/api/upload")
                connection.putheader("Content-Type", "audio/wav")
                connection.endheaders()
                response = connection.getresponse()
                body = response.read()
                status = response.status
            finally:
                connection.close()

            assert status == 411
            payload = json.loads(body)
            assert payload["ok"] is False
            assert "Content-Length" in payload["error"]


def test_upload_rejects_invalid_content_length():
    wav_bytes = make_wav_bytes()

    with tempfile.TemporaryDirectory() as temp_dir:
        with running_server(Path(temp_dir)) as server:
            connection = http.client.HTTPConnection(server.server_address[0], server.server_address[1], timeout=5)
            try:
                connection.putrequest("POST", "/api/upload")
                connection.putheader("Content-Type", "audio/wav")
                connection.putheader("Content-Length", "not-a-number")
                connection.endheaders()
                connection.send(wav_bytes)
                response = connection.getresponse()
                body = response.read()
                status = response.status
            finally:
                connection.close()

            assert status == 400
            payload = json.loads(body)
            assert payload["ok"] is False
            assert payload["error"] == "invalid Content-Length"


def test_upload_rejects_invalid_wav():
    with tempfile.TemporaryDirectory() as temp_dir:
        captures_dir = Path(temp_dir)
        with running_server(captures_dir) as server:
            invalid_bytes = b"not-a-wav"
            status, body, _headers = request(
                server,
                "POST",
                "/api/upload",
                body=invalid_bytes,
                headers={
                    "Content-Type": "audio/wav",
                    "Content-Length": str(len(invalid_bytes)),
                },
            )

            assert status == 400
            payload = json.loads(body)
            assert payload["ok"] is False
            assert payload["error"].startswith("invalid wav:")
            assert list(captures_dir.glob("capture_*.wav")) == []


def test_missing_capture_returns_not_found():
    with tempfile.TemporaryDirectory() as temp_dir:
        with running_server(Path(temp_dir)) as server:
            status, _body, _headers = request(server, "GET", "/captures/does-not-exist.wav")

            assert status == 404


def test_capture_path_traversal_is_rejected():
    wav_bytes = make_wav_bytes()

    with tempfile.TemporaryDirectory() as temp_dir:
        captures_dir = Path(temp_dir)
        outside_path = captures_dir.parent / "escape.wav"
        outside_path.write_bytes(wav_bytes)

        with running_server(captures_dir) as server:
            status, _body, _headers = request(server, "GET", "/captures/../escape.wav")

            assert status == 404


def test_preview_boosts_low_amplitude_capture():
    sample_values = [20, -20, 10, -10] * 200
    payload = b"".join(int(v).to_bytes(2, "little", signed=True) for v in sample_values)
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(16000)
        wav_file.writeframes(payload)
    wav_bytes = buffer.getvalue()

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
            payload = json.loads(body)
            assert payload["peak_abs"] == 20
            assert payload["preview_gain"] == pytest.approx(512.0, rel=1e-6)

            preview_status, preview_body, _headers = request(server, "GET", payload["preview_url"])
            assert preview_status == 200
            with wave.open(io.BytesIO(preview_body), "rb") as preview_wav:
                preview_frames = preview_wav.readframes(preview_wav.getnframes())
            preview_values = struct.unpack("<%dh" % (len(preview_frames) // 2), preview_frames)
            assert max(abs(v) for v in preview_values) == 10240


def test_start_script_replaces_stale_listener_on_requested_port():
    tools_dir = Path(__file__).resolve().parent
    server_script = tools_dir / "audio_record_web_server.py"
    start_script = tools_dir / "start_audio_record_web_server.ps1"
    stop_script = tools_dir / "stop_audio_record_web_server.ps1"
    port = reserve_free_port()

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        stale_captures = temp_path / "stale-captures"
        fresh_captures = temp_path / "fresh-captures"
        pid_file = temp_path / "audio_record_web_server.pid"
        stdout_log = temp_path / "audio_record_web_server.stdout.log"
        stderr_log = temp_path / "audio_record_web_server.stderr.log"

        stale_process = subprocess.Popen(
            [
                sys.executable,
                str(server_script),
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--captures-dir",
                str(stale_captures),
            ],
            cwd=str(tools_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            wait_for_http_ready(f"http://127.0.0.1:{port}/api/latest")

            start_result = subprocess.run(
                [
                    "powershell",
                    "-ExecutionPolicy",
                    "Bypass",
                    "-File",
                    str(start_script),
                    "-BindHost",
                    "127.0.0.1",
                    "-Port",
                    str(port),
                    "-CapturesDir",
                    str(fresh_captures),
                    "-PidFile",
                    str(pid_file),
                    "-StdoutLogFile",
                    str(stdout_log),
                    "-StderrLogFile",
                    str(stderr_log),
                ],
                cwd=str(tools_dir),
                text=True,
                capture_output=True,
                timeout=20,
            )

            assert start_result.returncode == 0, start_result.stderr or start_result.stdout
            assert pid_file.is_file()
            replacement_pid = int(pid_file.read_text(encoding="utf-8"))
            assert replacement_pid != stale_process.pid

            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline and stale_process.poll() is None:
                time.sleep(0.1)
            assert stale_process.poll() is not None

            wait_for_http_ready(f"http://127.0.0.1:{port}/api/latest")
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/latest", timeout=5) as response:
                latest_payload = json.loads(response.read().decode("utf-8"))
            assert latest_payload["preview_url"] is None
            assert latest_payload["player_url"] is None
        finally:
            subprocess.run(
                [
                    "powershell",
                    "-ExecutionPolicy",
                    "Bypass",
                    "-File",
                    str(stop_script),
                    "-Port",
                    str(port),
                    "-PidFile",
                    str(pid_file),
                ],
                cwd=str(tools_dir),
                text=True,
                capture_output=True,
                timeout=20,
                check=False,
            )
            if stale_process.poll() is None:
                stale_process.terminate()
                try:
                    stale_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    stale_process.kill()
