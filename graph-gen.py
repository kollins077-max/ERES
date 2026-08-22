import os
import time
import threading
import tarfile
import gzip
import argparse
import heapq
import tempfile
import shutil

headers = {'User-Agent':'Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6'}

def download(url, path):
    try:
        from pathlib import Path
        from tqdm import tqdm
    except:
        print("Installing dependencies...")
        from pip._internal import main
        main(['install', 'pathlib'])
        main(['install', 'tqdm'])
        from pathlib import Path
        from tqdm import tqdm
    from urllib.request import urlopen, Request
    print("Fetching from", url + "...")
    path = Path(path)
    blocksize = 1024 * 8
    blocknum = 0
    retry_times = 0
    while True:
        try:
            with urlopen(Request(url, headers=headers), timeout=3) as resp:
                total = resp.info().get("content-length", None)
                with tqdm(
                    unit="B",
                    unit_scale=True,
                    miniters=1,
                    unit_divisor=1024,
                    total=total if total is None else int(total),
                ) as t, path.open("wb") as f:
                    block = resp.read(blocksize)
                    while block:
                        f.write(block)
                        blocknum += 1
                        t.update(len(block))
                        block = resp.read(blocksize)
            break
        except KeyboardInterrupt:
            if path.is_file():
                path.unlink()
            raise
        except:
            retry_times += 1
            if retry_times >= 20:
                break
            print("Timed out, retrying...")
    if retry_times >= 20:
        if path.is_file():
            path.unlink()
        raise ConnectionError("bad internet connection, check it and retry.")

def showProcess():
    print(waiting_message, end="  ")
    while is_finished is False:
        print('\b-', end='')
        time.sleep(0.05)
        print('\b\\', end='')
        time.sleep(0.05)
        print('\b|', end='')
        time.sleep(0.05)
        print('\b/', end='')
        time.sleep(0.05)
    if is_finished is True:
        print('\bdone')
    else:
        print('\berror!')

def takeThird(triple):
    return triple[2]

# def move_data_file(source, destination):
#     if source.endswith(".txt"):
#         source = open(os.path.join('datasets', source), "r")
#     else:
#         source = open(os.path.join(os.path.join('datasets', source), "out." + source), "r")
#     lines = source.readlines()
#     destination = open(destination, "w")
#     destination.writelines(lines)

# def move_data_file(source, destination):
#     if source.endswith(".txt"):
#         source_path = os.path.join('datasets', source)
#     else:
#         source_path = os.path.join('datasets', source, "out." + source)

#     with open(source_path, "r") as src:
#         lines = src.readlines()

def move_data_file(source, destination, fraction=1.0):
    """
    将数据文件复制到目标文件，并支持抽取前 fraction 比例的数据。
    例如 fraction=0.1 表示仅使用前 10% 的边。
    """
    if source.endswith(".txt"):
        source_path = os.path.join('datasets', source)
    else:
        source_path = os.path.join('datasets', source, "out." + source)

    total_lines = 0
    with open(source_path, "r", encoding="utf-8", errors="ignore") as src:
        for _ in src:
            total_lines += 1

    if total_lines == 0:
        open(destination, "w").close()
        print("✅ Extracted 0 lines out of 0 (0.0% of dataset)")
        return

    subset_size = int(total_lines * fraction)
    subset_size = max(1, subset_size)

    written = 0
    with open(source_path, "r", encoding="utf-8", errors="ignore") as src, open(destination, "w", encoding="utf-8") as dst:
        for line in src:
            dst.write(line)
            written += 1
            if written >= subset_size:
                break

    print(f"✅ Extracted {written} lines out of {total_lines} ({fraction*100:.1f}% of dataset)")


    # # 只取前 1/10 的数据
    # subset_size = max(1, len(lines) // 10)
    # lines = lines[:subset_size]

    # with open(destination, "w") as dst:
    #     dst.writelines(lines)

    # print(f"✅ Extracted {subset_size} lines out of {len(lines)*10} (≈1/10 of dataset)")


