#include "radiomanager.h"

class write_err: public exception/*{{{*/
{
	virtual const char * what() const throw()
	{
		return "A write error occurred.";
	}
} write_err;/*}}}*/

RadioManager::RadioManager(): HEADER(HEADER_HEX),FOOTER(FOOTER_HEX)/*{{{*/
{
    window_buf = (byte *) calloc(NUM_PKTS_PER_ACK*MAX_PKT_SIZE,sizeof(byte));
    m_ttyPortName = DEFAULT_TTY_PORT_NAME;

    end_thread = false;
    is_open = false;
    m_fd = -1;
    num_pkts = 0;

}/*}}}*/

RadioManager::~RadioManager()/*{{{*/
{
    write_cv_mtx.lock();
    write_cv.notify_one();
    end_thread = true;
    write_cv_mtx.unlock();
    closeSerial();
    cout << " num pkts: " << num_pkts << endl;
    free(window_buf);
}/*}}}*/

int RadioManager::setUpSerial()/*{{{*/
{
    // open the port
    m_fd = open(m_ttyPortName.c_str(), O_RDWR | O_NOCTTY );
    cout << "after open" << endl;

    if(m_fd < 0){
        return OPEN_FAIL;
    }

    // check if valid serial port
    if(!isatty(m_fd)){
        return NOT_A_TTY;
    }

    // copy old config
    if(tcgetattr(m_fd, &m_oldConfig) < 0){
        return GET_CONFIG_FAIL;
    }

    m_config = m_oldConfig;

    // Input flags - turn off input processing
    m_config.c_iflag = 0;
    //m_config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL |
                         //INLCR | PARMRK | INPCK | ISTRIP | IXON);

    // Output flags - turn off output processing
    m_config.c_oflag = 0;

    // No line processing
    m_config.c_lflag = 0;
    //m_config.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);

    // No character processing
    //m_config.c_cflag &= CRTSCTS;
    m_config.c_cflag &= ~(CSIZE | PARENB);
    m_config.c_cflag |= CS8;

    m_config.c_cc[VMIN] = 1;
    m_config.c_cc[VTIME] = 0;

    // baud rate
    if(cfsetspeed(&m_config, B115200)){
        return BAUD_FAILED;
    }

    // apply config to port
    if(tcsetattr(m_fd,TCSAFLUSH, &m_config) < 0){
        return CONFIG_APPLY_FAIL;
    }

    if(tcflush(m_fd, TCIOFLUSH) != 0 ){
        cout << "flush issue" << endl;
    }

    is_open = true;

    write_th = thread(&RadioManager::write_loop,this);
    write_th.detach();

    read_th = thread(&RadioManager::read_loop,this);
    read_th.detach();

    return OPEN_SUCCESS;
}/*}}}*/

int RadioManager::closeSerial()/*{{{*/
{
    //fsync(m_fd);
    // revert to old config settings
    int exitCode = 0;
    if(m_fd != -1 && is_open){
        if(tcsetattr(m_fd,TCSAFLUSH, &m_oldConfig) < 0){
            exitCode |= CONFIG_APPLY_FAIL;
        }
        if(close(m_fd) < 0){
            exitCode |= CLOSE_FAIL;
        }
        is_open = false;
        end_thread = true;
        m_fd = -1;
    }

    return exitCode;
}/*}}}*/

#include <iomanip>
#include <stdio.h>
using std::printf;
void print_hex(byte * data, size_t len)/*{{{*/
{
    int perLine = 20;
    cout << "   0:  ";
    for(int i = 0; i < len; i++){
        byte bb = data[i];
        byte hb = (bb&0xF0)>>4;
        byte lb = bb&0x0F;
        char hc;
        char lc;
        if(hb>0x09){
            hc = 'A' + (hb-0x0A);
        } else{
            hc = '0' + hb;
        }
        if(lb>0x09){
            lc = 'A' + (lb-0x0A);
        } else{
            lc = '0' + lb;
        }
        cout << hc << lc << " ";
        if(i%perLine == perLine - 1 && i!=len-1 ){
            cout << endl;
            cout << std::setw(4) << i << ":  ";
        }
    }
    cout << endl << endl;
}/*}}}*/

