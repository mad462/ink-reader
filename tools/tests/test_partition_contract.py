import csv
from pathlib import Path


PARTITION_TABLE = Path("partitions/partitions_16mb.csv")
EXPECTED = [
    ("nvs", "data", "nvs", 0x009000, 0x006000),
    ("otadata", "data", "ota", 0x00F000, 0x002000),
    ("phy_init", "data", "phy", 0x011000, 0x001000),
    ("wifi_nvs", "data", "nvs", 0x012000, 0x00C000),
    ("nvs_keys", "data", "nvs_keys", 0x01E000, 0x001000),
    ("launcher", "app", "factory", 0x020000, 0x200000),
    ("reader", "app", "ota_0", 0x220000, 0x200000),
    ("photo", "app", "ota_1", 0x420000, 0x200000),
    ("usb_msc", "app", "ota_2", 0x620000, 0x200000),
    ("wifi_setup", "app", "ota_3", 0x820000, 0x200000),
    ("future_a", "app", "ota_4", 0xA20000, 0x200000),
    ("future_b", "app", "ota_5", 0xC20000, 0x200000),
    ("data", "data", "fat", 0xE20000, 0x1E0000),
]


def read_partitions() -> list[tuple[str, str, str, int, int]]:
    rows = []
    with PARTITION_TABLE.open(encoding="utf-8", newline="") as stream:
        for row in csv.reader(line for line in stream if not line.lstrip().startswith("#")):
            if not row:
                continue
            assert len(row) == 6
            assert row[5].strip() == ""
            name, kind, subtype, offset, size = (field.strip() for field in row[:5])
            rows.append((name, kind, subtype, int(offset, 0), int(size, 0)))
    return rows


def test_partition_fields_match_fixed_16mb_layout() -> None:
    assert read_partitions() == EXPECTED


def test_partition_layout_is_contiguous_aligned_and_fills_flash() -> None:
    rows = read_partitions()
    gaps = []
    for previous, current in zip(rows, rows[1:]):
        previous_end = previous[3] + previous[4]
        assert previous_end <= current[3]
        if previous_end != current[3]:
            gaps.append((previous[0], current[0], previous_end, current[3]))
    assert gaps == [("nvs_keys", "launcher", 0x01F000, 0x020000)]

    apps = [row for row in rows if row[1] == "app"]
    assert all(offset % 0x10000 == 0 for _, _, _, offset, _ in apps)
    assert all(size == 0x200000 for _, _, _, _, size in apps)
    assert rows[-1][3] + rows[-1][4] == 0x1000000
