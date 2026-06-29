from __future__ import annotations

import argparse
import sys
import wave
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Protocol


AUDIO_RECORD_READY_LINE = b"AUDIO_RECORD_READY"
AUDIO_RECORD_MAGIC = b"ARPROBE1"
HEADER_SIZE = 40
PAYLOAD_READ_CHUNK = 4096


class ProtocolError(RuntimeError):
    pass


class SerialLike(Protocol):
    def read(self, size: int) -> bytes: ...


@dataclass(frozen=True)
class AudioRecordHeader:
    sample_rate: int
    bits_per_sample: int
    channels: int
    pcm_data_size: int
    capture_ms: int
    sequence: int


READY_TIMEOUT_MESSAGE = "did not receive AUDIO_RECORD_READY before timeout"


def read_exact(port: SerialLike, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = port.read(size - len(chunks))
        if not chunk:
            raise ProtocolError(f"serial stream ended early: expected {size} bytes, got {len(chunks)}")
        chunks.extend(chunk)
    return bytes(chunks)


def read_exact_with_progress(
    port: SerialLike,
    size: int,
    progress_callback: Callable[[int, int], None] | None = None,
) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        to_read = min(PAYLOAD_READ_CHUNK, size - len(chunks))
        chunk = port.read(to_read)
        if not chunk:
            raise ProtocolError(f"serial stream ended early: expected {size} bytes, got {len(chunks)}")
        chunks.extend(chunk)
        if progress_callback is not None:
            progress_callback(len(chunks), size)
    return bytes(chunks)


def read_ready_line(
    port: SerialLike,
    ready_timeout_reads: int = 200,
    status_callback: Callable[[str], None] | None = None,
) -> None:
    buffer = bytearray()
    lines_seen = 0
    empty_reads = 0
    while lines_seen < ready_timeout_reads:
        chunk = port.read(1)
        if not chunk:
            empty_reads += 1
            if empty_reads >= ready_timeout_reads:
                break
            continue
        empty_reads = 0
        buffer.extend(chunk)
        if chunk == b"\n":
            line = bytes(buffer).strip()
            if line == AUDIO_RECORD_READY_LINE:
                return
            if status_callback is not None and line:
                try:
                    status_callback(line.decode("utf-8", errors="replace"))
                except Exception:
                    pass
            lines_seen += 1
            buffer.clear()
        elif len(buffer) > 256:
            buffer.clear()
    raise ProtocolError(READY_TIMEOUT_MESSAGE)


def parse_header(data: bytes) -> AudioRecordHeader:
    if len(data) != HEADER_SIZE:
        raise ProtocolError(f"invalid header length: expected {HEADER_SIZE}, got {len(data)}")
    if data[:8] != AUDIO_RECORD_MAGIC:
        raise ProtocolError(f"invalid magic: expected {AUDIO_RECORD_MAGIC!r}, got {data[:8]!r}")

    header_size = int.from_bytes(data[8:12], "little")
    header_version = int.from_bytes(data[12:16], "little")
    sample_rate = int.from_bytes(data[16:20], "little")
    bits_per_sample = int.from_bytes(data[20:22], "little")
    channels = int.from_bytes(data[22:24], "little")
    pcm_data_size = int.from_bytes(data[24:28], "little")
    capture_ms = int.from_bytes(data[28:32], "little")
    sequence = int.from_bytes(data[32:36], "little")

    if header_size != HEADER_SIZE:
        raise ProtocolError(f"invalid header_size: expected {HEADER_SIZE}, got {header_size}")
    if header_version != 1:
        raise ProtocolError(f"unsupported header_version: {header_version}")
    if sample_rate <= 0 or bits_per_sample <= 0 or channels <= 0:
        raise ProtocolError("header contains non-positive audio format fields")
    if bits_per_sample % 8 != 0:
        raise ProtocolError(f"bits_per_sample must be byte-aligned, got {bits_per_sample}")

    return AudioRecordHeader(
        sample_rate=sample_rate,
        bits_per_sample=bits_per_sample,
        channels=channels,
        pcm_data_size=pcm_data_size,
        capture_ms=capture_ms,
        sequence=sequence,
    )


def receive_packet(
    port: SerialLike,
    ready_timeout_reads: int = 200,
    status_callback: Callable[[str], None] | None = None,
    progress_callback: Callable[[int, int], None] | None = None,
) -> tuple[AudioRecordHeader, bytes]:
    read_ready_line(port, ready_timeout_reads=ready_timeout_reads, status_callback=status_callback)
    header = parse_header(read_exact(port, HEADER_SIZE))
    payload = read_exact_with_progress(port, header.pcm_data_size, progress_callback=progress_callback)
    return header, payload


def build_wav_path(output_dir: Path, timestamp: str | None = None) -> Path:
    stamp = timestamp or datetime.now().strftime("%Y-%m-%d_%H%M%S")
    return output_dir / f"capture_{stamp}.wav"


def write_wav_file(path: Path, header: AudioRecordHeader, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(header.channels)
        wav_file.setsampwidth(header.bits_per_sample // 8)
        wav_file.setframerate(header.sample_rate)
        wav_file.writeframes(payload)


def duration_seconds(header: AudioRecordHeader) -> float:
    if header.sample_rate <= 0 or header.bits_per_sample <= 0 or header.channels <= 0:
        return 0.0
    bytes_per_second = header.sample_rate * (header.bits_per_sample // 8) * header.channels
    if bytes_per_second <= 0:
        return 0.0
    return header.pcm_data_size / bytes_per_second


def should_continue_waiting(exc: ProtocolError) -> bool:
    return str(exc) == READY_TIMEOUT_MESSAGE


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Receive audio_record_probe UART packets and save WAV files.")
    parser.add_argument("--port", required=True, help="Serial port such as COM9")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate, default: 115200")
    parser.add_argument("--output-dir", default="captures", help="Directory for saved WAV files")
    parser.add_argument("--timeout", type=float, default=5.0, help="Per-read timeout in seconds")
    parser.add_argument(
        "--once",
        action="store_true",
        help="Receive one packet and exit. Without this flag the script keeps listening.",
    )
    return parser.parse_args()


def open_serial(port: str, baud: int, timeout: float):
    try:
        import serial
    except ModuleNotFoundError as exc:
        raise RuntimeError("pyserial is required. Install it with: pip install pyserial") from exc

    try:
        return serial.Serial(port=port, baudrate=baud, timeout=timeout)
    except serial.SerialException as exc:
        raise RuntimeError(f"failed to open serial port {port}: {exc}") from exc


def print_capture_summary(path: Path, header: AudioRecordHeader) -> None:
    print(f"saved: {path}")
    print(f"duration_s: {duration_seconds(header):.3f}")
    print(f"sample_rate: {header.sample_rate}")
    print(f"channels: {header.channels}")
    print(f"data_bytes: {header.pcm_data_size}")


def print_status_line(line: str) -> None:
    if line.startswith("AUDIO_RECORD_EVENT "):
        print(f"device: {line.removeprefix('AUDIO_RECORD_EVENT ')}")
        return
    if line.startswith("AUDIO_RECORD_STATUS "):
        print(f"device: {line.removeprefix('AUDIO_RECORD_STATUS ')}")
        return
    print(f"device-log: {line}")


def print_payload_progress(received: int, total: int) -> None:
    if total <= 0:
        return
    percent = (received * 100) // total
    print(f"receiving-audio: {received}/{total} bytes ({percent}%)")


def run() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir).resolve()

    try:
        serial_port = open_serial(args.port, args.baud, args.timeout)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    with serial_port:
        if hasattr(serial_port, "reset_input_buffer"):
            try:
                serial_port.reset_input_buffer()
            except Exception:
                pass
        print(f"listening on {args.port} @ {args.baud} baud")
        print("waiting for AUDIO_RECORD_READY...")
        while True:
            try:
                header, payload = receive_packet(
                    serial_port,
                    status_callback=print_status_line,
                    progress_callback=print_payload_progress,
                )
                print(
                    f"packet received: seq={header.sequence} bytes={header.pcm_data_size} "
                    f"rate={header.sample_rate}Hz"
                )
                path = build_wav_path(output_dir)
                write_wav_file(path, header, payload)
                print_capture_summary(path, header)
            except ProtocolError as exc:
                if should_continue_waiting(exc):
                    print("still waiting for AUDIO_RECORD_READY...")
                    continue
                print(f"error: {exc}", file=sys.stderr)
                return 1
            except OSError as exc:
                print(f"error: failed to write wav: {exc}", file=sys.stderr)
                return 1
            except Exception as exc:
                print(f"error: serial receive failed: {exc}", file=sys.stderr)
                return 1

            if args.once:
                return 0


if __name__ == "__main__":
    raise SystemExit(run())