string to_hex(byte bb)/*{{{*/
{
    byte hb = (bb&0xF0)>>4;
    byte lb = bb&0x0F;
    string out = "00";
    if(hb>0x09){
        out[0] = 'A' + (hb-0x0A);
    } else{
        out[0] = '0' + hb;
    }
    if(lb>0x09){
        out[1] = 'A' + (lb-0x0A);
    } else{
        out[1] = '0' + lb;
    }
    return out;
}/*}}}*/

void print_pkt(Packet pkt){/*{{{*/
    string out = "Packet\n\tmsgID: %s\n\tpktID: %s\n\tlen: \n";
    printf(out.c_str(),to_hex(pkt.data[ID_OFFSET]).c_str(),to_hex(pkt.data[ID_OFFSET+1]).c_str(),to_hex(pkt.len).c_str());
    print_hex(pkt.data,pkt.len);
}/*}}}*/


int RadioManager::send(byte * data, const ulong numBytes)/*{{{*/
{
    static byte msgID = 0;/*{{{*/
    static byte * foot_ptr = (byte *)(&FOOTER);
    
    int numSent = -1;
    if(!is_open)
        return numSent;
    if(msgID == MAX_ID)
        msgID = 0;

    size_t sizeDataCompressed = (numBytes * 1.1) + 12;
    byte dataCompressed[sizeDataCompressed];
    int z_result = compress( dataCompressed, &sizeDataCompressed, data, numBytes);
    size_t numPkts = sizeDataCompressed / PKT_DATA_SIZE + 1;
    /*}}}*/
    if(Z_OK == z_result && numPkts < MAX_ID){
        // set up 
        if((byte) numPkts == MAX_ID)
            return -1;
        size_t numTotalBytesForPkts = MSG_SIZE(sizeDataCompressed);
        
        size_t bytesRemaining = sizeDataCompressed;
        byte pktID = 0;
        Packet * pkt_to_send = new Packet[numPkts+1];

        // create HEAD PKT/*{{{*/
        byte * pkt_data = pkt_to_send[0].data;

        // ID
        pkt_data[ID_OFFSET]=msgID;
        pkt_data[ID_OFFSET+1]=pktID;

        // DATA: number of packets and compressed data crc
        pkt_data[PKT_DATA_OFFSET] = (byte)numPkts;

        m_crc.reset();
        m_crc.add(dataCompressed,sizeDataCompressed);
        m_crc.getHash(pkt_data+PKT_DATA_OFFSET+3);

        // PKT CRC
        m_crc.reset();
        m_crc.add(pkt_data+ID_OFFSET,ID_SIZE + HEAD_PKT_DATA_SIZE);
        m_crc.getHash(pkt_data+CRC_OFFSET(HEAD_PKT_DATA_SIZE));

        pkt_to_send[pktID].len = HEAD_PKT_SIZE;
        /*}}}*/

        // create remaining data filled packets /*{{{*/ 
        for(pktID = 1; pktID < numPkts+1; pktID++){
            int data_len = PKT_DATA_SIZE;
            if(bytesRemaining < PKT_DATA_SIZE)
                data_len = bytesRemaining;
            bytesRemaining -= data_len;

            pkt_data = pkt_to_send[pktID].data;
            byte pkt_len = PKT_SIZE(data_len);
            pkt_to_send[pktID].len = pkt_len;

            // ID
            pkt_data[ID_OFFSET]=msgID;
            pkt_data[ID_OFFSET+1]=pktID;

            // DATA
            memcpy(pkt_data+PKT_DATA_OFFSET,dataCompressed+(pktID-1)*PKT_DATA_SIZE, data_len);

            // PKT CRC
            m_crc.reset();
            m_crc.add(pkt_data+ID_OFFSET,data_len+ID_SIZE);
            m_crc.getHash(pkt_data+CRC_OFFSET(data_len));

            // FOOTER
            if(data_len < PKT_DATA_SIZE){
                for(int i = 0; i < FOOTER_SIZE; i++)
                    pkt_data[FOOTER_OFFSET(data_len) + i]=foot_ptr[i];
            }
        }
        /*}}}*/

        // copy pkts to shared mem
        to_send_mtx.lock();      // MUTEX LOCK
        for(pktID = 0; pktID < numPkts+1; pktID++){
            to_send.push_back(pkt_to_send[pktID]);
        }
        to_send_mtx.unlock();    // MUTEX UNLOCK

        if(write_cv_mtx.try_lock()){
            write_cv.notify_one();
            write_cv_mtx.unlock();
        }

        delete [] pkt_to_send;
        numSent = numTotalBytesForPkts;
        msgID++;
    }
    return numSent;
}/*}}}*/

