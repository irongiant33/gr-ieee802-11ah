Hi!

This is a IEEE 802.11ah (Wifi HaLow) transceiver for GNU Radio based on the gr-ieee802-11 repository. Interoperability was tested with the [Alfa Halow U radios](https://www.alfa.com.tw/products/halow-u?variant=39467228758088). The code can also be used in simulations.

# Table of Contents

1. [Development](#development)

2. [Installation](#installation)

3. [Usage](#usage)

4. [Troubleshooting](#troubleshooting)

5. [Further information](#further-information)

---

# Development

Like GNU Radio, this module uses *maint* branches for development.
These branches are supposed to be used with the corresponding GNU Radio
branches. This means: the *maint-3.7* branch is compatible with GNU Radio 3.7,
*maint-3.8* is compatible with GNU Radio 3.8, etc.

This repository is based on [gr-ieee802-11](https://github.com/bastibl/gr-ieee802-11), but due to the differences between 802.11ah and 802.11a/g/p in terms of FFT length, carrier allocator setup, and auto/cross correlation parameters, it did not make sense to integrate 802.11ah into `gr-ieee802-11`. Nonetheless, both repositories still work independently of one another. 

---

# Installation

## Dependencies

### GNU Radio

There are several ways to install GNU Radio. Please refer to [the GNU Radio install wiki](https://wiki.gnuradio.org/index.php/InstallingGR)

### gr-foo


Some non project specific GNU Radio blocks from Bastian Bloessl's gr-foo repo are
needed. For example the Wireshark connector. You can find these blocks at
[https://github.com/bastibl/gr-foo](https://github.com/bastibl/gr-foo). They are
installed with the typical command sequence:

```
git clone https://github.com/bastibl/gr-foo
cd gr-foo
mkdir build
cd build
cmake ..
make
sudo make install
sudo ldconfig
```

## Installation

First, clone the repository :

```
git clone https://github.com/irongiant33/gr-ieee802-11ah
cd gr-ieee802-11ah
```

Then, proceed with the installation by running the `build_and_install.sh` script :

```
./build_and_install.sh
```

Alternatively, you can perform the installation manually using the following commands :

```
mkdir build
cd build
cmake ..
make -j4
sudo make install
sudo ldconfig
cd ..
```

### Adjust Maximum Shared Memory

Since the transmitter is using the Tagged Stream blocks it has to store a
complete frame in the buffer before processing it. The default maximum shared
memory might not be enough on most Linux systems. It can be increased with

```
sudo sysctl -w kernel.shmmax=2147483648
```

### OFDM PHY

The physical layer is encapsulated in a hierarchical block to allow for a
clearer transceiver structure in GNU Radio Companion. This hierarchical block is
not included in the installation process. You have to open
```/examples/halow_phy_hier.grc``` with GNU Radio Companion and "Generate the flow graph". This
will install the block in ```~/.grc_gnuradio/```.

### Check message port connections

Sometime the connections between the message ports (the gray ones in GNU Radio
Companion) break. Therefore, please open the flow graphs and assert that
everything is connected. It should be pretty obvious how the blocks are supposed
to be wired. Actually this should not happen anymore, so if your ports are still
unconnected please raise an issue in this repository.

### Python OpenGL

If you want to run the receive demo (the one that plots the subcarrier
constellations), please assert that you have python-opengl installed. The nongl
version of the plot does not work for me.


### Run volk_profile

`volk_profile` is part of GNU Radio. It benchmarks different SIMD implementations
on your PC and creates a configuration file that stores the fastest version of
every function. This can speed up the computation considerably and is required
in order to deal with the high rate of incoming samples.

### Calibrate your daughterboard

If you have a WBX, SBX, or CBX daughterboard you should calibrate it in order to
minimize IQ imbalance and TX DC offsets. See the [application
notes](http://files.ettus.com/manual/page_calibration.html).

---

# Usage

## Simulation

The loopback flow graph should give you an idea of how simulations can be
conducted. To ease use, most blocks have debugging and logging capabilities that
can generate traces of the simulation. You can read about the logging feature
and how to use it on the [GNU Radio
Wiki](https://wiki.gnuradio.org/index.php/Logging).

## Decoding a WiFi HaLow communication

There are three recordings of a 1 MHz HaLow transmission on [IQ Engine](https://www.iqengine.org/browser) within the "802.11ah WiFi HaLow" folder. These files are:
- `1mhz-mcs0-chan43` [data](https://www.iqengine.org/api/datasources/gnuradio/iqengine/802.11ah%20WiFi%20HaLow/1mhz-mcs0-chan43.sigmf-data) and [meta](https://www.iqengine.org/api/datasources/gnuradio/iqengine/802.11ah%20WiFi%20HaLow/1mhz-mcs0-chan43.sigmf-meta)
- `1mhz-mcs1-chan43` [data](https://www.iqengine.org/api/datasources/gnuradio/iqengine/802.11ah%20WiFi%20HaLow/1mhz-mcs1-chan43.sigmf-data) and [meta](https://www.iqengine.org/api/datasources/gnuradio/iqengine/802.11ah%20WiFi%20HaLow/1mhz-mcs1-chan43.sigmf-meta)
- `1mhz-mcs10-chan43` [data](https://www.iqengine.org/api/datasources/gnuradio/iqengine/802.11ah%20WiFi%20HaLow/1mhz-mcs10-chan43.sigmf-data) and [meta](https://www.iqengine.org/api/datasources/gnuradio/iqengine/802.11ah%20WiFi%20HaLow/1mhz-mcs10-chan43.sigmf-meta)

You can download these files and change the `filepath` variable within `halow_rx.grc` to the respective download location in order to analyze the contents of the transmissions. 

## Unidirectional communication

As first over the air test I recommend to try `halow_rx.grc` and
`halow_tx.grc`. You do not need a USRP board to get these flowgraphs to work, they should be compatible
with any SDR that supports TX and/or RX. Just open the flow graphs in GNU Radio companion and execute
them. If it does not work out of the box, try to play around with the gain. If
everything works as intended you should see similar output as in the
`halow_loopback.grc` example.

---

# Troubleshooting

## Checking your installation

As a first step I recommend to test the ```halow_loopback.grc``` flow graph. This
flow graph does not need any hardware and allows you to ensure that the software
part is installed correctly. So open the flow graph and run it. If everything
works as intended you should see some decoded packets on the console.

## GNU Radio Cannot Find Blocks

If GRC complains that it can't find some blocks (other than performance counters
and hierarchical blocks) like

    >>> Error: Block key "ieee802_11ah_ofdm_mac" not found in Platform - grc(GNU Radio Companion)
    >>> Error: Block key "foo_packet_pad" not found in Platform - grc(GNU Radio Companion)

Most likely you used a different ```CMAKE_INSTALL_PREFIX``` for the module than
for GNU Radio. Therefore, the blocks of the module ended up in a different
directory and GRC can't find them. You have to tell GRC where these blocks are
by creating/adding to your ```~/.gnuradio/config.conf``` something like

    [grc]
    global_blocks_path = /opt/local/share/gnuradio/grc/blocks
    local_blocks_path = /Users/basti/usr/share/gnuradio/grc/blocks

But with the directories that match your installation.

## Miscellaneous

- Please check compile and installation logs. They might contain interesting
  information.
- Did you calibrate your daughterboard?
- Did you run `volk_profile`?
- Did you try different gain settings?
- Did you close the case of the devices?
- Did you try real-time priority?
- Did you compile GNU Radio and gr-ieee802-11 in release mode?
- If you see warnings that ```blocks_ctrlport_monitor_performance``` is missing
  that means that you installed GNU Radio without control port or performance
  counters. These blocks allow you to monitor the performance of the transceiver
  while it is running, but are not required. You can just delete them from the
  flow graph.
- The message

>    You must now use ifconfig to set its IP address. E.g.,
>    `$ sudo ifconfig tap0 192.168.200.1`

is normal and is output by the TUN/Tap Block during startup. The configuration
of the TUN/TAP interface is handled by the scripts in the ```apps``` folder.
- Did you try to tune the RF frequency out of the band of interest (i.e. used
  the LO offset menu of the flow graphs)?
- If 'D's appear, it might be related to your Ethernet card. Assert that you
  made the sysconf changes recommended by Ettus. Did you try to connect you PC
  directly to the USRP without a switch in between?

# Further information

Credit to Bastian Bloessl and other key contributors to [gr-ieee802-11](https://github.com/bastibl/gr-ieee802-11) for providing a key foundation for this repository. For further information on their lab, please checkout their project page
[https://www.wime-project.net](https://www.wime-project.net)

[Presentation on gr-ieee802-11ah](https://www.youtube.com/watch?v=x1QhxR8Mw5o&t=1256s) at GNU Radio Conference 2024. This is prior to contributions by @neo-knight-td that enabled full transceiver functionality for 1MHz S1G PPDUs, so we will have to create another video to show how this solution works.

Any issues or suggested improvements, such as support for other channel bandwidths, please raise an issue or initiate a pull request.
