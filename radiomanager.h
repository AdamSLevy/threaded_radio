#pragma once

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
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "zlib.h" // compression lib
#include "crc32.h"
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>

#include <iostream>
using std::endl;
using std::cout;
using std::mutex;
using std::thread;
using std::unique_lock;
using std::condition_variable;
using std::deque;
using std::string;
using std::exception;

typedef unsigned long ulong;
typedef unsigned char byte;

#define HEADER_SIZE     4
#define ID_SIZE         2
#define PKT_DATA_SIZE 128
#define CRC_SIZE        4
#define FOOTER_SIZE     4

#define MAX_PKT_SIZE (HEADER_SIZE + ID_SIZE + PKT_DATA_SIZE + CRC_SIZE + FOOTER_SIZE)
#define HEAD_PKT_SIZE (HEADER_SIZE + ID_SIZE + 1 + CRC_SIZE + CRC_SIZE + FOOTER_SIZE)

#define MSG_BUF_SIZE    (4096*3)    // 12288 bytes, up to 86 packets
#define SHARED_BUF_SIZE (4096*12*2)   // 49152 bytes, 346 packets of size 142bytes, 4.9 messages at 70 packets per msg, 

// for standard packet with full data
#define ID_OFFSET       HEADER_SIZE
#define PKT_DATA_OFFSET (ID_OFFSET+ID_SIZE)
#define CRC_OFFSET(data_len)      (PKT_DATA_OFFSET+data_len)
#define FOOTER_OFFSET(data_len)   (CRC_OFFSET(data_len)+CRC_SIZE)

#define MAX_PKTS_SHARED_BUF (SHARED_BUF_SIZE / MAX_PKT_SIZE)
#define MAX_PKTS_MSG_BUF (MSG_BUF_SIZE / MAX_PKT_SIZE)

#define MAX_PKTS_WRITE_LOOP 20
#define MAX_NUM_ATTEMPTS 3
#define NUM_BYTES_WRITE_CALL 128

struct Packet{
    byte msg = 0;
    byte pkt = 0;
    byte len = 0;
    byte send_count = MAX_NUM_ATTEMPTS;
    byte * ptr = NULL;
    string crc;
};

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
    void write_loop();
    int m_fd; // file descripter for serial port
    struct termios m_oldConfig;
    struct termios m_config;
    string m_ttyPortName;
    CRC32 m_crc;
    const ulong HEADER;
    const ulong FOOTER;

    byte * shared_buf;
    byte * msg_buf;

    deque<Packet> send_q;

    // mutexes
    mutex shared_mem_mtx;   // shared memory mutex
    mutex write_cv_mtx;     // write thread mutex
    thread write_th;
    condition_variable write_cv;
    bool end_thread;

};
