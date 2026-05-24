#!/usr/bin/env python3
"""
Скрипт для построения сравнительного графика Throughput для методов
CUBIC, CUBIC-LTCP, CUBIC-NCE, BBR-NCR из папки result/data/for_bbr.
Каждый метод должен находиться в подпапке с соответствующим названием:
  result/data/for_bbr/cubic/throughput.data
  result/data/for_bbr/ltcp/throughput.data
  result/data/for_bbr/nce/throughput.data
  result/data/for_bbr/bbr/throughput.data

На выходе один график с линиями для каждого доступного метода.
"""

import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# -------- Конфигурация ----------
BASE_DIR = Path("result/data/for_bbr")
# Словарь: ключ для подпапки -> путь к файлу
METHOD_PATHS = {
    'cubic': BASE_DIR / "cubic" / "throughput.data",
    'ltcp': BASE_DIR / "ltcp" / "throughput.data",
    'nce': BASE_DIR / "nce" / "throughput.data",
    'bbr': BASE_DIR / "bbr" / "throughput.data"
}
# Словарь: ключ -> отображаемое имя в легенде
LEGEND_LABELS = {
    'cubic': 'CUBIC',
    'ltcp': 'CUBIC-LTCP',
    'nce': 'CUBIC-NCE',
    'bbr': 'BBR-NCR'
}
# Цвета для графиков
COLORS = {
    'cubic': 'tab:blue',
    'ltcp': 'tab:orange',
    'nce': 'tab:green',
    'bbr': 'tab:red'
}

def read_throughput_file(path: Path):
    """Читает файл с двумя колонками: время (с) и Throughput (Mbps)."""
    if not path.exists():
        print(f"[!] Файл не найден, пропускаем: {path}")
        return None, None

    data = np.genfromtxt(path, comments="#", dtype=None, encoding="utf-8",
                         invalid_raise=False)
    if data.size == 0:
        print(f"[!] Файл пуст: {path}")
        return None, None
    if data.ndim == 0:
        data = np.array([data])

    names = data.dtype.names
    if names is None:
        arr = np.asarray(data)
        if arr.ndim == 1:
            arr = arr.reshape(1, -1)
        if arr.shape[1] < 2:
            print(f"[!] Недостаточно колонок в {path}")
            return None, None
        time = arr[:, 0].astype(float)
        thr = arr[:, 1].astype(float)
    else:
        time = data["f0"].astype(float)
        thr = data["f1"].astype(float)
    return time, thr

def main():
    plt.figure(figsize=(12, 6))

    for key, file_path in METHOD_PATHS.items():
        time, thr = read_throughput_file(file_path)
        if time is None:
            continue
        label = LEGEND_LABELS.get(key, key)   # используем красивое имя
        plt.plot(time, thr, linewidth=1.5, color=COLORS.get(key, None),
                 label=label)

    plt.xlabel("Time (s)")
    plt.ylabel("Throughput (Mbps)")
    plt.title("Throughput Comparison: CUBIC, CUBIC-LTCP, CUBIC-NCE, BBR-NCR(50Mbps, 1ms, 10ms)")
    plt.legend(loc="best")
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.tight_layout()
    plt.savefig("throughput_comparison.png", dpi=150)
    plt.show()

if __name__ == "__main__":
    main()