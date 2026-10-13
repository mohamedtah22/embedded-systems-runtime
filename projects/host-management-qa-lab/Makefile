PYTHON ?= python3
.PHONY: test demo

test:
	mkdir -p reports
	$(PYTHON) -m pytest -q tests --junitxml=reports/junit.xml

demo:
	$(PYTHON) -m pytest -v tests/test_functional.py tests/test_security.py
