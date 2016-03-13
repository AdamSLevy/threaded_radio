#pragma once


// OS sys calls, serial port
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>


#include "zlib.h"       // compression lib
#include "crc32.h"      // class CRC32

// STL containers
#include <vector>
using std::vector;
#include <string>
using std::string;

// threading
#include <mutex>
using std::mutex;
using std::unique_lock;
#include <thread>
using std::thread;
#include <condition_variable>
using std::condition_variable;

// misc
#include <algorithm>
using std::min;

// debug
#include <sys/time.h>
#include <iomanip>
using std::setw;
#include <stdio.h>
using std::printf;
#include <iostream>
using std::endl;
using std::cout;
using std::cerr;
#define PRINT_DEBUG {printf("\tFile: %s, Line: %i\n",__FILE__,__LINE__);}

// PORT NAME
#ifndef __APPLE__
    #define DEFAULT_TTY_PORT_NAME "/dev/ttyS1"
    //#define DEFAULT_TTY_PORT_NAME "/dev/ttyUSB0"
#endif
#ifdef __APPLE__
    #define DEFAULT_TTY_PORT_NAME "/dev/cu.usbserial-A103N2XP"
#endif
// PORT NAME

typedef unsigned long ulong;
typedef unsigned char byte;


// set up serial error codes
#define OPEN_SUCCESS        0x00
#define OPEN_FAIL           0x01
#define NOT_A_TTY           0x02
#define GET_CONFIG_FAIL     0x04
#define BAUD_FAILED         0x08
#define CONFIG_APPLY_FAIL   0x10
#define CLOSE_FAIL          0x20
#define IS_ALREADY_OPEN     0x40

#define HEADER_HEX 0xFaFfFfFa
#define FOOTER_HEX 0xFeFfFfFe

#define MAX_ID 0xFF

#define HEADER_SIZE     4
#define ID_SIZE         2
#define PKT_DATA_SIZE 256
#define CRC_SIZE        4
#define FOOTER_SIZE     4

#define PKT_SIZE(data_len)  (HEADER_SIZE + ID_SIZE + (data_len) + CRC_SIZE + FOOTER_SIZE)
#define MSG_SIZE(data_len)  (HEAD_PKT_SIZE + MAX_PKT_SIZE * ((data_len) / PKT_DATA_SIZE) + PKT_SIZE((data_len) % PKT_DATA_SIZE))
#define MAX_PKT_SIZE        PKT_SIZE(PKT_DATA_SIZE)
#define HEAD_PKT_DATA_SIZE  1 + 4 // 1 for num_pkts, 4 for message crc
#define HEAD_PKT_SIZE       PKT_SIZE(HEAD_PKT_DATA_SIZE)

// for standard packet with full data
#define ID_OFFSET                   HEADER_SIZE
#define PKT_DATA_OFFSET             (ID_OFFSET+ID_SIZE)
#define CRC_OFFSET(data_len)        (PKT_DATA_OFFSET+(data_len))
#define FOOTER_OFFSET(data_len)     (CRC_OFFSET(data_len)+CRC_SIZE)

#define NUM_PKTS_PER_ACK 10
#define MAX_ACK_SIZE (HEADER_SIZE + NUM_PKTS_PER_ACK + 2 + 2 + CRC_SIZE + FOOTER_SIZE)
#define MAX_NUM_ATTEMPTS 20
#define MAX_BYTES_PER_WRITE 8000//500
#define READ_BUF_SIZE (MAX_ACK_SIZE * 4)
#define MAX_ACKS_AUTO_RESEND 3
#define WINDOW_BUF_SIZE ((NUM_PKTS_PER_ACK+1)*MAX_PKT_SIZE)

#define SELECT_SEC_DELAY  4
#define SELECT_NSEC_DELAY 0
//#define r16(e)   r4( r4(e))
//#define r32(e)   r2(r16(e))
//#define r64(e)   r2(r32(e))
//#define r256(e) r16(r16(e))

#define HEADER_INIT 0xFA,r2(0xFF),0xFA
#define FOOTER_INIT 0xFE,r2(0xFF),0xFE

#define r2(e)    e,e
#define r4(e)    r2( r2(e) )
#define r8(e)    r2( r4(e) )
#define r11(e)  r8(e),r2(e),e

#define HEAD_PKT_INIT HEADER_INIT,r11(0x00),FOOTER_INIT     // takes up 19 bytes

// The below macro magic generates a static initialization of the data
// which automatically resizes according to however the max pkt data
// is set in the above macros, such as PKT_DATA_SIZE which are used to 
// compute MAX_PKT_SIZE. The static initialization allows the headers
// and footers to be in place whenever a new Packet object is created.
// Footers are placed to accomodate the HEAD_PKT_SIZE and MAX_PKT_SIZE
// For shorter packets, the necessary footer must be added.
//
// static_init.h was found on the blog Approxion. 
// Posted by Ralf on 08/06/2008
// Accessed by Adam Levy on 1/25/16
// https://www.approxion.com/?p=24
//
// The Packet struct holds 
//      len: data length, 
//      send_rem: the number of remaining send attempts
//      data: the data bytes

struct Packet{
    size_t len              = MAX_PKT_SIZE;
    size_t send_rem         = MAX_NUM_ATTEMPTS;
    size_t num_acks_passed  = 0;
    bool acked              = false;
    byte data[MAX_PKT_SIZE] = { HEAD_PKT_INIT,
                                    #define STATIC_INIT_VALUE 0x00
                                    #define STATIC_INIT_COUNT (MAX_PKT_SIZE - HEAD_PKT_SIZE - FOOTER_SIZE)
                                    #include "static_init.h"
                                FOOTER_INIT};
};

struct AwaitingAck{
    size_t num_acks_passed = 0;
    vector<Packet> to_ack;
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
    int open_serial();
    int open_serial(string port_name);
    int close_serial();
    int queue_data(byte * data, const ulong numBytes);

    // this will clear all data and stop transmitions but leave the serial port open
    // transmittion will resume the next time queue_data is called
    void clear_queued_data();

    bool send_in_progress();
    size_t queue_size();

private:
    // file descriptors and their mutexes
    // mutexes are necessary to prevent closing the port during a read/write call
    int m_wfd;
    mutex is_writing_mtx;   // used during write call
    int m_rfd;
    mutex is_reading_mtx;   // used during read call

    int m_ack_count;        // debug
    int m_bad_crc;          // debug
    int m_num_resent;
    int m_num_sent;

    struct termios m_oldConfig; // stores previous configuration
    struct termios m_config;    // stores existing configuration
    string m_port_name;         // stores the port name/path i.e. "/dev/ttyUSB0"

    const ulong HEADER;
    const ulong FOOTER;

    // Populated by send(), emptied by write_loop()
    vector<Packet> to_send;         // Holds new Packets awaiting to be sent
    mutex to_send_mtx;

    // Populated by write_loop(), emptied by read_loop()
    vector<Packet> to_ack;      // Holds a struct with a vector of sent packets awaiting acknowledgements.
    mutex to_ack_mtx;               // The vector groups Packets which will be acknowledged together.

    vector<Packet> to_ack_ack; // Holds acknowledgements of acknowledgements that need to be sent.
    mutex to_ack_ack_mtx;

    // populated by read_loop(), empted by write_loop(), resends are given priority by write_loop
    vector<Packet> to_resend;       // Holds packets that were not acknowledged and need to be resent
    mutex to_resend_mtx;

    // solely used by write_loop()
    vector<Packet> send_window;     // Holds the current packets being sent

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
    void ack_ack(MsgPktID);

    bool is_open;

    int num_pkts;

    int call_read_select();
};
