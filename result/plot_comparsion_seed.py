#!/usr/bin/env python3
"""
Сравнительные графики для разных seed'ов при одинаковых N, loss, Nbad, metric.
Для каждого метода (base, ltcp, nce) строятся линии всех seed'ов (полупрозрачные) 
и осреднённая линия (жирная). Масштаб оси Y общий для графика.
"""

from collections import defaultdict
from pathlib import Path
from typing import Optional, Dict, List, Tuple
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import AutoMinorLocator, MaxNLocator

DATA_DIR = Path("result/data")
GRAPH_DIR = Path("result/graph")
SUPPORTED_METRICS = {"cwnd", "rtt", "throughput"}
MSS = 1440
METHODS = ["base", "ltcp", "nce"]
COLORS = {'base': 'tab:blue', 'ltcp': 'tab:orange', 'nce': 'tab:green'}

# ---------- чтение файлов ----------
def read_trace_file(path: Path):
    """Читает файл трассировки ns-3, возвращает (time, values, contexts, header) или None."""
    header = None
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                header = line.strip("#").strip()
                continue
            if line.strip():
                break

    data = np.genfromtxt(path, comments="#", dtype=None, encoding="utf-8", invalid_raise=False)
    if data.size == 0:
        return None
    if data.ndim == 0:
        data = np.array([data])

    names = data.dtype.names
    if names is None:
        arr = np.asarray(data)
        if arr.ndim == 1:
            arr = arr.reshape(1, -1)
        if arr.shape[1] < 2:
            return None
        time = arr[:, 0].astype(float)
        values = arr[:, 1].astype(float)
        contexts = arr[:, 2].astype(str) if arr.shape[1] >= 3 else None
        return time, values, contexts, header

    time = data["f0"].astype(float)
    values = data["f1"].astype(float)
    contexts = data["f2"].astype(str) if len(names) >= 3 else None
    return time, values, contexts, header

def transform_values(metric: str, values: np.ndarray) -> Tuple[np.ndarray, str]:
    """Преобразование единиц измерения и подпись оси Y."""
    if metric == "cwnd":
        return values / MSS, f"CWND (segments, MSS={MSS} bytes)"
    elif metric == "rtt":
        return values, "RTT (s)"
    elif metric == "throughput":
        return values, "Throughput (Mbps)"
    else:
        return values, metric

# ---------- разбор параметров из пути ----------
def parse_experiment_params(data_file: Path) -> Optional[Dict[str, str]]:
    """
    Извлекает параметры эксперимента из относительного пути.
    Ожидаемая структура: .../loss-<value>/seeed-<seed>/N-<N>/Nbad-<Nbad>/<method>/<metric>.data
    """
    rel = data_file.relative_to(DATA_DIR)
    parts = rel.parts
    params = {}
    # loss
    for part in parts:
        if part.startswith("loss-"):
            try:
                params['loss'] = float(part.split("-", 1)[1])
            except ValueError:
                return None
            break
        elif part == "wihtout_loss":   # сохранена опечатка из оригинального кода
            params['loss'] = 0.0
            break
    else:
        return None

    # seed (seeed-...)
    for part in parts:
        if part.startswith("seeed-"):
            try:
                params['seed'] = int(part.split("-", 1)[1])
            except ValueError:
                return None
            break
    else:
        return None

    # N (N-...)
    for part in parts:
        if part.startswith("N-"):
            try:
                params['N'] = int(part.split("-", 1)[1])
            except ValueError:
                return None
            break
    else:
        return None

    # Nbad (Nbad-...)
    for part in parts:
        if part.startswith("Nbad-"):
            try:
                params['Nbad'] = int(part.split("-", 1)[1])
            except ValueError:
                return None
            break
    else:
        return None

    # method (последняя папка перед именем файла)
    if len(parts) >= 2:
        method_dir = parts[-2]
        if method_dir in METHODS:
            params['method'] = method_dir
        else:
            return None
    else:
        return None

    # metric из имени файла без расширения
    metric = data_file.stem
    if metric in SUPPORTED_METRICS:
        params['metric'] = metric
    else:
        return None

    return params

