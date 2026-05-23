#!/usr/bin/env python3
"""Построение графика CWND по одному файлу трасс."""

import sys
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np

MSS = 1440  # максимальный размер сегмента

def read_cwnd_file(path: Path):
    """Чтение файла cwnd.data, возвращает массивы времени и cwnd в байтах."""
    header = None
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                header = line.strip("#").strip()
                continue
            if line.strip():
                break

    data = np.genfromtxt(path, comments="#", dtype=None, encoding="utf-8",
                         invalid_raise=False)
    if data.size == 0:
        raise ValueError("Файл пуст")
    if data.ndim == 0:
        data = np.array([data])

    names = data.dtype.names
    if names is None:
        arr = np.asarray(data)
        if arr.ndim == 1:
            arr = arr.reshape(1, -1)
        if arr.shape[1] < 2:
            raise ValueError("Недостаточно колонок")
        time = arr[:, 0].astype(float)
        cwnd = arr[:, 1].astype(float)
        return time, cwnd, header

    time = data["f0"].astype(float)
    cwnd = data["f1"].astype(float)
    return time, cwnd, header

def main():
    if len(sys.argv) < 2:
        print(f"Использование: {sys.argv[0]} <путь к cwnd.data>")
        sys.exit(1)

    file_path = Path(sys.argv[1])
    if not file_path.exists():
        print(f"Файл {file_path} не найден")
        sys.exit(1)

    time, cwnd_bytes, header = read_cwnd_file(file_path)
    cwnd_seg = cwnd_bytes / MSS

    plt.figure(figsize=(12, 5))
    plt.plot(time, cwnd_seg, linewidth=1.2)
    plt.xlabel("Time (s)")
    plt.ylabel(f"CWND (segments, MSS={MSS} bytes)")
    title = f"CWND – {file_path.name}"
    if header:
        title += f"\n{header}"
    plt.title(title)
    plt.grid(True, which="both", linestyle="--", alpha=0.5)
    plt.xlim(0, None)
    plt.ylim(0, None)
    plt.tight_layout()
    plt.savefig("output.png", dpi=300)

if __name__ == "__main__":
    main()