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

#define HEADER_HEX 0xFaFfFfFa
#define FOOTER_HEX 0xFeFfFfFe

#define MAX_ID 0xFF

#define HEADER_SIZE     4
#define ID_SIZE         2
#define PKT_DATA_SIZE 128
#define CRC_SIZE        4
#define FOOTER_SIZE     4

#define PKT_SIZE(data_len)  (HEADER_SIZE + ID_SIZE + data_len + CRC_SIZE + FOOTER_SIZE)
#define MSG_SIZE(data_len)  (HEAD_PKT_SIZE + MAX_PKT_SIZE * (data_len / PKT_DATA_SIZE) + PKT_SIZE(data_len % PKT_DATA_SIZE))
#define MAX_PKT_SIZE        PKT_SIZE(PKT_DATA_SIZE)
#define HEAD_PKT_DATA_SIZE  1 + 4 // 1 for num_pkts, 4 for message crc
#define HEAD_PKT_SIZE       PKT_SIZE(HEAD_PKT_DATA_SIZE)

// for standard packet with full data
#define ID_OFFSET       HEADER_SIZE
#define PKT_DATA_OFFSET (ID_OFFSET+ID_SIZE)
#define CRC_OFFSET(data_len)      (PKT_DATA_OFFSET+data_len)
#define FOOTER_OFFSET(data_len)   (CRC_OFFSET(data_len)+CRC_SIZE)

#define NUM_PKTS_PER_ACK 20
#define MAX_ACK_SIZE (HEADER_SIZE + NUM_PKTS_PER_ACK + 2 + 2 + CRC_SIZE + FOOTER_SIZE)
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

struct MsgPktID{
    byte msg_id;
    byte pkt_id;
};

class RadioManager
{
public:
    RadioManager();
    ~RadioManager();
    int setUpSerial();
    int closeSerial();
    int send(byte * data, const ulong numBytes);

    bool send_in_progress();

private:
    int m_wfd; // file descripter for serial port
    mutex is_writing_mtx;
    int m_rfd;
    mutex is_reading_mtx;

    int m_ack_count;
    int m_bad_crc;

    struct termios m_oldConfig;
    struct termios m_config;
    string m_ttyPortName;
    CRC32 m_crc;
    const ulong HEADER;
    const ulong FOOTER;

    // populated by send(), emptied by write_loop()
    vector<Packet> to_send;
    mutex to_send_mtx;

    // populated by write_loop(), emptied by read_loop()
    vector<Packet> to_ack;
    mutex to_ack_mtx;

    // populated by read_loop(), empted by write_loop()
    vector<Packet> to_resend;
    mutex to_resend_mtx;

    // solely used by write_loop()
    vector<Packet> send_window;

    // WRITE
    void write_loop();
    void wake_write_loop();
    thread write_th;
    condition_variable write_cv;    // used for waking up write_loop
    mutex write_cv_mtx;             // used by read_loop() and send() if write_loop doesn't own write_cr_mtx
                                    // which indicates that write_loop is waiting and not writing
                                    
    void read_loop();
    thread read_th;

    void verify_crc(string data);
    void request_ack_resend();


    bool end_thread;
    bool is_open;

    int num_pkts;

};