def normalize(filename):
    chunk_size = 500000
    temp_files = []
    chunk = []

    def flush_chunk(items):
        if not items:
            return
        items.sort(key=lambda x: x[2])
        tf = tempfile.NamedTemporaryFile(mode="w", delete=False, encoding="utf-8", newline="\n")
        for u, v, t in items:
            tf.write(f"{u} {v} {t}\n")
        tf.close()
        temp_files.append(tf.name)

    with open(filename, "r", encoding="utf-8", errors="ignore") as f:
        for raw in f:
            if "%" in raw:
                continue
            parts = raw.split()
            if len(parts) < 3:
                continue
            try:
                u = parts[0]
                v = parts[1]
                t = float(parts[-1])
            except:
                continue
            chunk.append((u, v, t))
            if len(chunk) >= chunk_size:
                flush_chunk(chunk)
                chunk = []
    flush_chunk(chunk)

    if not temp_files:
        open(filename, "w").close()
        return

    fps = [open(p, "r", encoding="utf-8", errors="ignore") for p in temp_files]
    heap = []
    for idx, fp in enumerate(fps):
        line = fp.readline()
        if line:
            u, v, t = line.split()
            heapq.heappush(heap, (float(t), idx, u, v))

    with open(filename, "w", encoding="utf-8", newline="\n") as out:
        prev_t = None
        norm_t = -1
        while heap:
            t, idx, u, v = heapq.heappop(heap)
            if prev_t is None or t != prev_t:
                norm_t += 1
                prev_t = t
            out.write(f"{u} {v} {norm_t}\n")
            line = fps[idx].readline()
            if line:
                uu, vv, tt = line.split()
                heapq.heappush(heap, (float(tt), idx, uu, vv))

    for fp in fps:
        fp.close()
    for p in temp_files:
        if os.path.exists(p):
            os.remove(p)

