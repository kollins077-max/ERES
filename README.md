# IRES: Incremental RES for Historical-SCC Queries

This repository contains the C++ implementation and experiment drivers for IRES, an incremental index for Historical Strongly Connected Component (Historical-SCC) queries on directed temporal graphs. The implementation extends the RES codebase released with [spannedSCC](https://github.com/ForwardStar/spannedSCC).

The public artifact focuses on executable code and data-preparation utilities. Experimental datasets and manuscript files are not included.

## Methods

The executable provides the following primary experiment modes.

| Task | Mode | Description |
|---|---|---|
| Full construction | `RES` | Original start-time-oriented RES construction. |
| Full construction | `IRES` | End-time-oriented IRES construction with reverse-chronological reuse and pruning. |
| Single-edge update | `RES-Single` | Original forward RES maintenance, one edge at a time. |
| Single-edge update | `RES-interval` | Forward RES maintenance restricted by the SCC-formation influence interval. |
| Single-edge update | `IRES3-NoInterval` | Reverse incremental maintenance without influence-interval pruning. |
| Single-edge update | `IRES3` | IRES single-edge maintenance with the complete optimization set. |
| Batch update | `RES-Batch` | Original forward timestamp-batch RES maintenance. |
| Batch update | `IRES-Batch` | IRES batch maintenance with SCCID pruning and intra-SCC non-RES pruning. |

Auxiliary validation and ablation modes are also retained: `Online`, `Baseline`, `RES-Reverse`, and `IRES3-NoPrune`.

## Repository Layout

```text
.
|-- source/                 C++ implementation and headers
|-- example/                tiny synthetic graph and queries
|-- amazon_handler.cpp      Amazon CSV preprocessing utility
|-- graph-gen.py            dataset download and graph normalization
|-- graph-gen.sh            shell wrapper for graph-gen.py
|-- query-gen.py            temporal-window query generator
|-- query-gen.sh            shell wrapper for query-gen.py
|-- Makefile                Linux compilation used by run.sh
|-- IRES.vcxproj            Visual Studio Release/x64 project
|-- run.sh                  build-and-run wrapper
`-- README.md
```

The production executable is built from these modules:

- `baseline`: baseline index construction and query processing.
- `commonfunctions`: shared timing, progress, and utility functions.
- `online_search`: index-free Historical-SCC query processing.
- `optimized`: RES/IRES construction, maintenance, pruning, and indexed queries.
- `temporal_graph`: directed temporal-graph storage.
- `main`: command-line interface and experiment graph loader.

## Requirements

- A C++11 compiler. GCC, Clang, and Microsoft Visual C++ are supported.
- Python 3 for graph and query generation.
- Sufficient memory for the selected temporal graph and index.

The implementation is single-process C++ and does not require an external graph library.

## Input Format

### Temporal graph

Each line of the graph file is a directed temporal edge:

```text
source_vertex destination_vertex timestamp
```

Vertex identifiers and timestamps must be nonnegative integers. Input edges should be ordered by nondecreasing timestamp. Timestamps should be normalized to a contiguous range beginning at zero for reproducible scalability and update experiments.

### Queries

Each line of the query file is a closed time window:

```text
start_time end_time
```

The executable writes the Historical-SCC result for every query to the specified output file.

## How to Use It

The following scripts should be run on a Linux platform.

The temporal graph datasets used by the generation script are obtained from [SNAP](https://snap.stanford.edu/data/index.html) and [KONECT](http://konect.cc/).

- Run `graph-gen.sh` to generate graph data automatically. The script downloads datasets from [SNAP](https://snap.stanford.edu/data/index.html) and [KONECT](http://konect.cc/) and processes the selected dataset into `graph.txt`:

```bash
sh graph-gen.sh
```

When prompted, select a dataset and enter the fraction of that dataset to process. On the first run, the script downloads the configured datasets and then normalizes the selected graph into `graph.txt`.

- Run `query-gen.sh` to generate query data automatically. The script writes the generated queries into `query.txt`:

```bash
sh query-gen.sh
```

When prompted, enter the number of queries and the query-window length as a fraction of the graph's maximum timestamp.

- Run the following command to execute a solution:

```bash
sh run.sh $1
```

Here, `$1` can be `Online`, `Baseline`, `RES`, `IRES`, or `RES-Reverse`, corresponding to online search, the baseline index, the original RES-index, the proposed IRES-index, and the reverse RES construction, respectively. The query result is written to `output.txt`.

For example:

```bash
sh run.sh RES
sh run.sh IRES
```

### PowerShell

On Windows, first build `IRES.vcxproj` with the `Release|x64` configuration in Visual Studio. Then generate graph and query data with:

```powershell
python .\graph-gen.py
python .\query-gen.py
```

These commands use the same interactive prompts as the Linux scripts: select the graph dataset and fraction first, then enter the number of queries and the relative query-window length.

Run a solution with:

```powershell
.\FINDSCC.exe graph.txt query.txt output.txt RES
.\FINDSCC.exe graph.txt query.txt output.txt IRES
```

### Amazon data

For Amazon review data in comma-separated `source,destination,value,timestamp` form, compile and run:

```bash
g++ amazon_handler.cpp -o amazon_handler -O3
./amazon_handler amazon.csv graph.txt NUMBER_OF_EDGES
```

The utility maps string vertex identifiers to integers and aggregates Unix timestamps by month.

## Subgraph Construction

To construct an index on a prefix defined by a timestamp fraction, append `subgraph`; the program then reads the fraction from standard input:

```bash
sh run.sh RES subgraph
sh run.sh IRES subgraph
```

Then input the fraction of timestamps in the subgraph, such as `0.25`.

In PowerShell, the equivalent form is:

```powershell
"0.25" | .\FINDSCC.exe graph.txt query.txt output.txt IRES subgraph
```

## Run Update Experiments

Single-edge and batch experiments use:

```bash
sh run.sh MODE INITIAL_FRACTION UPDATE_EDGES
```

`INITIAL_FRACTION` is the fraction of input edges used for initial construction. `UPDATE_EDGES` is the number of subsequent edges to process. The reported update time excludes initial prefix construction and includes the selected maintenance procedure.

Single-edge examples with a 20% prefix and 1,000 inserted edges:

```bash
sh run.sh RES-Single 0.2 1000
sh run.sh RES-interval 0.2 1000
sh run.sh IRES3-NoInterval 0.2 1000
sh run.sh IRES3 0.2 1000
```

Batch-update examples:

```bash
sh run.sh RES-Batch 0.2 1000
sh run.sh IRES-Batch 0.2 1000
```

The shell wrapper uses `graph.txt`, `query.txt`, and `output.txt` automatically. The equivalent PowerShell commands are:

```powershell
.\FINDSCC.exe graph.txt query.txt output.txt RES-Single 0.2 1000
.\FINDSCC.exe graph.txt query.txt output.txt RES-interval 0.2 1000
.\FINDSCC.exe graph.txt query.txt output.txt IRES3-NoInterval 0.2 1000
.\FINDSCC.exe graph.txt query.txt output.txt IRES3 0.2 1000
.\FINDSCC.exe graph.txt query.txt output.txt RES-Batch 0.2 1000
.\FINDSCC.exe graph.txt query.txt output.txt IRES-Batch 0.2 1000
```

## Quick Check

The `example/` directory contains a tiny graph and four query windows:

```bash
cp example/graph.txt graph.txt
cp example/query.txt query.txt
sh run.sh IRES
```

For correctness checks, run `Online`, `RES`, and `IRES` on the same graph and query file and compare the generated SCC results.

## Reported Metrics

Construction and update modes print:

- graph size and the number of materialized edges;
- index construction or maintenance time in microseconds;
- average update time per edge for update experiments;
- index materialization time where applicable;
- index space cost in bytes; and
- total query-processing time.

Progress messages are emitted during long RES and IRES constructions so large experiments can be monitored.

