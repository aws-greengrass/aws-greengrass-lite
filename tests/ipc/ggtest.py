import argparse
import os
import sys
from datetime import datetime

import pytest

parser = argparse.ArgumentParser(description="Run tests")

parser.add_argument("--config-file")

parser.add_argument("--report", action="store_true")


def pytest_addoption(parser):
    parser.addoption("--config-file",
                     action="store",
                     default="config.yml",
                     help="configuration file")


if __name__ == "__main__":
    args, uargs = parser.parse_known_args()
    pytest_args = [
        "-s", "-v", "--config-file",
        os.path.abspath(args.config_file)
    ]
    if uargs:
        for arg in uargs:
            pytest_args.append(arg)
    if args.report:
        ts = datetime.now().isoformat().replace(":", ".")
        pytest_args += (
            "--json-report --json-report-indent=2 "
            f"--json-report-file=./gglite_report_{ts}.json").split()
    sys.exit(pytest.main(pytest_args))