if __name__ == "__main__":
    # parse optional CLI args for non-interactive usage
    parser = argparse.ArgumentParser(description="Prepare graph.txt from datasets with optional fraction subset")
    parser.add_argument("--dataset", type=str, default=None, help="Dataset name (e.g., CollegeMsg) or index (e.g., 4)")
    parser.add_argument("--fraction", type=float, default=None, help="Fraction of dataset to use (0 < x ≤ 1)")
    args = parser.parse_args()

    # download datasets
    DATASETS_URL = [
                    "http://konect.cc/files/download.tsv.dblp-cite.tar.bz2",
                    "http://konect.cc/files/download.tsv.flickr-growth.tar.bz2",
                    "http://konect.cc/files/download.tsv.soc-sign-bitcoinotc.tar.bz2",
                    "https://snap.stanford.edu/data/email-Eu-core-temporal.txt.gz",
                    "https://snap.stanford.edu/data/CollegeMsg.txt.gz"]
    if os.path.isdir("datasets") is False or len(os.listdir("datasets")) < len(DATASETS_URL):
        need_download = False
        if os.path.isdir("datasets") is False:
            os.mkdir("datasets")
        for url in DATASETS_URL:
            path = os.path.join("datasets", url.split('/')[-1])
            if not os.path.exists(path):
                if (path.split('.')[-1] == "bz2" and not os.path.exists(os.path.join("datasets", path.split('.')[2]))) or \
                    (path.split('.')[-1] == "gz" and not os.path.exists(path.split('.')[0] + '.' + path.split('.')[1])):
                        if not need_download:
                            need_download = True
                            print("Downloading datasets...")
                        download(url, path)

    # extract all datasets
    waiting_message = "Extracting datasets..."
    is_finished = False
    thread_extract_datasets = threading.Thread(target=showProcess)
    thread_extract_datasets.start()
    file_ls = os.listdir("datasets")
    for file in file_ls:
        if file.endswith(".tar.bz2"):
            archive = tarfile.open(os.path.join("datasets", file), "r:bz2")
            archive.extractall("datasets")
            os.remove(os.path.join("datasets", file))
        elif file.endswith(".txt.gz"):
            src_path = os.path.join("datasets", file)
            dst_path = os.path.join("datasets", file.split('.')[0] + "." + file.split('.')[1])
            with gzip.open(src_path, "rb") as archive, open(dst_path, "wb") as out:
                shutil.copyfileobj(archive, out, length=1024 * 1024)
            os.remove(os.path.join("datasets", file))
    is_finished = True
    thread_extract_datasets.join()

    # clear cache
    if os.path.isfile("model"):
        os.remove("model")

    # select a target graph dataset
    file_ls = os.listdir("datasets")
    count = 1
    print("Datasets:")
    print("0. naive")
    printable_names = []
    for file in file_ls:
        name = file.split(".")[0] if file.endswith(".txt") else file
        printable_names.append(name)
        print(str(count) + ".", name)
        count += 1

    # determine selection: CLI arg or interactive
    if args.dataset is not None:
        ds_arg = args.dataset.strip()
        if ds_arg.isdigit():
            user_input = ds_arg
        else:
            # try to find by name (case-insensitive)
            lowered = [n.lower() for n in printable_names]
            if ds_arg.lower() in lowered:
                idx = lowered.index(ds_arg.lower()) + 1  # +1 because 0 is 'naive'
                user_input = str(idx)
            else:
                print(f"Invalid dataset name '{ds_arg}'. Program terminated.")
                exit()
    else:
        user_input = input("Select a graph dataset (0-" + str(count - 1) + "): ")

    # move data file
    # if user_input.strip() in [str(i) for i in range(count)]:
    #     waiting_message = 'Copying dataset to "graph.txt"...'
    #     is_finished = False
    #     thread_move_data_file = threading.Thread(target=showProcess)
    #     thread_move_data_file.start()
    #     if int(user_input) == 0:
    #         open("graph.txt", "w").write("0 1 0\n1 2 0\n2 0 0\n2 3 0\n3 2 1\n3 4 0\n4 5 0\n5 3 0")
    #     else:
    #         move_data_file(file_ls[int(user_input) - 1], "graph.txt")
    #     is_finished = True
    #     thread_move_data_file.join()
        # move data file with optional fraction
    if user_input.strip() in [str(i) for i in range(count)]:
        # fraction can come from CLI or interactive
        if args.fraction is not None:
            fraction = args.fraction
            if not (0 < fraction <= 1):
                print("⚠️ Invalid fraction from CLI, using full dataset (1.0)")
                fraction = 1.0
        else:
            try:
                fraction = float(input("Enter fraction of dataset to use (0 < x ≤ 1, default=1.0): ").strip())
                if not (0 < fraction <= 1):
                    print("⚠️ Invalid fraction, using full dataset (1.0)")
                    fraction = 1.0
            except:
                fraction = 1.0

        waiting_message = f'Copying {fraction*100:.1f}% of dataset to "graph.txt"...'
        is_finished = False
        thread_move_data_file = threading.Thread(target=showProcess)
        thread_move_data_file.start()

        if int(user_input) == 0:
            open("graph.txt", "w").write("0 1 0\n1 2 0\n2 0 0\n2 3 0\n3 2 1\n3 4 0\n4 5 0\n5 3 0")
        else:
            move_data_file(file_ls[int(user_input) - 1], "graph.txt", fraction)

        is_finished = True
        thread_move_data_file.join()


    else:
        print("Invalid input! Program terminated.")
        exit()

    # normalize the graph
    waiting_message = "Normalizing the graph..."
    is_finished = False
    thread_normalize = threading.Thread(target=showProcess)
    thread_normalize.start()
    if int(user_input) != 0:
        normalize("graph.txt")
    is_finished = True
    thread_normalize.join()
