# RadioManager Test Program
This is a test program for the RadioManager class used for sending telemetry serially over RFD900+ radios on the microDOAS payload. Written by Adam Levy for Northern Embedded Solutions (NES). The `main` of this program makes no effort to be good code, it will never be used for anything other than testing. The `main` will just try to set up the serial connection and send arbitrary data. If the set up or sending fails at any point it will prompt the user if they want to retry or exit the program.

## class RadioManager
The RadioManager class 

1. manages the OS calls to open/write/close a serial port on linux using [termios](http://man7.org/linux/man-pages/man3/termios.3.html), 

2. compresses arbitrary data using [zlib](http://www.zlib.net/) prior to sending and packetizes the data with header/footer, message/packet id's, and a four byte crc32 checksum, 

3. sends packets in a rolling window style which waits for acknowledgments every `MAX_PKTS_WRITE_LOOP` number of packets. Unacknowledged packets are resent up to `MAX_NUM_ATTEMPTS`. (`MAX_PKTS_WRITE_LOOP` and `MAX_NUM_ATTEMPTS` are `#define`'s in radiomanager.h)


## Building
Simply run `make` in the project directory. The executable will be named `./emulator`

Notes/Requirements:
- The program won't do much if it cannot open the serial port. Specify the path for the serial port using `#define DEFAULT_TTY_PORT_NAME "/dev/ttyS1"` at the very top of *radiomanager.h* 
- This project links against zlib using the `-lz` compiler flag, so this must be on your system.
- This project uses `c++11`
