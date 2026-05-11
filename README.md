# BYUI Badge

Instructions and code for the BYUI Badge (esp32-s3-mini-1)

## Setup

Getting the ESP-IDF framework setup on Ubuntu (WSL)

1. Configure Windows to connect USB port to WSL.

Open PowerShell as administrator:

```powershell
winget install usbipd
```

Restart terminal.

Identify the device and find the BUSID (plug and unplug device to see it appear and dissapear from the list).

```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

Configure linux for serial access:

```bash
# install required libraries
sudo apt update && sudo apt install linux-tools-generic hwdata
# make sure the device appears
lsusb
# find the port (probably /dev/ttyUSB0 or /dev/ttyACM0)
# plug and unplug can help identify it here
ls /dev/tty*
```

2. Install ESP-IDF

```bash
# clone
git clone --recursive https://github.com/espressif/esp-idf.git
# build
./install.sh esp32s3
# source the environment
. ./export.sh
```

