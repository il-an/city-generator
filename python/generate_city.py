"""
generate_city.py
==================

This script provides a Python interface to the C++ procedural city generator.
Rather than interacting with the command-line program directly, users can
import this module or run it as a script to produce city models.  A
configuration can be specified via command-line arguments or by populating
the Config dataclass defined below.  The script wraps the compiled
executable (``citygen``) and passes parameters accordingly.

Example usage from the command line:

.. code-block:: bash

    python generate_city.py --population 50000 --hospitals 2 --schools 3 \
        --transport transit --seed 42 --grid-size 100 \
        --radius-fraction 0.8 --output out_dir

After running, the directory ``out_dir`` will contain ``city.obj`` and
``city_summary.json`` representing the generated city.

Alternatively, this module can be imported and the ``generate`` function
called from Python code.
"""

import argparse
import subprocess
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Optional


@dataclass
class CityConfig:
    population: int = 100000
    hospitals: int = 1
    schools: int = 1
    transport: str = "car"
    seed: int = 0
    grid_size: int = 100
    radius_fraction: float = 0.8
    output: str = "output"


def generate(config: CityConfig) -> None:
    """Generate a city using the compiled ``citygen`` executable.

    Parameters
    ----------
    config : CityConfig
        Configuration for the city.  Fields correspond to command-line
        options accepted by ``citygen``.

    Raises
    ------
    RuntimeError
        If the city generator executable returns a non-zero exit code.
    """
    exe = Path(__file__).resolve().parent.parent / "citygen"
    if not exe.exists():
        raise FileNotFoundError(f"Executable not found: {exe}")
    args = [str(exe)]
    args.extend([
        f"--population={config.population}",
        f"--hospitals={config.hospitals}",
        f"--schools={config.schools}",
        f"--transport={config.transport}",
        f"--seed={config.seed}",
        f"--grid-size={config.grid_size}",
        f"--radius-fraction={config.radius_fraction}",
        f"--output={config.output}",
    ])
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"citygen failed with code {result.returncode}:\n{result.stderr}"
        )
    print(result.stdout)


def main(argv: Optional[list[str]] = None) -> None:
    parser = argparse.ArgumentParser(description="Generate a procedural city")
    parser.add_argument("--population", type=int, default=100000,
                        help="Number of inhabitants")
    parser.add_argument("--hospitals", type=int, default=1,
                        help="Number of hospitals to place")
    parser.add_argument("--schools", type=int, default=1,
                        help="Number of schools to place")
    parser.add_argument("--transport", type=str, default="car",
                        help="Primary transport mode (car|transit|walk)")
    parser.add_argument("--seed", type=int, default=0,
                        help="Random seed for deterministic output")
    parser.add_argument("--grid-size", type=int, default=100,
                        help="Grid dimension (square)")
    parser.add_argument("--radius-fraction", type=float, default=0.8,
                        help="Fraction of half grid forming city radius")
    parser.add_argument("--output", type=str, default="output",
                        help="Directory to write results")
    args = parser.parse_args(argv)
    cfg = CityConfig(
        population=args.population,
        hospitals=args.hospitals,
        schools=args.schools,
        transport=args.transport,
        seed=args.seed,
        grid_size=args.grid_size,
        radius_fraction=args.radius_fraction,
        output=args.output,
    )
    generate(cfg)


if __name__ == "__main__":
    main()
