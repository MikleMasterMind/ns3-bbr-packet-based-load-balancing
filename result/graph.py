from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from matplotlib.ticker import AutoMinorLocator, FixedLocator

import matplotlib.pyplot as plt
import numpy as np

DATA_DIR = Path("result/data")
GRAPH_DIR = Path("result/graph")
SUPPORTED_METRICS = {"cwnd", "rtt"}
MSS = 1440


@dataclass
class PlotData:
    data_file: Path
    output_file: Path
    metric_name: str
    num_paths: int
    num_bad_paths: int
    time_values: np.ndarray
    plot_values: np.ndarray
    contexts: Optional[np.ndarray]
    header: Optional[str]
    y_label: str
    y_min_positive: float
    y_max: float


def read_trace_file(path: Path):
    header = None
    with path.open("r", encoding="utf-8") as file:
        for line in file:
            if line.startswith("#"):
                header = line.strip("#").strip()
                continue
            if line.strip():
                break

    data = np.genfromtxt(
        path,
        comments="#",
        dtype=None,
        encoding="utf-8",
        invalid_raise=False,
    )

    if data.size == 0:
        return None

    if data.ndim == 0:
        data = np.array([data])

    names = data.dtype.names
    if names is None:
        array = np.asarray(data)
        if array.ndim == 1:
            array = array.reshape(1, -1)
        if array.shape[1] < 2:
            return None
        time_values = array[:, 0].astype(float)
        metric_values = array[:, 1].astype(float)
        contexts = array[:, 2].astype(str) if array.shape[1] >= 3 else None
        return time_values, metric_values, contexts, header

    time_values = data["f0"].astype(float)
    metric_values = data["f1"].astype(float)
    contexts = data["f2"].astype(str) if len(names) >= 3 else None
    return time_values, metric_values, contexts, header


def transform_values(metric_name: str, values: np.ndarray):
    if metric_name == "cwnd":
        return values / MSS, f"CWND (segments, MSS={MSS} bytes)"
    if metric_name == "rtt":
        return values, "RTT (s)"
    return values, "Throughput (Mbps)"


def build_output_path(data_file: Path) -> Path:
    relative_path = data_file.relative_to(DATA_DIR)
    return (GRAPH_DIR / relative_path).with_suffix(".png")


def extract_plot_metadata(data_file: Path) -> tuple[str, int, int]:
    relative_parts = data_file.relative_to(DATA_DIR).parts

    num_paths = 1
    num_bad_paths = 0
    for part in relative_parts:
        if part.endswith("-numPaths"):
            try:
                num_paths = int(part.split("-", maxsplit=1)[0])
            except ValueError:
                pass
        if part.endswith("-numBadPaths"):
            try:
                num_bad_paths = int(part.split("-", maxsplit=1)[0])
            except ValueError:
                pass

    return num_paths, num_bad_paths


def build_plot_title(plot_data: PlotData) -> str:
    base_title = plot_data.metric_name if not plot_data.header else f"{plot_data.metric_name} ({plot_data.header})"
    return (
        f"{base_title}\n"
        f"numPaths={plot_data.num_paths}, "
        f"numBadPaths={plot_data.num_bad_paths}"
    )


def should_render(plot_data: PlotData, metric_y_min_positive: float, metric_y_max: float) -> bool:
    if not plot_data.output_file.exists():
        return True

    output_mtime = plot_data.output_file.stat().st_mtime
    if plot_data.data_file.stat().st_mtime > output_mtime:
        return True

    return plot_data.y_max < metric_y_max or plot_data.y_min_positive > metric_y_min_positive


def prepare_plot_data(data_file: Path):
    metric_name = data_file.stem
    if metric_name not in SUPPORTED_METRICS:
        return None

    parsed = read_trace_file(data_file)
    if parsed is None:
        print(f"Skip empty/unreadable: {data_file}")
        return None

    time_values, metric_values, contexts, header = parsed

    mask = np.isfinite(time_values) & np.isfinite(metric_values)
    if metric_name != "throughput":
        mask &= metric_values != 0

    time_values = time_values[mask]
    metric_values = metric_values[mask]
    if contexts is not None:
        contexts = contexts[mask]

    if len(time_values) == 0:
        print(f"Skip without valid points: {data_file}")
        return None

    plot_values, y_label = transform_values(metric_name, metric_values)
    positive_values = plot_values[plot_values > 0]

    num_paths, num_bad_paths = extract_plot_metadata(data_file)

    return PlotData(
        data_file=data_file,
        output_file=build_output_path(data_file),
        metric_name=metric_name,
        num_paths=num_paths,
        num_bad_paths=num_bad_paths,
        time_values=time_values,
        plot_values=plot_values,
        contexts=contexts,
        header=header,
        y_label=y_label,
        y_min_positive=float(np.min(positive_values)),
        y_max=float(np.max(plot_values)),
    )


