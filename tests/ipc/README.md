# IPC Tests

These are integration tests for Greengrass IPC.

## Dependencies

To setup the environment

```bash
poetry install
```

## Run Tests

Before running the tests, create/update the configuration file as specified in
`config.yml`.

If virtual environment is activated

```bash
python ggtest.py --config-file /path/to/config.yml --report <optional-pytest-arguments>
```

Otherwise

```bash
poetry run python ggtest.py --config-file /path/to/config.yml --report <optional-pytest-arguments>
```
