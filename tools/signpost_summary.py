#!/usr/bin/env python3

import argparse
import math
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict


def _pct(values: list[int], p: float) -> float:
    if not values:
        return float("nan")
    values_sorted = sorted(values)
    if len(values_sorted) == 1:
        return float(values_sorted[0])
    idx = p * (len(values_sorted) - 1)
    lo = int(math.floor(idx))
    hi = int(math.ceil(idx))
    if lo == hi:
        return float(values_sorted[lo])
    frac = idx - lo
    return values_sorted[lo] * (1.0 - frac) + values_sorted[hi] * frac


def _resolve_fmt(elem: ET.Element, by_id: dict[str, str]) -> str | None:
    if elem is None:
        return None
    fmt = elem.attrib.get("fmt")
    if fmt:
        return fmt
    ref = elem.attrib.get("ref")
    if ref:
        return by_id.get(ref)
    text = (elem.text or "").strip()
    return text or None


def _resolve_id(elem: ET.Element) -> str | None:
    if elem is None:
        return None
    return elem.attrib.get("ref") or elem.attrib.get("id")


def ns_to_ms(ns: float) -> float:
    return ns / 1_000_000.0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=(
            "Summarize macOS os_signpost events exported via xctrace (XML). "
            "Focuses on subsystem/category filters and reports interval durations."
        )
    )
    ap.add_argument("xml", help="Path to os-signpost table XML exported from a .trace")
    ap.add_argument("--subsystem", default="engine-sim", help="Subsystem filter (default: engine-sim)")
    ap.add_argument("--category", default="perf", help="Category filter (default: perf)")
    ap.add_argument("--top", type=int, default=20, help="How many intervals to print (default: 20)")
    args = ap.parse_args()

    # id -> fmt lookup tables (only for things we care about)
    event_type_fmt: dict[str, str] = {}
    signpost_name_fmt: dict[str, str] = {}
    subsystem_fmt: dict[str, str] = {}
    category_fmt: dict[str, str] = {}
    thread_fmt: dict[str, str] = {}

    # Keyed by (thread_id, signpost_identifier)
    open_intervals: dict[tuple[str, str], tuple[int, str | None, str | None]] = {}

    durations_by_name: dict[str, list[int]] = defaultdict(list)
    unmatched_ends = 0
    matched = 0

    # Streaming parse for large XML.
    try:
        it = ET.iterparse(args.xml, events=("end",))
    except FileNotFoundError:
        print(f"error: file not found: {args.xml}", file=sys.stderr)
        return 2

    for _event, elem in it:
        tag = elem.tag

        # Capture memoized fmt strings for referenced nodes.
        if tag == "event-type":
            elem_id = elem.attrib.get("id")
            fmt = elem.attrib.get("fmt")
            if elem_id and fmt:
                event_type_fmt[elem_id] = fmt
        elif tag == "signpost-name":
            elem_id = elem.attrib.get("id")
            fmt = elem.attrib.get("fmt")
            if elem_id and fmt:
                signpost_name_fmt[elem_id] = fmt
        elif tag == "subsystem":
            elem_id = elem.attrib.get("id")
            fmt = elem.attrib.get("fmt")
            if elem_id and fmt:
                subsystem_fmt[elem_id] = fmt
        elif tag == "category":
            elem_id = elem.attrib.get("id")
            fmt = elem.attrib.get("fmt")
            if elem_id and fmt:
                category_fmt[elem_id] = fmt
        elif tag == "thread":
            elem_id = elem.attrib.get("id")
            fmt = elem.attrib.get("fmt")
            if elem_id and fmt:
                thread_fmt[elem_id] = fmt

        if tag != "row":
            elem.clear()
            continue

        # Parse one signpost row.
        time_ns: int | None = None
        time_fmt: str | None = None
        event_kind: str | None = None
        name: str | None = None
        subsystem: str | None = None
        category: str | None = None
        thread_id: str | None = None
        signpost_id: str | None = None

        for child in list(elem):
            ctag = child.tag

            if ctag in ("event-time", "sample-time"):
                t = (child.text or "").strip()
                if t:
                    try:
                        time_ns = int(t)
                    except ValueError:
                        time_ns = None
                time_fmt = child.attrib.get("fmt") or time_fmt

            elif ctag == "event-type":
                fmt = _resolve_fmt(child, event_type_fmt)
                if fmt:
                    event_kind = fmt

            elif ctag == "signpost-name":
                fmt = _resolve_fmt(child, signpost_name_fmt)
                if fmt:
                    name = fmt

            elif ctag == "subsystem":
                fmt = _resolve_fmt(child, subsystem_fmt)
                if fmt:
                    subsystem = fmt

            elif ctag == "category":
                fmt = _resolve_fmt(child, category_fmt)
                if fmt:
                    category = fmt

            elif ctag == "thread":
                thread_id = _resolve_id(child) or thread_id

            elif ctag == "os-signpost-identifier":
                # Prefer fmt (often hex) since refs can be opaque.
                signpost_id = child.attrib.get("fmt") or (child.text or "").strip() or child.attrib.get("ref")

        # Filter.
        if not (subsystem == args.subsystem and category == args.category):
            elem.clear()
            continue

        if not (time_ns is not None and event_kind and name and thread_id and signpost_id):
            elem.clear()
            continue

        ek = event_kind.lower()
        is_begin = "begin" in ek
        is_end = "end" in ek

        # Ignore non-interval events.
        if not (is_begin or is_end):
            elem.clear()
            continue

        key = (thread_id, signpost_id)

        if is_begin and not is_end:
            open_intervals[key] = (time_ns, name, time_fmt)
        elif is_end and not is_begin:
            start = open_intervals.pop(key, None)
            if start is None:
                unmatched_ends += 1
            else:
                start_ns, start_name, _start_fmt = start
                if start_name != name:
                    # Still count it, but attribute by the begin name.
                    name_to_use = start_name
                else:
                    name_to_use = name
                dt = time_ns - start_ns
                if dt >= 0:
                    durations_by_name[name_to_use].append(dt)
                    matched += 1
        # If a label ever contained both begin+end (unlikely), ignore.

        elem.clear()

    # Report
    rows = []
    for interval_name, durs in durations_by_name.items():
        total = sum(durs)
        rows.append(
            (
                total,
                interval_name,
                len(durs),
                total / len(durs) if durs else float("nan"),
                _pct(durs, 0.50),
                _pct(durs, 0.95),
                max(durs) if durs else 0,
            )
        )

    rows.sort(reverse=True, key=lambda r: r[0])

    print(f"subsystem={args.subsystem} category={args.category}")
    print(f"matched_intervals={matched} open_intervals_remaining={len(open_intervals)} unmatched_end_events={unmatched_ends}")
    print("")
    print(
        f"{'interval':32} {'count':>8} {'total_ms':>12} {'avg_ms':>10} {'p50_ms':>10} {'p95_ms':>10} {'max_ms':>10}"
    )
    print("-" * 32 + " " + "-" * 8 + " " + "-" * 12 + " " + "-" * 10 + " " + "-" * 10 + " " + "-" * 10 + " " + "-" * 10)

    for total, interval_name, count, avg, p50, p95, mx in rows[: args.top]:
        print(
            f"{interval_name[:32]:32} {count:8d} {ns_to_ms(total):12.3f} {ns_to_ms(avg):10.3f} {ns_to_ms(p50):10.3f} {ns_to_ms(p95):10.3f} {ns_to_ms(mx):10.3f}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
