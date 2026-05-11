# BYU-I Badge Project Instructions

This project is for developing and flashing software for the ESP32-S3-Mini-1 chip.

## Project Structure

- `projects/`: Contains individual projects in C or MicroPython.
- `external/e-badge/`: Git submodule for the BYU-I eBadge repository.
- `scripts/`: Utility scripts for flashing and updates.
- `tests/`: Unit tests for projects.

## Conventions

- **C Development:** Use ESP-IDF standards.
- **MicroPython:** Target MicroPython firmware for ESP32-S3.
- **Testing:**
    - C: Use Unity or CMock for unit testing.
    - MicroPython: Use `unittest` or a compatible library.

## Workflows

### Submodule Update
To update the `e-badge` submodule to the latest version, run:
`./scripts/update_ebadge.sh`

### Flashing
Use the `scripts/flash.sh` script for flashing the board.