/*{{{*//* // send compressed
int RadioManager::sendCompressed(byte * data, const ulong numBytes)
{
    ulong sizeDataCompressed = (numBytes * 1.1) + 12;
    byte dataCompressed[sizeDataCompressed];
    int z_result = compress( dataCompressed, &sizeDataCompressed, data, numBytes);
    int numSent = -1;
    if(Z_OK == z_result){
        numSent = 0;
        const int scale = 3; 
        const int P_SIZE = scale*128;
        byte * p_start;
        ulong bytesRemaining = sizeDataCompressed;
        for(unsigned int i = 0; i < sizeDataCompressed / P_SIZE + 1; i++){
            if(tcdrain(m_fd) != 0){
                int errsv = errno;
                cout << "tcdrain: " << errsv << endl;
                char buf[40];
                strerror_r(errsv,buf,40);
                cout << buf << endl;
            }
            usleep(scale*50000);
            p_start = dataCompressed + i * P_SIZE;
            int len = P_SIZE;
            if(bytesRemaining < P_SIZE){
                len = bytesRemaining;
            }
            m_crc.reset();
            m_crc.add(p_start, len);
            byte hash[m_crc.HashBytes];
            m_crc.getHash(hash);
            cout << m_crc.getHash() << endl;
            numSent += write(m_fd, &HEADER, HEADER_SIZE);
            numSent += write(m_fd, hash, m_crc.HashBytes);
            numSent += write(m_fd, p_start, len);
            bytesRemaining -= len;
        }
    }
    return numSent;
}*//*}}}*/

void RadioManager::write_loop()/*{{{*/
{
    unique_lock<mutex> lck(write_cv_mtx);
    while(is_open && (!end_thread || !send_window.empty() || !to_send.empty() || !to_resend.empty())){
        // resend packets get priority
        // add packets to send_window, until full or to_resend is empty
        int num_pkts_added = 0;
        to_resend_mtx.lock();  // MUTEX LOCK
        while(send_window.size() < NUM_PKTS_PER_ACK && num_pkts_added < to_resend.size()){
            Packet pkt = to_resend[num_pkts_added++];
            send_window.push_back(pkt);
        }
        // from to_resend, remove the packets that were added to send_window
        to_resend.erase(to_resend.begin(),to_resend.begin()+num_pkts_added);
        to_resend_mtx.unlock(); // MUTEX UNLOCK

        // now add the to_send packets
        num_pkts_added = 0; 
        to_send_mtx.lock();  // MUTEX LOCK
        while(send_window.size() < NUM_PKTS_PER_ACK && num_pkts_added < to_send.size()){
            Packet pkt = to_send[num_pkts_added++];
            send_window.push_back(pkt);
        }
        // from to_send, remove the packets that were added to send_window
        to_send.erase(to_send.begin(),to_send.begin()+num_pkts_added);
        to_send_mtx.unlock(); // MUTEX UNLOCK

        // the packets in send_window will need to be acknowledged
        to_ack_mtx.lock();
        for( auto pkt : send_window )
            to_ack.push_back(pkt);
        to_ack_mtx.unlock();


        // write the packet data to the window buffer
        byte * window_ptr = window_buf;
        for(auto & pkt_iter : send_window){
            pkt_iter.send_count--;
            memcpy(window_ptr,pkt_iter.data,pkt_iter.len);
            //print_pkt(*pkt_iter);
            num_pkts++;
            window_ptr += pkt_iter.len;
        }
        //print_hex(window_buf, window_ptr - window_buf);

        // write the data
        size_t num_bytes_to_send = window_ptr - window_buf;
        //cout << " num bytes to send: " << num_bytes_to_send << endl;
        window_ptr = window_buf;
        static size_t bytes_sent = 0;
        while(num_bytes_to_send > 0){
            int num_bytes_sent = 0;
            try{
                size_t max_bytes = 500;
                if(max_bytes > num_bytes_to_send)
                    max_bytes = num_bytes_to_send;
                num_bytes_sent = write(m_fd,window_ptr,max_bytes);
                if(num_bytes_sent < 0){
                    throw write_err;
                }
            } catch(exception err){
                //cout << err.what() << endl;
                is_open = false;
                return;
            }
            num_bytes_to_send -= num_bytes_sent;
            bytes_sent += num_bytes_sent;
            window_ptr += num_bytes_sent;
            //cout << "num_bytes_sent: " << num_bytes_sent << " total: " << bytes_sent << endl;

            if(tcdrain(m_fd) != 0){
                int errsv = errno;
                //cout << "tcdrain: " << errsv << endl;
                char buf[40];
                strerror_r(errsv,buf,40);
                //cout << buf << endl;
            }
            if(tcflush(m_fd, TCOFLUSH) != 0 ){
                //cout << "flush issue" << endl;
            }
            size_t send_time = 1e6 * num_bytes_sent / (115200/9)+100000;
            //cout << "sleep_time " << send_time << endl;
            usleep(send_time);
        }

        // populate send_window
        send_window.clear();

        // if nothing to send, wait until notify
        while(to_send.empty() && to_resend.empty() && send_window.empty() && !end_thread && is_open) 
            write_cv.wait(lck);
    }
    //cout << "exiting the thread, free window buf" << endl;
}/*}}}*/

