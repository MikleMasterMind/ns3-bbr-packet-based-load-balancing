#!/usr/bin/env python3
"""
Скрипт для построения сравнительных графиков CWND, RTT и Throughput
для доступных методов (Base, LTCP, NCE) при одинаковых параметрах эксперимента.
Если для каких-то методов данные отсутствуют (например, таймаут), строятся только имеющиеся.
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
METHODS = ["base", "ltcp", "nce"]   # порядок для легенды

# ---------- чтение файлов ----------
def read_trace_file(path: Path):
    """Читает файл трассировки, возвращает (time, values, contexts, header) или None."""
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
    """Преобразование значений и метка оси Y."""
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
    Извлекает параметры эксперимента из пути файла.
    Ожидаемая структура:
    .../loss-0.001/seeed-5/N-2/Nbad-1/метод/имя_файла
    Возвращает словарь с ключами: loss, seed, N, Nbad, method, metric
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
        elif part == "wihtout_loss":   # опечатка в оригинале
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
        method_dir = parts[-2]          # предпоследний элемент — папка метода
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

# ---------- группировка (без требования полноты) ----------
def group_files(files: List[Path]) -> Dict[Tuple, Dict[str, Path]]:
    """
    Группирует файлы по ключу (loss, seed, N, Nbad, metric).
    Значение — словарь {method: path}. Может содержать от 1 до 3 методов.
    """
    groups = defaultdict(dict)   # ключ -> {method: path}
    for f in files:
        params = parse_experiment_params(f)
        if not params:
            print(f"Пропуск (не удалось разобрать параметры): {f}")
            continue
        method = params['method']
        metric = params['metric']
        loss = params['loss']
        seed = params['seed']
        N = params['N']
        Nbad = params['Nbad']
        key = (loss, seed, N, Nbad, metric)
        groups[key][method] = f

    # убираем совсем пустые группы (на всякий случай)
    return {k: v for k, v in groups.items() if len(v) > 0}

# ---------- построение одного сравнительного графика ----------
def plot_comparison(key: Tuple, method_paths: Dict[str, Path]):
    loss, seed, N, Nbad, metric = key
    available_methods = [m for m in METHODS if m in method_paths]
    if not available_methods:
        return

    print(f"Строю сравнение: loss={loss}, seed={seed}, N={N}, Nbad={Nbad}, metric={metric}, методы={available_methods}")

    # Чтение данных доступных методов
    all_data = {}
    for method in available_methods:
        path = method_paths[method]
        parsed = read_trace_file(path)
        if parsed is None:
            print(f"  Файл {path} не прочитан, пропускаем метод {method}")
            continue
        time, values, _, _ = parsed
        # фильтрация нулей/бесконечностей
        mask = np.isfinite(time) & np.isfinite(values)
        if metric != "throughput":
            mask &= values != 0
        time = time[mask]
        values = values[mask]
        if len(time) == 0:
            print(f"  Нет валидных точек в {path}")
            continue
        all_data[method] = (time, values)

    if not all_data:
        print("  Не осталось данных для построения")
        return

    # Общий диапазон Y
    all_vals = np.concatenate([data[1] for data in all_data.values()])
    y_max = np.max(all_vals) * 1.05
    t_max = max(np.max(data[0]) for data in all_data.values())
    t_min = 1.0
    if t_max <= t_min:
        t_max = t_min + 1

    # Построение
    fig, ax = plt.subplots(figsize=(12, 8))
    colors = {'base': 'tab:blue', 'ltcp': 'tab:orange', 'nce': 'tab:green'}

    for method in available_methods:
        if method not in all_data:
            continue
        time, raw_values = all_data[method]
        plot_values, y_label = transform_values(metric, raw_values)
        ax.plot(time, plot_values, linewidth=1.2, label=method.upper(), color=colors.get(method, None))

    ax.set_xlim(t_min, t_max)
    ax.set_ylim(0, y_max)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(y_label)
    title = (f"{metric.upper()} comparison\n"
             f"loss={loss}, seed={seed}, N={N}, Nbad={Nbad}")
    ax.set_title(title)
    ax.legend(loc="best")
    ax.grid(True, which="major", linewidth=0.8)
    ax.grid(True, which="minor", linewidth=0.4, alpha=0.5)
    ax.xaxis.set_major_locator(plt.MaxNLocator(10))
    ax.xaxis.set_minor_locator(AutoMinorLocator(2))
    ax.yaxis.set_major_locator(plt.MaxNLocator(16))
    ax.yaxis.set_minor_locator(AutoMinorLocator(4))

    # Путь для сохранения
    loss_dir = "loss-{}".format(loss) if loss != 0.0 else "wihtout_loss"
    out_path = GRAPH_DIR / loss_dir / f"seeed-{seed}" / f"N-{N}" / f"Nbad-{Nbad}" / f"{metric}_comparison.png"
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

    for key, method_paths in groups.items():
        plot_comparison(key, method_paths)

if __name__ == "__main__":
    main()