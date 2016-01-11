#ifndef RADIOMANAGER_H
#define RADIOMANAGER_H

#define DEFAULT_TTY_PORT_NAME "S1"

// set up serial error codes
#define OPEN_SUCCESS        0x00
#define OPEN_FAIL           0x01
#define NOT_A_TTY           0x02
#define GET_CONFIG_FAIL     0x04
#define BAUD_FAILED         0x08
#define CONFIG_APPLY_FAIL   0x10
#define CLOSE_FAIL          0x20

#define MAX_BYTES (256*1)

#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "zlib.h" // compression lib
#include "crc32.h"

#include <iostream>
using std::endl;
using std::cout;

using std::string;

typedef unsigned long ulong;
typedef unsigned char byte;

class RadioManager
{
public:
    RadioManager();
    ~RadioManager();
    int setUpSerial();
    int closeSerial();
    int send(byte * data, const ulong numBytes);
    int sendCompressed(byte * data, const ulong numBytes);

private:
    int m_fd; // file descripter for serial port
    struct termios m_oldConfig;
    struct termios m_config;
    string m_ttyPortName;
    CRC32 m_crc;
    const ulong HEADER;
//    const ulong FOOTER;
    const ulong HEADER_SIZE;
};

#endif // RADIOMANAGER_H
