# Memfault nRF9160 Relay Example

This example builds upon the [nRF9160 UDP example by Nordic
Semiconductor](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/samples/nrf9160/udp/README.html).
It shows how a server that receives UDP packets might relay them to the
[Memfault](https://memfault.com/) servers.

This example uses `v1.6.0-rc2` of the nRF SDK.

## Installing dependencies

### nRF SDK and Zephyr

Install the necessary system dependencies (this command should work as-is on Ubuntu):

```bash
sudo apt install cmake python3 python3-pip python3-venv
```

Clone the repository:

```bash
git clone https://github.com/memfault/memfault-nRF9160-relay.git
cd memfault-nRF9160-relay
```

Create a virtual environment and install the dependencies:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip3 install -r requirements.txt
```

All the steps starting from here mimic [Nordic's instructions](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_installing.html#get-the-ncs-code).

Install nRF SDK dependencies:

```bash
west init -m https://github.com/nrfconnect/sdk-nrf --mr v1.6.0-rc2
west config manifest.path firmware
west update
west zephyr-export
```

Install additional Python dependencies:

```bash
pip3 install -r zephyr/scripts/requirements.txt
pip3 install -r nrf/scripts/requirements.txt
pip3 install -r bootloader/mcuboot/scripts/requirements.txt
```

## Compiling and running

Once you've installed all the dependencies, make sure to set your Project Key and a static IP address to your UDP server in `firmware/prj.conf`:

```
CONFIG_UDP_SERVER_ADDRESS_STATIC="18.188.13.221"
CONFIG_MEMFAULT_NCS_PROJECT_KEY="owTDJvKithvuHTNj47hwmzUOsQ2qEGbG"
```

Run this in this repository's root:

```bash
west build -b=nrf9160dk_nrf9160ns firmware
```

Set up the UDP server:

```bash
python3 server
```

Finally, flash the device:

```bash
west build -t flash
```
