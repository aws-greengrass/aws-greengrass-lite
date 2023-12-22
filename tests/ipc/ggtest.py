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
    args = parser.parse_args()
    config = os.path.abspath(args.config_file)
    pytest_args = ["-s", "-v", "--config-file", config]
    ts = datetime.now().isoformat().replace(":", ".")
    if args.report:
        pytest_args += (
            "--json-report --json-report-indent=2 "
            f"--json-report-file=./gglite_report_{ts}.json").split()
    sys.exit(pytest.main(pytest_args))
