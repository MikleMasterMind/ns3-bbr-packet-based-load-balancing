#!/usr/bin/env python3
"""
Графики Throughput (и CWND, RTT) с одинаковым масштабом Y для разных Nbad при фиксированных loss и N=4.
Масштаб вычисляется по 99-му процентилю значений из LTCP и NCE (без base).
Обход строго по заданной структуре папок:
  result/data/{wihtout_loss,loss-0.001,loss-0.005}/seeed-{1..5}/N-4/Nbad-{1,2,3}/{base,ltcp,nce}/
"""

import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
from collections import defaultdict

# ------------------- НАСТРОЙКИ -------------------
DATA_DIR = Path("result/data")
GRAPH_DIR = Path("result/graph")
METRICS = ["throughput"]          # можно добавить "cwnd", "rtt"
MSS = 1440                       # для пересчёта cwnd в сегменты
LOSS_LEVELS = ["wihtout_loss", "loss-0.001", "loss-0.005"]
SEEDS = [1, 2, 3, 4, 5]
N = 4
NBAD_VALUES = [1, 2, 3]
METHODS = ["base", "ltcp", "nce"]
# Метки для легенды
METHOD_LABELS = {
    "base": "CUBIC",
    "ltcp": "CUBIC-LTCP",
    "nce": "CUBIC-NCE",
}
COLORS = {
    "base": "tab:blue",
    "ltcp": "tab:orange",
    "nce": "tab:green",
}
# Методы, участвующие в расчёте масштаба (без base)
SCALE_METHODS = ["ltcp", "nce"]

# ------------------- ЧТЕНИЕ ФАЙЛА -------------------
def read_trace_file(path: Path):
    """Читает файл трассы ns-3, возвращает (time, values) или (None, None)."""
    if not path.exists():
        return None, None
    try:
        data = np.genfromtxt(path, comments="#", dtype=None, encoding="utf-8",
                             invalid_raise=False)
    except Exception:
        return None, None
    if data.size == 0:
        return None, None
    if data.ndim == 0:
        data = np.array([data])

    names = data.dtype.names
    if names is None:
        arr = np.asarray(data)
        if arr.ndim == 1:
            arr = arr.reshape(1, -1)
        if arr.shape[1] < 2:
            return None, None
        time = arr[:, 0].astype(float)
        values = arr[:, 1].astype(float)
    else:
        time = data["f0"].astype(float)
        values = data["f1"].astype(float)
    return time, values

# ------------------- ПРЕОБРАЗОВАНИЕ ЕДИНИЦ -------------------
def transform(metric: str, values: np.ndarray):
    if metric == "cwnd":
        return values / MSS, f"CWND (segments, MSS={MSS} bytes)"
    elif metric == "rtt":
        return values, "RTT (s)"
    elif metric == "throughput":
        return values, "Throughput (Mbps)"
    else:
        return values, metric

# ------------------- ПОСТРОЕНИЕ ГРАФИКОВ -------------------
def main():
    for loss in LOSS_LEVELS:
        for metric in METRICS:
            # ---- СБОР ДАННЫХ И ВЫЧИСЛЕНИЕ Y_MAX ----
            # Словарь: (Nbad, method) -> список массивов значений (по seed'ам)
            data_values = defaultdict(list)
            # Для расчёта y_max собираем все значения по SCALE_METHODS
            all_scale_values = []

            for Nbad in NBAD_VALUES:
                for method in METHODS:
                    for seed in SEEDS:
                        path = (DATA_DIR / loss / f"seeed-{seed}" / f"N-{N}" /
                                f"Nbad-{Nbad}" / method / f"{metric}.data")
                        time, vals = read_trace_file(path)
                        if time is None:
                            continue
                        # Фильтруем нули для cwnd/rtt
                        mask = np.isfinite(vals)
                        if metric != "throughput":
                            mask &= (vals != 0)
                        time = time[mask]
                        vals = vals[mask]
                        if len(vals) == 0:
                            continue
                        data_values[(Nbad, method)].append((time, vals))
                        # Накапливаем значения для масштаба
                        if method in SCALE_METHODS:
                            all_scale_values.extend(vals.tolist())

            if not all_scale_values:
                print(f"Нет данных для {loss}/{metric}, пропускаем")
                continue

            # Единый y_max как 99-й процентиль
            y_max = np.percentile(all_scale_values, 99)

            # ---- ПОСТРОЕНИЕ ДЛЯ КАЖДОГО NBAD ----
            for Nbad in NBAD_VALUES:
                plt.figure(figsize=(12, 8))
                y_label = ""
                for method in METHODS:
                    traces = data_values.get((Nbad, method), [])
                    if not traces:
                        continue
                    # Собираем все серии по времени для этого метода и Nbad
                    times, vals_list = zip(*traces)
                    # Находим общий диапазон времени
                    t_min = min(t.min() for t in times)
                    t_max = max(t.max() for t in times)
                    t_common = np.linspace(t_min, t_max, 500)
                    interp_matrix = []
                    for t, v in traces:
                        interp_matrix.append(np.interp(t_common, t, v))
                    interp_matrix = np.array(interp_matrix)

                    mean_curve = np.mean(interp_matrix, axis=0)
                    min_curve = np.min(interp_matrix, axis=0)
                    max_curve = np.max(interp_matrix, axis=0)

                    color = COLORS.get(method, "gray")
                    label = METHOD_LABELS.get(method, method)
                    # Преобразование единиц измерения
                    mean_curve, _ = transform(metric, mean_curve)
                    min_curve, _ = transform(metric, min_curve)
                    max_curve, _ = transform(metric, max_curve)
                    if not y_label:
                        _, y_label = transform(metric, np.array([1.0]))   # костыль для лейбла

                    plt.fill_between(t_common, min_curve, max_curve, color=color, alpha=0.2)
                    plt.plot(t_common, mean_curve, linewidth=2.0, color=color, label=label)

                plt.xlim(1.0, t_common[-1] * 1.02)
                plt.ylim(0, y_max * 1.05)
                plt.xlabel("Time (s)")
                plt.ylabel(y_label)
                plt.title(f"{metric.upper()} (loss={loss}, N={N}, Nbad={Nbad})")
                plt.legend(loc="best")
                plt.grid(True, which="major", linewidth=0.8)
                plt.grid(True, which="minor", linewidth=0.4, alpha=0.5)
                plt.tight_layout()

                loss_dir = loss.replace("wihtout_loss", "wihtout_loss")  # сохранение имени
                out_path = (GRAPH_DIR / loss_dir / f"N-{N}" / f"Nbad-{Nbad}" /
                            f"{metric}_comparison_mean_same.png")
                out_path.parent.mkdir(parents=True, exist_ok=True)
                plt.savefig(out_path, dpi=300)
                plt.close()
                print(f"Сохранён: {out_path}")

if __name__ == "__main__":
    main()