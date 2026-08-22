import argparse
import random


def get_tmax(filename):
    tmax = None
    with open(filename, "r", encoding="utf-8") as graph_file:
        for line in graph_file:
            fields = line.split()
            if len(fields) < 3:
                continue
            timestamp = int(fields[2])
            tmax = timestamp if tmax is None else max(tmax, timestamp)
    if tmax is None:
        raise ValueError(f"{filename} contains no valid temporal edges")
    return tmax


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate temporal-window queries for graph.txt."
    )
    parser.add_argument("--graph", default="graph.txt", help="input graph file")
    parser.add_argument("--output", default="query.txt", help="output query file")
    parser.add_argument("--number", type=int, help="number of queries")
    parser.add_argument(
        "--length",
        type=float,
        help="query-window length as a fraction in the range (0, 1]",
    )
    parser.add_argument("--seed", type=int, default=0, help="random seed")
    return parser.parse_args()


def main():
    args = parse_args()
    number = args.number
    if number is None:
        number = int(input("Enter the number of queries to be generated: "))

    fraction = args.length
    if fraction is None:
        fraction = float(
            input("Enter the length of the query windows (0 < x <= 1): ")
        )

    if number < 0:
        raise ValueError("--number must be nonnegative")
    if not 0 < fraction <= 1:
        raise ValueError("--length must be in the range (0, 1]")

    tmax = get_tmax(args.graph)
    length = min(tmax, max(0, int(tmax * fraction)))
    random_generator = random.Random(args.seed)

    with open(args.output, "w", encoding="utf-8", newline="\n") as output_file:
        for _ in range(number):
            start = random_generator.randint(0, tmax - length)
            output_file.write(f"{start} {start + length}\n")


if __name__ == "__main__":
    main()
