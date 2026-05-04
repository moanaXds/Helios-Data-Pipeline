#!/usr/bin/env python3
import csv
import os
import random
from datetime import datetime, timedelta

OUTPUT_DIR = "input"
NUM_FILES = random.randint(3, 5)
MIN_ROWS = 500
MAX_ROWS = 1000
USER_COUNT = 200
PAGE_SLUGS = [
    "/home",
    "/product",
    "/checkout",
    "/about",
    "/contact",
    "/search",
    "/cart",
    "/login",
    "/signup",
    "/blog",
    "/pricing",
    "/faq",
    "/terms",
    "/privacy",
]
START_DATE = datetime(2024, 1, 1)
END_DATE = datetime(2024, 1, 2)


def random_timestamp(start: datetime, end: datetime) -> str:
    delta_seconds = int((end - start).total_seconds())
    random_offset = random.randint(0, delta_seconds - 1)
    ts = start + timedelta(seconds=random_offset)
    return ts.strftime("%Y-%m-%d %H:%M:%S")


def generate_clickstream_rows(row_count: int, session_start: int) -> list[dict]:
    rows = []
    for row_index in range(row_count):
        user_id = f"u{random.randint(1, USER_COUNT):03d}"
        page_views = random.randint(1, 20)
        session_length_seconds = random.randint(5, 3600)
        is_bounce = 1 if page_views == 1 else 0
        session_id = f"sess_{session_start + row_index:04d}"
        rows.append(
            {
                "user_id": user_id,
                "url": random.choice(PAGE_SLUGS),
                "session_id": session_id,
                "timestamp": random_timestamp(START_DATE, END_DATE),
                "page_views": page_views,
                "session_length_seconds": session_length_seconds,
                "is_bounce": is_bounce,
            }
        )
    return rows


def main() -> None:
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    session_counter = 1
    print(f"Generating {NUM_FILES} clickstream files in '{OUTPUT_DIR}'...")

    for file_index in range(1, NUM_FILES + 1):
        row_count = random.randint(MIN_ROWS, MAX_ROWS)
        filename = os.path.join(OUTPUT_DIR, f"clickstream_batch_{file_index:02d}.csv")
        rows = generate_clickstream_rows(row_count, session_counter)
        session_counter += row_count

        with open(filename, mode="w", newline="", encoding="utf-8") as csvfile:
            writer = csv.DictWriter(
                csvfile,
                fieldnames=[
                    "user_id",
                    "url",
                    "session_id",
                    "timestamp",
                    "page_views",
                    "session_length_seconds",
                    "is_bounce",
                ],
            )
            writer.writeheader()
            writer.writerows(rows)

        print(f"Wrote {row_count} rows to {filename}")


if __name__ == "__main__":
    main()
