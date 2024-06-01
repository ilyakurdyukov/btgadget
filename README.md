## Bluetooth gadgets tool

Command line tool for various Bluetooth gadgets.

Just a personal tool for experiments. May be useful as example code.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, USE AT YOUR OWN RISK!

### Usage

`btgadget [options] commands...`

#### Options

- `--dst`: destination (gadget) address  
- `--verbose N`: verbosity level  

#### Commands

- `tjd`: switch to TJD mode (fitness bracelets)  
- `moyoung`: switch to Moyoung mode (smart watches)  
- `atorch`: display data from Atorch USB tester  
- `batlevel`: read battery level (common UUID)  
- `timeout N`: change timeout  

#### Commands (TJD mode)

- `info`: device info  
- `batlevel`: read battery level  
- `finddev`: find device feature  
- `timesync`: synchronize time  
- `setlanguage N`: set language (0..33)  

#### Commands (Moyoung mode)

- `info`: device info  
- `finddev`: find device feature  
- `timesync`: synchronize time  

