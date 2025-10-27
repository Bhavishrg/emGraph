#!/usr/bin/env python3
"""
Scan all `agregate_stat.log` files under the `Results` directory and generate three comparison tables:

1) mpa_graphiti vs mpa_emgraph for varying num-parties and vec-size=100000
2) e2e_graphiti vs e2e_emgraph for varying num-parties and vec-size=100000
3) e2e_graphiti vs e2e_emgraph for varying vec-size and num-parties=5

Usage:
    python3 pythonScripts/generate_compare_tables.py --results-dir /path/to/Results

The script prints ASCII tables to stdout and saves CSV files to the Results directory.
"""
import argparse
import csv
import os
import re
from collections import defaultdict


def parse_agregate_file(path):
    """Parse agregate_stat.log and return a dict of metrics.

    Expected keys parsed (if present):
      - Online time (converted to seconds by caller)
      - Online comm (bytes)
      - Preproc time, Preproc comm (optional)

    Returns: dict with keys 'online_time', 'online_comm', 'preproc_time', 'preproc_comm'
    Values are floats (or None if missing)
    """
    metrics = {'online_time': None, 'online_comm': None, 'preproc_time': None, 'preproc_comm': None}
    float_re = re.compile(r"[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?")
    try:
        with open(path, 'r') as f:
            for line in f:
                line = line.strip()
                if line.lower().startswith('online time:'):
                    m = float_re.search(line)
                    if m:
                        metrics['online_time'] = float(m.group(0))
                elif line.lower().startswith('online comm:'):
                    m = float_re.search(line)
                    if m:
                        metrics['online_comm'] = float(m.group(0))
                elif line.lower().startswith('preproc time:'):
                    m = float_re.search(line)
                    if m:
                        metrics['preproc_time'] = float(m.group(0))
                elif line.lower().startswith('preproc comm:'):
                    m = float_re.search(line)
                    if m:
                        metrics['preproc_comm'] = float(m.group(0))
    except Exception:
        # If file unreadable, return empty metrics
        pass
    return metrics


def discover_results(results_dir):
    """Walk results_dir and find agregate_stat.log files. Return mapping keyed by (benchmark, players, vec_size)."""
    mapping = {}
    for root, dirs, files in os.walk(results_dir):
        if 'agregate_stat.log' in files:
            full = os.path.join(root, 'agregate_stat.log')
            # Expect path like Results/<benchmark>/<N>_PC/<vec_size>/agregate_stat.log
            rel = os.path.relpath(full, results_dir)
            parts = rel.split(os.sep)
            # Minimal validation
            if len(parts) >= 3:
                benchmark = parts[0]
                players_dir = parts[1]
                vec_size = parts[2]
                # players_dir like '5_PC' or '10_PC'
                players_m = re.match(r'(\d+)_PC', players_dir)
                if players_m:
                    players = int(players_m.group(1))
                else:
                    # fallback: try to parse a number
                    try:
                        players = int(players_dir)
                    except Exception:
                        players = None

                # vec_size may be numeric folder name
                try:
                    vec = int(vec_size)
                except Exception:
                    vec = None

                metrics = parse_agregate_file(full)

                mapping[(benchmark, players, vec)] = metrics
    return mapping


def fmt(val, is_comm=False):
    if val is None:
        return 'N/A'
    if is_comm:
        return f"{val:.2f}"
    else:
        return f"{val:.2f}"


def print_table_grouped(title, rows):
    """Print a grouped table where rows is a list of tuples (group_key, [(label, runtime_s, comm_mb), ...])"""
    print('\n' + title)
    print('-' * len(title))
    # determine widths
    col1w = max(8, max((len(str(g)) for g, _ in rows), default=8))
    hdr = f"{'#Parties':<{col1w}} | {'Runtime (s)':>12} | {'Comm. (MB)':>10}"
    print(hdr)
    print('-' * len(hdr))
    for group, entries in rows:
        # print group header (parties) only for first
        first = True
        for label, runtime, comm in entries:
            parties_str = str(group) if first else ''
            print(f"{parties_str:<{col1w}} | {label:>12} | {runtime:>10}")
            # show comm on next line indented
            print(f"{'':<{col1w}}   Comm (MB): {comm:>8}")
            first = False
        print('')