def configure_axis_ticks(axis, plot_data: PlotData):
    t_min = 1.0
    t_max = np.max(plot_data.time_values)
    if t_max <= t_min:
        t_max = t_min + 1  # защита от пустых данных
    axis.set_xlim(t_min, t_max)

    axis.xaxis.set_major_locator(plt.MaxNLocator(10))
    axis.xaxis.set_minor_locator(AutoMinorLocator(2))

    if plot_data.metric_name == "rtt":
        axis.yaxis.set_major_locator(plt.MaxNLocator(16))
        axis.yaxis.set_minor_locator(AutoMinorLocator(4))
    else:
        axis.yaxis.set_major_locator(plt.MaxNLocator(16))
        axis.yaxis.set_minor_locator(AutoMinorLocator(4))

    axis.tick_params(axis="both", which="major", labelsize=10)
    axis.tick_params(axis="both", which="minor", length=3)



def render_plot(plot_data: PlotData, metric_y_min_positive: float, metric_y_max: float):
    fig, axis = plt.subplots(figsize=(12, 8))

    if plot_data.contexts is None:
        axis.plot(plot_data.time_values, plot_data.plot_values, linewidth=1.2)
    else:
        unique_contexts = np.unique(plot_data.contexts)
        show_legend = len(unique_contexts) <= 8

        for context in unique_contexts:
            context_mask = plot_data.contexts == context
            label = context
            if len(label) > 60:
                label = "..." + label[-57:]
            axis.plot(
                plot_data.time_values[context_mask],
                plot_data.plot_values[context_mask],
                linewidth=1.2,
                label=label,
            )

        if show_legend:
            axis.legend(loc="best", fontsize=9)

    axis.set_xlabel("Time (s)")
    axis.set_ylabel(plot_data.y_label)
    axis.set_title(build_plot_title(plot_data))
    axis.set_ylim(
        bottom=0,
        top=metric_y_max * 1.05 if metric_y_max > 0 else 1.0,
    )
    configure_axis_ticks(axis, plot_data)
    axis.grid(True, which="major", linewidth=0.8)
    axis.grid(True, which="minor", linewidth=0.4, alpha=0.5)

    plot_data.output_file.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout()
    plt.savefig(plot_data.output_file, dpi=300)
    plt.close(fig)

    print(f"Saved: {plot_data.output_file}")


def iter_data_files():
    for data_file in sorted(DATA_DIR.rglob("*.data")):
        if data_file.stem in SUPPORTED_METRICS:
            yield data_file


def main():
    files = list(iter_data_files())
    if not files:
        print(f"No supported .data files found in {DATA_DIR}")
        return

    prepared_plots = [plot_data for data_file in files if (plot_data := prepare_plot_data(data_file)) is not None]
    if not prepared_plots:
        print("No valid plot data found")
        return

    metric_ranges = {
        metric_name: (
            min(plot_data.y_min_positive for plot_data in prepared_plots if plot_data.metric_name == metric_name),
            max(plot_data.y_max for plot_data in prepared_plots if plot_data.metric_name == metric_name),
        )
        for metric_name in SUPPORTED_METRICS
        if any(plot_data.metric_name == metric_name for plot_data in prepared_plots)
    }

    for plot_data in prepared_plots:
        metric_y_min_positive, metric_y_max = metric_ranges[plot_data.metric_name]
        if not should_render(plot_data, metric_y_min_positive, metric_y_max):
            print(f"Skip up-to-date: {plot_data.output_file}")
            continue
        render_plot(plot_data, metric_y_min_positive, metric_y_max)


if __name__ == "__main__":
    main()