# ---------- группировка (ключ без seed) ----------
def group_files(files: List[Path]) -> Dict[Tuple, Dict[str, Dict[int, Path]]]:
    """
    Группирует файлы по ключу (loss, N, Nbad, metric).
    Возвращает словарь: ключ -> {method -> {seed -> path}}.
    """
    groups = defaultdict(lambda: defaultdict(dict))   # ключ -> method -> seed -> path
    for f in files:
        params = parse_experiment_params(f)
        if not params:
            print(f"Пропуск (не удалось разобрать параметры): {f}")
            continue
        method = params['method']
        metric = params['metric']
        loss = params['loss']
        N = params['N']
        Nbad = params['Nbad']
        seed = params['seed']
        key = (loss, N, Nbad, metric)
        groups[key][method][seed] = f

    # удаляем пустые группы
    result = {}
    for key, method_dict in groups.items():
        if method_dict:
            result[key] = dict(method_dict)
    return result

# ---------- построение одного сравнительного графика ----------
def plot_experiment_group(key: Tuple, method_seed_paths: Dict[str, Dict[int, Path]]):
    loss, N, Nbad, metric = key
    available_methods = [m for m in METHODS if m in method_seed_paths]
    if not available_methods:
        return

    seeds = set()
    for m in available_methods:
        seeds.update(method_seed_paths[m].keys())
    seeds = sorted(seeds)
    seed_str = f"seeds {min(seeds)}-{max(seeds)}" if len(seeds) > 1 else f"seed {seeds[0]}"

    print(f"Строю группу: loss={loss}, N={N}, Nbad={Nbad}, metric={metric}, methods={available_methods}, {seed_str}")

    # Собираем данные
    fig, ax = plt.subplots(figsize=(12, 8))
    y_max_global = 0
    t_max_global = 0
    y_label = ""

    for method in available_methods:
        all_t = []
        all_v = []
        seed_data = method_seed_paths[method]
        for seed, path in seed_data.items():
            parsed = read_trace_file(path)
            if parsed is None:
                print(f"  Файл {path} не прочитан, пропускаем seed {seed}")
                continue
            time, values, _, _ = parsed
            mask = np.isfinite(time) & np.isfinite(values)
            if metric != "throughput":
                mask &= values != 0
            time = time[mask]
            values = values[mask]
            if len(time) == 0:
                continue
            plot_values, lbl = transform_values(metric, values)
            y_label = lbl
            all_t.append(time)
            all_v.append(plot_values)

            # рисуем отдельный seed полупрозрачно
            ax.plot(time, plot_values, linewidth=0.5, alpha=0.25, color=COLORS.get(method, 'gray'))

        if not all_t:
            continue

        # вычисляем среднюю линию: общая временная сетка
        t_common = np.linspace(1.0, max(np.max(t) for t in all_t), 500)
        interp_vals = []
        for t, v in zip(all_t, all_v):
            interp_vals.append(np.interp(t_common, t, v))
        mean_vals = np.mean(interp_vals, axis=0)
        ax.plot(t_common, mean_vals, linewidth=2.0, color=COLORS.get(method, 'gray'), label=method.upper())
        y_max_global = max(y_max_global, np.max(mean_vals))
        t_max_global = max(t_max_global, np.max(t_common))

    if y_max_global == 0:
        print("  Нет данных для построения группы")
        plt.close(fig)
        return

    ax.set_xlim(1.0, t_max_global * 1.02)
    ax.set_ylim(0, y_max_global * 1.05)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(y_label)
    title = (f"{metric.upper()} (loss={loss}, N={N}, Nbad={Nbad}, {seed_str})")
    ax.set_title(title)
    ax.legend(loc="best")
    ax.grid(True, which="major", linewidth=0.8)
    ax.grid(True, which="minor", linewidth=0.4, alpha=0.5)
    ax.xaxis.set_major_locator(MaxNLocator(10))
    ax.xaxis.set_minor_locator(AutoMinorLocator(2))
    ax.yaxis.set_major_locator(MaxNLocator(16))
    ax.yaxis.set_minor_locator(AutoMinorLocator(4))

    loss_dir = "loss-{}".format(loss) if loss != 0.0 else "wihtout_loss"
    out_path = GRAPH_DIR / loss_dir / f"N-{N}" / f"Nbad-{Nbad}" / f"{metric}_seed_{min(seeds)}-{max(seeds)}.png"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout()
    plt.savefig(out_path, dpi=300)
    plt.close(fig)
    print(f"  Сохранён: {out_path}")

# ---------- главная функция ----------
def main():
    all_files = list(DATA_DIR.rglob("*.data"))
    all_files = [f for f in all_files if f.stem in SUPPORTED_METRICS]
    if not all_files:
        print("Нет .data файлов для обработки")
        return

    groups = group_files(all_files)
    if not groups:
        print("Не найдено ни одной группы")
        return

    for key, method_seed_paths in groups.items():
        plot_experiment_group(key, method_seed_paths)

if __name__ == "__main__":
    main()