def build_and_print_tables(mapping, results_dir):
    # Prepare helper to fetch values and convert units
    def get_metrics(bench, players, vec):
        key = (bench, players, vec)
        m = mapping.get(key)
        if not m:
            return None, None
        # online_time: always in ms -> convert to seconds
        ot = m.get('online_time')
        if ot is not None:
            ot_s = ot / 1000.0  # Always convert from ms to seconds
        else:
            ot_s = None
        oc = m.get('online_comm')
        if oc is not None:
            oc_mb = oc / (1024.0 * 1024.0)
        else:
            oc_mb = None
        return ot_s, oc_mb

    # 1) mpa_graphiti vs mpa_emgraph for vec_size=100000 across num-parties
    vec = 100000
    benchmarks_a = ('mpa_graphiti', 'mpa_emgraph')
    parties_set = set()
    for (bench, players, v) in mapping.keys():
        if bench in benchmarks_a and v == vec and players is not None:
            parties_set.add(players)
    parties = sorted(parties_set)

    rows1 = []
    for p in parties:
        entries = []
        for bench in benchmarks_a:
            ot, oc = get_metrics(bench, p, vec)
            entries.append((bench, fmt(ot), fmt(oc, is_comm=True)))
        rows1.append((p, entries))

    # 2) e2e_graphiti vs e2e_emgraph for vec_size=100000
    benchmarks_b = ('e2e_graphiti', 'e2e_emgraph')
    parties_set_b = set()
    for (bench, players, v) in mapping.keys():
        if bench in benchmarks_b and v == vec and players is not None:
            parties_set_b.add(players)
    parties_b = sorted(parties_set_b)

    rows2 = []
    for p in parties_b:
        entries = []
        for bench in benchmarks_b:
            ot, oc = get_metrics(bench, p, vec)
            entries.append((bench, fmt(ot), fmt(oc, is_comm=True)))
        rows2.append((p, entries))

    # 3) e2e_graphiti vs e2e_emgraph for num-parties=5 across vec_size
    p = 5
    vec_set = set()
    for (bench, players, v) in mapping.keys():
        if bench in benchmarks_b and players == p and v is not None:
            vec_set.add(v)
    vecs = sorted(vec_set)

    rows3 = []
    for v in vecs:
        entries = []
        for bench in benchmarks_b:
            ot, oc = get_metrics(bench, p, v)
            entries.append((bench, fmt(ot), fmt(oc, is_comm=True)))
        rows3.append((v, entries))

    # Print tables
    print_table_grouped("Table 1: Comparison of mpa_graphiti and mpa_emgraph (vec_size=100000)", rows1)
    print_table_grouped("Table 2: Comparison of e2e_graphiti and e2e_emgraph (vec_size=100000)", rows2)
    print_table_grouped("Table 3: Comparison of e2e_graphiti and e2e_emgraph (num_parties=5)", rows3)

    # Save to CSV files
    save_table_csv(os.path.join(results_dir, 'table1_mpa_comparison.csv'), 
                   'mpa_graphiti vs mpa_emgraph (vec_size=100000)', 
                   rows1, '#Parties')
    save_table_csv(os.path.join(results_dir, 'table2_e2e_comparison_parties.csv'), 
                   'e2e_graphiti vs e2e_emgraph (vec_size=100000)', 
                   rows2, '#Parties')
    save_table_csv(os.path.join(results_dir, 'table3_e2e_comparison_vecsize.csv'), 
                   'e2e_graphiti vs e2e_emgraph (num_parties=5)', 
                   rows3, 'Vec Size')


def save_table_csv(filepath, title, rows, group_col_name):
    """Save a comparison table to CSV format."""
    with open(filepath, 'w', newline='') as f:
        writer = csv.writer(f)
        # Write header
        writer.writerow([title])
        writer.writerow([group_col_name, 'Benchmark', 'Runtime (s)', 'Comm. (MB)'])
        
        # Write data rows
        for group, entries in rows:
            for bench, runtime, comm in entries:
                writer.writerow([group, bench, runtime, comm])
    
    print(f"\nSaved: {filepath}")


def main():
    p = argparse.ArgumentParser(description='Generate comparison tables from Results/*/agregate_stat.log')
    p.add_argument('--results-dir', default=os.path.join(os.path.dirname(__file__), '..', 'Results'), help='Path to Results directory')
    args = p.parse_args()

    results_dir = os.path.abspath(args.results_dir)
    if not os.path.isdir(results_dir):
        print(f"Results directory not found: {results_dir}")
        return 2

    mapping = discover_results(results_dir)

    if not mapping:
        print("No agregate_stat.log files found under Results directory.")
        return 1

    build_and_print_tables(mapping, results_dir)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