int call_select(int fd, size_t delay_sec, size_t delay_usec)/*{{{*/
{
    fd_set rfds;            // stores which fd's should be watched for bytes available to read
    struct timeval tv;      // stores timeout for select
    
    FD_ZERO(&rfds);         // clear fd's
    FD_SET(fd, &rfds);    // add m_fd to rfds

    // set delay
    tv.tv_sec = delay_sec;
    tv.tv_usec = delay_usec;

    cout << " wait " << endl;
    return select(1, &rfds, nullptr, nullptr, &tv);   // will return on bytes available or timeout
}/*}}}*/

size_t count(const string to_search, const string to_count){/*{{{*/
    size_t count = 0;
    if(to_search.size() < to_count.size())
        return 0;
    for(int i = 0; i < to_search.size() - to_count.size(); i++){
        if(to_count == string(to_search.c_str()+i, to_count.size()))
            count++;
    }
    return count;
}/*}}}*/

void RadioManager::read_loop()/*{{{*/
{
    static byte read_buf[READ_BUF_SIZE];
    static byte * read_ptr = read_buf;
    static bool partial_pkt = false;
    static string current_pkt;
    string header((char *)(&HEADER),4);
    string footer((char *)(&FOOTER),4);
    while(is_open && (!end_thread || !to_ack.empty() || !to_resend.empty())){
        // read in new packet
        while(!end_thread && is_open && call_select(m_fd, 0, 1000000)){    // while there are still bytes available or .5 sec passes
            cout << "select was 1" << endl;
            size_t bytes_read = read(m_fd, read_ptr, READ_BUF_SIZE - (read_ptr - read_buf));
            cout << "bytes_read: " << bytes_read << endl;
            read_ptr += bytes_read;
            print_hex(read_buf, bytes_read); // debug
        }
        //size_t bytes_read = read(m_fd, read_ptr, READ_BUF_SIZE - (read_ptr - read_buf));
        //cout << "bytes_read: " << bytes_read << endl;
        //read_ptr += bytes_read;

        if(read_ptr - read_buf == 0)
            continue;
        string in_data((char *)read_buf, read_ptr-read_buf);


        size_t search_from = 0;
        if(partial_pkt){
            size_t footer_index = in_data.find(footer);
            size_t next_header_index = in_data.find(header,0);

            if(footer_index != string::npos){
                if(next_header_index == string::npos || footer_index < next_header_index){
                    current_pkt += string((char *)read_buf,footer_index);
                    verify_crc(current_pkt);
                    partial_pkt = false;
                }
            } else if (next_header_index == string::npos){
                current_pkt += in_data;
                partial_pkt = true;
            } else{
                partial_pkt = false;
            }
            if(next_header_index != string::npos){
                search_from = next_header_index;
            }
        }

        size_t num_headers = count(in_data, header);

        for(int i = 0; i < num_headers; i++){
            size_t header_index = in_data.find(header);
            size_t footer_index = in_data.find(footer,header_index);

            size_t next_header_index = in_data.find(header,header_index+4);

            if(footer_index != string::npos){
                if(next_header_index == string::npos || footer_index < next_header_index){
                    current_pkt = string((char *)(read_buf + header_index + 4), footer_index - header_index - 4);
                    verify_crc(current_pkt);
                }
            } else if(next_header_index == string::npos){
                current_pkt = string((char *)(read_buf + header_index + 4),read_ptr - (read_buf + header_index + 4));
                partial_pkt = true;
            }
            if(next_header_index != string::npos)
                search_from = next_header_index;
        }
    }
}/*}}}*/

