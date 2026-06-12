"""Fail if firmware binary exceeds a flash slot size limit."""
import argparse
import sys
from pathlib import Path

DEFAULT_MAX = 116 * 1024


def main() -> int:
    parser = argparse.ArgumentParser(description="Check firmware binary size")
    parser.add_argument("image", type=Path, help="path to .bin file")
    parser.add_argument(
        "--max",
        type=int,
        default=DEFAULT_MAX,
        help=f"max size in bytes (default {DEFAULT_MAX})",
    )
    args = parser.parse_args()

    path = args.image
    if not path.is_file():
        print(f"error: file not found: {path}", file=sys.stderr)
        return 1

    size = path.stat().st_size
    if size > args.max:
        print(
            f"error: {path.name} is {size} bytes, limit is {args.max} bytes",
            file=sys.stderr,
        )
        return 1

    print(f"ok: {path.name} = {size} bytes (limit {args.max})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
