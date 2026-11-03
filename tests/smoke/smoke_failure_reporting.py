"""Keep the original failure visible if its report cannot be saved."""

import sys


def write_failure_report(writer, path, value):
    try:
        writer(path, value)
    except OSError as error:
        print(f"could not write failure report '{path}': {error}", file=sys.stderr, flush=True)
