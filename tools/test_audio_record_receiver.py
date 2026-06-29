from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from audio_record_receiver import (
    AUDIO_RECORD_MAGIC,
    AudioRecordHeader,
    ProtocolError,
    build_wav_path,
    parse_header,
    receive_packet,
    read_ready_line,
    should_continue_waiting,
    write_wav_file,
)


class FakeSerial:
    def __init__(self, chunks):
        self._chunks = list(chunks)

    def read(self, size):
        if not self._chunks:
            return b""
        chunk = self._chunks.pop(0)
        if len(chunk) <= size:
            return chunk
        self._chunks.insert(0, chunk[size:])
        return chunk[:size]


def make_header_bytes(
    *,
    sample_rate=16000,
    bits_per_sample=16,
    channels=1,
    pcm_data_size=320,
    capture_ms=10,
    sequence=1,
):
    return (
        AUDIO_RECORD_MAGIC
        + (40).to_bytes(4, "little")
        + (1).to_bytes(4, "little")
        + sample_rate.to_bytes(4, "little")
        + bits_per_sample.to_bytes(2, "little")
        + channels.to_bytes(2, "little")
        + pcm_data_size.to_bytes(4, "little")
        + capture_ms.to_bytes(4, "little")
        + sequence.to_bytes(4, "little")
        + (0).to_bytes(4, "little")
    )


def test_parse_header_roundtrip():
    header = parse_header(make_header_bytes())

    assert header.sample_rate == 16000
    assert header.bits_per_sample == 16
    assert header.channels == 1
    assert header.pcm_data_size == 320
    assert header.capture_ms == 10
    assert header.sequence == 1


def test_parse_header_rejects_bad_magic():
    with pytest.raises(ProtocolError):
        parse_header(b"BADMAGIC" + make_header_bytes()[8:])


def test_receive_packet_reads_full_payload():
    payload = b"\x01\x00\x02\x00" * 10
    serial_port = FakeSerial(
        [
            b"noise\r\n",
            b"AUDIO_RECORD_READY\r\n",
            make_header_bytes(pcm_data_size=len(payload), capture_ms=1),
            payload,
        ]
    )

    header, pcm = receive_packet(serial_port, ready_timeout_reads=4)

    assert header.pcm_data_size == len(payload)
    assert pcm == payload


def test_read_ready_line_reports_status_lines_before_audio_packet():
    seen_lines = []
    serial_port = FakeSerial(
        [
            b"AUDIO_RECORD_EVENT pressed\r\n",
            b"AUDIO_RECORD_STATUS recording elapsed_ms=250 remaining_ms=9750 bytes=8192\r\n",
            b"AUDIO_RECORD_READY\r\n",
        ]
    )

    read_ready_line(serial_port, ready_timeout_reads=4, status_callback=seen_lines.append)

    assert seen_lines == [
        "AUDIO_RECORD_EVENT pressed",
        "AUDIO_RECORD_STATUS recording elapsed_ms=250 remaining_ms=9750 bytes=8192",
    ]


def test_receive_packet_errors_on_short_payload():
    serial_port = FakeSerial(
        [
            b"AUDIO_RECORD_READY\r\n",
            make_header_bytes(pcm_data_size=8),
            b"\x01\x02\x03",
        ]
    )

    with pytest.raises(ProtocolError):
        receive_packet(serial_port, ready_timeout_reads=2)


def test_receive_packet_reports_payload_progress():
    progress_updates = []
    payload = b"\x01\x00\x02\x00" * 10
    serial_port = FakeSerial(
        [
            b"AUDIO_RECORD_READY\r\n",
            make_header_bytes(pcm_data_size=len(payload), capture_ms=1),
            payload[:12],
            payload[12:],
        ]
    )

    header, pcm = receive_packet(
        serial_port,
        ready_timeout_reads=2,
        progress_callback=lambda received, total: progress_updates.append((received, total)),
    )

    assert header.pcm_data_size == len(payload)
    assert pcm == payload
    assert progress_updates[-1] == (len(payload), len(payload))
    assert len(progress_updates) >= 2


def test_write_wav_file_creates_valid_header(tmp_path: Path):
    header = AudioRecordHeader(
        sample_rate=16000,
        bits_per_sample=16,
        channels=1,
        pcm_data_size=8,
        capture_ms=0,
        sequence=1,
    )
    output_path = tmp_path / "capture.wav"
    payload = b"\x00\x00\x01\x00\x02\x00\x03\x00"

    write_wav_file(output_path, header, payload)

    data = output_path.read_bytes()
    assert data[:4] == b"RIFF"
    assert data[8:12] == b"WAVE"
    assert data[12:16] == b"fmt "
    assert data[36:40] == b"data"


def test_build_wav_path_uses_timestamp_name(tmp_path: Path):
    path = build_wav_path(tmp_path, "2026-06-28_153000")
    assert path.name == "capture_2026-06-28_153000.wav"


def test_should_continue_waiting_only_for_ready_timeout():
    assert should_continue_waiting(ProtocolError("did not receive AUDIO_RECORD_READY before timeout"))
    assert not should_continue_waiting(ProtocolError("invalid magic"))
