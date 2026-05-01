PYTHON ?= python3
MARKDOWNLINT ?= npx --yes markdownlint-cli2@0.22.1

cpplint:
	cpplint --repository=. --recursive --filter=-whitespace/line_length,-legal/copyright,-runtime/printf,-build/include,-build/namespace,-runtime/int,-whitespace/comments,-runtime/threadsafe_fn ./src

docs-lint:
	$(MARKDOWNLINT) "docs/**/*.md" README.md

docs-fix:
	$(MARKDOWNLINT) --fix "docs/**/*.md" README.md

docs-build:
	$(PYTHON) -m mkdocs build --strict

docs-check: docs-lint docs-build

.PHONY: cpplint docs-lint docs-fix docs-build docs-check
