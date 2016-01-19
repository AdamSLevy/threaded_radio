#pragma once

//#define DEFAULT_TTY_PORT_NAME "/dev/ttyS1"

#define DEFAULT_TTY_PORT_NAME "/dev/cu.usbserial-A103N2XP"

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
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "zlib.h" // compression lib
#include "crc32.h"
#include <vector>
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
using std::vector;
using std::string;
using std::exception;

typedef unsigned long ulong;
typedef unsigned char byte;

#define HEADER_SIZE     4
#define ID_SIZE         2
#define PKT_DATA_SIZE 128
#define CRC_SIZE        4
#define FOOTER_SIZE     4

#define PKT_SIZE(data_len) (HEADER_SIZE + ID_SIZE + data_len + CRC_SIZE + FOOTER_SIZE)
#define MAX_PKT_SIZE PKT_SIZE(PKT_DATA_SIZE)
#define HEAD_PKT_SIZE PKT_SIZE(1 + CRC_SIZE)
#define MSG_SIZE(data_len) (HEAD_PKT_SIZE + MAX_PKT_SIZE * (data_len / PKT_DATA_SIZE) + PKT_SIZE(data_len % PKT_DATA_SIZE))

// for standard packet with full data
#define ID_OFFSET       HEADER_SIZE
#define PKT_DATA_OFFSET (ID_OFFSET+ID_SIZE)
#define CRC_OFFSET(data_len)      (PKT_DATA_OFFSET+data_len)
#define FOOTER_OFFSET(data_len)   (CRC_OFFSET(data_len)+CRC_SIZE)

#define MAX_PKTS_WRITE_LOOP 20
#define MAX_ACK_SIZE (HEADER_SIZE + MAX_PKTS_WRITE_LOOP + 2 + 2 + CRC_SIZE + FOOTER_SIZE)
#define MAX_NUM_ATTEMPTS 3
#define NUM_BYTES_WRITE_CALL 128
#define READ_BUF_SIZE (MAX_ACK_SIZE * 4)

struct Packet{
    byte len = MAX_PKT_SIZE;
    byte send_count = MAX_NUM_ATTEMPTS;
    byte data[MAX_PKT_SIZE]={0xFA,0xFF,0xFF,0xFA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0xFF,0xFF,0xFE,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                              0x00,0x00,0x00,0x00,0x00,0xFE,0xFF,0xFF,0xFE};
};

class RadioManager
{
public:
    RadioManager();
    ~RadioManager();
    int setUpSerial();
    int closeSerial();
    int send(byte * data, const ulong numBytes);
    //int sendCompressed(byte * data, const ulong numBytes);

private:
    void write_loop();
    int m_fd; // file descripter for serial port
    struct termios m_oldConfig;
    struct termios m_config;
    string m_ttyPortName;
    CRC32 m_crc;
    //const ulong HEADER;
    const ulong FOOTER;

    vector<Packet> to_send;

    vector<Packet> send_window;
    byte * window_buf;

    // mutexes
    mutex shared_mem_mtx;   // shared memory mutex
    mutex write_cv_mtx;     // write thread mutex
    thread write_th;
    condition_variable write_cv;
    bool end_thread;
    bool is_open;

    int num_pkts;

};