struct MsgPktID{
    byte msg_id;
    byte pkt_id;
};

void RadioManager::verify_crc(string data){/*{{{*/
    static CRC32 crc;
    static vector<Packet> ack_resend_wait;

    crc.reset();
    crc.add(data.c_str(),data.size() - 4);
    byte h[4];
    crc.getHash(h);
    string hash((char *)h,4);

    if(hash == string(data.c_str() + data.size() - 4, 4)){
        byte * data_ptr = (byte *)data.c_str();
        MsgPktID id;
        bool resent_ack = false;
        if(data_ptr[0] == 0xFF){
            resent_ack = true;
        }
        id.msg_id = data_ptr[0 + resent_ack];
        id.pkt_id = data_ptr[1 + resent_ack];
        byte current_msg_id = data_ptr[0 + resent_ack];

        vector<MsgPktID> ack_id;
        ack_id.push_back(id);
        for(int i = 2 + resent_ack; i < data.size(); i++){
            byte bb = data_ptr[i];
            if(bb == 0xFF){
                current_msg_id = data_ptr[i+1];
                i++;
                continue;
            }
            id.msg_id = current_msg_id;
            id.pkt_id = bb;
            ack_id.push_back(id);
        }

        if(!resent_ack){
            to_ack_mtx.lock();
            bool resend_req = false;
            for(auto pkt : to_ack){
                id.msg_id = pkt.data[ID_OFFSET];
                id.pkt_id = pkt.data[ID_OFFSET+1];

                bool has_ack = false;
                for( auto p_id : ack_id ){
                    if( p_id.msg_id == id.msg_id &&
                            p_id.pkt_id == id.pkt_id ){
                        has_ack = true;
                        break;
                    }
                }
                if(!has_ack){
                    if( !resend_req ){
                        to_resend_mtx.lock();
                        resend_req = true;
                    }
                    to_resend.push_back(pkt);
                }
            }
            to_ack.clear();
            if(resend_req){
                to_resend_mtx.unlock();
                // wake up write loop
                if(write_cv_mtx.try_lock()){
                    write_cv.notify_one();
                    write_cv_mtx.unlock();
                }
            }
            to_ack_mtx.unlock();
        } else{
            bool resend_req = false;
            for(auto pkt : ack_resend_wait){
                id.msg_id = pkt.data[ID_OFFSET];
                id.pkt_id = pkt.data[ID_OFFSET+1];

                bool has_ack = false;
                for( auto p_id : ack_id ){
                    if( p_id.msg_id == id.msg_id &&
                            p_id.pkt_id == id.pkt_id ){
                        has_ack = true;
                        break;
                    }
                }
                if(!has_ack){
                    if( !resend_req ){
                        to_resend_mtx.lock();
                        resend_req = true;
                    }
                    to_resend.push_back(pkt);
                }
            }
            ack_resend_wait.clear();
            if(resend_req){
                to_resend_mtx.unlock();
                // wake up write loop
                if(write_cv_mtx.try_lock()){
                    write_cv.notify_one();
                    write_cv_mtx.unlock();
                }
            }
        }
    } else if(!to_ack.empty()){
        request_ack_resend();

        to_ack_mtx.lock();
        ack_resend_wait = to_ack;
        to_ack.clear();
        to_ack_mtx.unlock();
    }
}/*}}}*/

void RadioManager::request_ack_resend(){/*{{{*/
    cout << "request resend" << endl;
    to_resend_mtx.lock();

    Packet request_ack_pkt;
    request_ack_pkt.data[ID_OFFSET] = 0xFF;
    request_ack_pkt.data[ID_OFFSET+1] = 0xFF;
    CRC32 crc;
    crc.reset();
    crc.add(request_ack_pkt.data + ID_OFFSET, 2);
    crc.getHash(request_ack_pkt.data+CRC_OFFSET(0));
    memcpy(request_ack_pkt.data + FOOTER_OFFSET(0), (byte*)(&FOOTER), 4);
    request_ack_pkt.len = PKT_SIZE(0);
    to_resend.push_back(request_ack_pkt);

    to_resend_mtx.unlock();

    if(write_cv_mtx.try_lock()){
        write_cv.notify_one();
        write_cv_mtx.unlock();
    }
}/*}}}*/

