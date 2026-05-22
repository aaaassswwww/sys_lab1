#!/usr/bin/env python3

import csv
import math
import os
import sys
from collections import defaultdict

try:
    import matplotlib.pyplot as plt
except ImportError as exc:
    print("matplotlib is required. Install it with: pip install matplotlib", file=sys.stderr)
    raise SystemExit(1) from exc


def load_rows(path):
    rows = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            row["buffer_size"] = int(row["buffer_size"])
            row["total_bytes"] = int(row["total_bytes"])
            row["real_sec"] = float(row["real_sec"])
            row["user_sec"] = float(row["user_sec"])
            row["sys_sec"] = float(row["sys_sec"])
            row["mib_per_sec"] = float(row["mib_per_sec"])
            rows.append(row)
    return rows


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def save_line_chart(series_map, ylabel, title, output_path):
    plt.figure(figsize=(10, 6))
    for label, rows in series_map.items():
        rows = sorted(rows, key=lambda r: r["buffer_size"])
        x = [row["buffer_size"] for row in rows]
        y = [row["mib_per_sec"] for row in rows]
        plt.plot(x, y, marker="o", linewidth=1.8, markersize=4, label=label)

    plt.xscale("log", base=2)
    plt.xlabel("Buffer Size (bytes)")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, which="both", linestyle="--", alpha=0.35)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=160)
    plt.close()


def save_time_chart(series_map, field, ylabel, title, output_path):
    plt.figure(figsize=(10, 6))
    for label, rows in series_map.items():
        rows = sorted(rows, key=lambda r: r["buffer_size"])
        x = [row["buffer_size"] for row in rows]
        y = [row[field] for row in rows]
        plt.plot(x, y, marker="o", linewidth=1.8, markersize=4, label=label)

    plt.xscale("log", base=2)
    plt.xlabel("Buffer Size (bytes)")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, which="both", linestyle="--", alpha=0.35)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=160)
    plt.close()


def save_bar_chart(labels, values, ylabel, title, output_path):
    plt.figure(figsize=(8, 5))
    plt.bar(labels, values, width=0.55)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, axis="y", linestyle="--", alpha=0.35)
    plt.tight_layout()
    plt.savefig(output_path, dpi=160)
    plt.close()


def filter_rows(rows, method, sync_mode=None):
    result = [row for row in rows if row["method"] == method]
    if sync_mode is not None:
        result = [row for row in result if row["sync_mode"] == sync_mode]
    return result


def main():
    if len(sys.argv) not in (2, 3):
        print("Usage: python3 plot_results.py results.csv [output_dir]", file=sys.stderr)
        raise SystemExit(1)

    input_path = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) == 3 else "figures"
    ensure_dir(output_dir)

    rows = load_rows(input_path)
    if not rows:
        print("No data rows found in CSV.", file=sys.stderr)
        raise SystemExit(1)

    read_rows = filter_rows(rows, "read")
    fread_rows = filter_rows(rows, "fread")
    my_fread_rows = filter_rows(rows, "my_fread")
    write_off_rows = filter_rows(rows, "write", "off")
    write_on_rows = filter_rows(rows, "write", "on")
    getc_rows = filter_rows(rows, "getc")
    fgetc_rows = filter_rows(rows, "fgetc")

    read_family = {
        "read()": read_rows,
        "fread()": fread_rows,
        "my_fread()": my_fread_rows,
    }

    write_family = {
        "write() no O_SYNC": write_off_rows,
        "write() with O_SYNC": write_on_rows,
    }

    save_line_chart(
        read_family,
        "Throughput (MiB/s)",
        "Read Path Throughput vs Buffer Size",
        os.path.join(output_dir, "read_throughput.png"),
    )
    save_time_chart(
        read_family,
        "real_sec",
        "Wall Time (s)",
        "Read Path Wall Time vs Buffer Size",
        os.path.join(output_dir, "read_wall_time.png"),
    )
    save_line_chart(
        write_family,
        "Throughput (MiB/s)",
        "Write Path Throughput vs Buffer Size",
        os.path.join(output_dir, "write_throughput.png"),
    )
    save_time_chart(
        write_family,
        "real_sec",
        "Wall Time (s)",
        "Write Path Wall Time vs Buffer Size",
        os.path.join(output_dir, "write_wall_time.png"),
    )

    if getc_rows and fgetc_rows:
        labels = ["getc()", "fgetc()"]
        values = [getc_rows[0]["mib_per_sec"], fgetc_rows[0]["mib_per_sec"]]
        save_bar_chart(
            labels,
            values,
            "Throughput (MiB/s)",
            "getc() vs fgetc() Throughput",
            os.path.join(output_dir, "getc_vs_fgetc.png"),
        )

    best_rows = defaultdict(list)
    for row in rows:
        key = (row["method"], row["sync_mode"])
        best_rows[key].append(row)

    summary_path = os.path.join(output_dir, "summary.txt")
    with open(summary_path, "w", encoding="utf-8") as out:
        out.write("Best throughput by method\n")
        out.write("=========================\n")
        for key, entries in sorted(best_rows.items()):
            best = max(entries, key=lambda r: r["mib_per_sec"])
            method, sync_mode = key
            out.write(
                f"{method:8s} sync={sync_mode:>3s} "
                f"best_buffer={best['buffer_size']:>8d} "
                f"throughput={best['mib_per_sec']:.3f} MiB/s "
                f"real={best['real_sec']:.6f}s\n"
            )

    print(f"Saved figures and summary to: {output_dir}")


if __name__ == "__main__":
    main()
