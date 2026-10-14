import pathlib
import subprocess
import pytest


@pytest.fixture(scope="session")
def binary():
    candidate = pathlib.Path(__file__).resolve().parents[1] / "build" / "tensor_transport"
    if not candidate.exists():
        pytest.fail("Build before testing: cmake -S . -B build && cmake --build build")
    return str(candidate)


@pytest.fixture(scope="session")
def model_binary():
    candidate = pathlib.Path(__file__).resolve().parents[1] / "build" / "collective_model"
    if not candidate.exists():
        pytest.fail("Missing collective_model binary")
    return str(candidate)
