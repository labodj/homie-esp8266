PYTHON ?= python3
MARKDOWNLINT ?= npx --yes markdownlint-cli2@0.22.1
PYTHON_OTA_FILES := scripts/homie_ota.py scripts/ota_updater/ota_updater.py scripts/ota_updater/test_ota_updater.py

cpplint:
	cpplint --repository=. --recursive --filter=-whitespace/line_length,-legal/copyright,-runtime/printf,-build/include,-build/namespace,-runtime/int,-whitespace/comments,-runtime/threadsafe_fn ./src

python-format:
	$(PYTHON) -m ruff format $(PYTHON_OTA_FILES)

python-format-check:
	$(PYTHON) -m ruff format --check $(PYTHON_OTA_FILES)

python-lint:
	$(PYTHON) -m ruff check $(PYTHON_OTA_FILES)

python-typecheck:
	$(PYTHON) -m mypy

python-test:
	$(PYTHON) -m pytest scripts/ota_updater/test_ota_updater.py

python-check: python-format-check python-lint python-typecheck python-test

docs-lint:
	$(MARKDOWNLINT) "docs/**/*.md" README.md

docs-fix:
	$(MARKDOWNLINT) --fix "docs/**/*.md" README.md

docs-build:
	$(PYTHON) -m mkdocs build --strict

docs-check: docs-lint docs-build

.PHONY: cpplint python-format python-format-check python-lint python-typecheck python-test python-check docs-lint docs-fix docs-build docs-check
