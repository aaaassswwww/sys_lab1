# Part A

This directory contains a single benchmark program for the Lab 1 Part A UNIX I/O experiments.

## Build

```bash
make
```

## Prepare a test file

The lab handout suggests a file larger than 500MB:

```bash
dd if=/dev/urandom of=testfile bs=1M count=500
```

## Run

```bash
./parta_bench --input ./testfile --output results.csv --write-dir .
```

Quick smoke test:

```bash
dd if=/dev/urandom of=testfile_quick bs=1M count=32
./parta_bench --quick --input ./testfile_quick --output quick_results.csv --write-dir .
```

`--quick` changes only the default benchmark scale:

- uses a smaller buffer-size set: `64,256,1024,4096,16384,65536`
- uses `4096` as the default `my_fread` internal buffer
- uses `32 MiB` as the default write workload

If you also pass `--sizes`, `--myfread-buffer`, or `--write-total-bytes`, your explicit values still win.

Optional arguments:

```bash
./parta_bench \
  --input ./testfile \
  --output results.csv \
  --write-dir . \
  --sizes 1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,16777216 \
  --myfread-buffer 8192 \
  --write-total-bytes 524288000
```

Default write workload in normal mode:

- each `write()` benchmark case writes `8 MiB`

Default write workload in `--quick` mode:

- each `write()` benchmark case writes `32 MiB`

## Plot figures

Install matplotlib first if needed:

```bash
python3 -m pip install matplotlib
```

Then generate figures and a short summary:

```bash
python3 plot_results.py results.csv figures
```

This will create:

- `figures/read_throughput.png`
- `figures/read_wall_time.png`
- `figures/write_throughput.png`
- `figures/write_wall_time.png`
- `figures/getc_vs_fgetc.png`
- `figures/summary.txt`

## Report template

You can start your write-up from:

```text
report_template.md
```

## CSV columns

- `method`: `read`, `getc`, `fgetc`, `fread`, `my_fread`, `write`
- `buffer_size`: request size used for that test
- `sync_mode`: `off` or `on` for `write`, otherwise `n/a`
- `total_bytes`: total processed bytes
- `real_sec`: wall clock time
- `user_sec`: user CPU time
- `sys_sec`: system CPU time
- `mib_per_sec`: throughput based on wall clock time
- `notes`: short reminder of the path under test

## Notes

- `getc` and `fgetc` are each measured once because they are character-at-a-time interfaces.
- `my_fread` uses an internal buffer size controlled by `--myfread-buffer`, default `8192`.
- Write benchmarks create temporary files in `--write-dir` and delete them after each run.
- For more stable data, run multiple times and average the results in your report.
