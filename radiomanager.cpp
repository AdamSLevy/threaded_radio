#include "radiomanager.h"

// V************* debug functions *********************************************V/*{{{*/
#include <iomanip>
#include <stdio.h>
using std::printf;
void print_hex(byte * data, size_t len)/*{{{*/
{
    size_t perLine = 20;
    cout << "   0:  ";
    for(size_t i = 0; i < len; i++){
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
// ^************** end debug functions ******************************************************^/*}}}*/

// V************** helper NON RadioManager functions *************************************V/*{{{*/
int call_select(int fd, size_t delay_sec, size_t delay_nsec)/*{{{*/
{
    if(fd < 0)
        return -1;
    fd_set rfds;            // stores which fd's should be watched for bytes available to read
    struct timespec tv;      // stores timeout for select
    
    FD_ZERO(&rfds);         // clear fd's
    FD_SET(fd, &rfds);    // add fd to rfds

    // set delay
    tv.tv_sec = delay_sec;
    tv.tv_nsec = delay_nsec;

    int ret = pselect(fd+1, &rfds, nullptr, nullptr, &tv, nullptr);   // will return on bytes available or timeout

    /*  // debug 
    if(ret < 0){
        int errsv = errno;
        //cout << "pselect: " << errsv << endl;     // debug
        char buf[40];
        //strerror_r(errsv,buf,40);   // debug
        cout << buf << endl;
    }
    */
    return ret;
}/*}}}*/

size_t count(const string & to_search, const string & to_count){/*{{{*/
    size_t count = 0;
    if(to_search.size() < to_count.size())
        return 0;
    for(size_t i = 0; i < to_search.size() - to_count.size(); i++){
        if(to_count == string(to_search.c_str()+i, to_count.size()))
            count++;
    }
    return count;
}/*}}}*/

size_t find_partial_end( const string & to_search, const string & to_find )/*{{{*/
{
    size_t max_offset = std::min(to_search.size(),to_find.size());
    for(int offset = max_offset - 1; offset >= 0; offset--){
        bool depth_partial = true;
        for(int place = offset; place >= 0; place--){
            if(to_search[to_search.size() - 1 - offset + place] != to_find[0 + place]){
                depth_partial = false;
                break;
            }
        }
        if(depth_partial)
            return offset + 1;
    }
    return 0;
}/*}}}*/
// ^************** end helper NON RadioManager functions ***********************************^/*}}}*/

// V************** RadioManager member functions *******************************************V
RadioManager::RadioManager(): HEADER(HEADER_HEX),FOOTER(FOOTER_HEX)/*{{{*/
{
    m_ttyPortName = DEFAULT_TTY_PORT_NAME;

    end_thread = false;
    is_open = false;
    m_wfd = -1;
    m_rfd = -1;
    num_pkts = 0;

}/*}}}*/

RadioManager::~RadioManager()/*{{{*/
{
    closeSerial();
    cout << " num pkts: " << num_pkts << endl;  // debug
    cout << " num acks received: " << m_ack_count << endl;  // debug
    cout << " num bad crc : " << m_bad_crc << endl; // debug
}/*}}}*/

int RadioManager::setUpSerial()/*{{{*/
{
    if(is_open)
        closeSerial();

    // open the port
    m_wfd = open(m_ttyPortName.c_str(), O_WRONLY | O_NOCTTY );
    m_rfd = open(m_ttyPortName.c_str(), O_RDONLY | O_NOCTTY );
    cout << "after open" << endl; // debug

    if(m_wfd < 0 || m_rfd < 0){
        return OPEN_FAIL;
    }

    // check if valid serial port
    if(!isatty(m_wfd) || !isatty(m_rfd)){
        return NOT_A_TTY;
    }

    // copy old config
    if(tcgetattr(m_wfd, &m_oldConfig) < 0){
        return GET_CONFIG_FAIL;
    }

    m_config = m_oldConfig;

    cfmakeraw(&m_config);

    m_config.c_cc[VMIN] = 0;
    m_config.c_cc[VTIME] = 5;

    // baud rate
    if(cfsetspeed(&m_config, B115200)){
        return BAUD_FAILED;
    }

    // apply config to port
    if(tcsetattr(m_wfd,TCSAFLUSH, &m_config) < 0){
        return CONFIG_APPLY_FAIL;
    }
    if(tcsetattr(m_rfd,TCSAFLUSH, &m_config) < 0){
        return CONFIG_APPLY_FAIL;
    }

    // flush ports
    if(tcflush(m_wfd, TCIFLUSH) != 0 ){
        //std::cerr << "m_wfd flush issue" << endl;   // debug
    }
    if(tcflush(m_rfd, TCOFLUSH) != 0 ){
        //std::cerr << "m_rfd flush issue" << endl;   // debug
    }

    is_open = true;

    // launch threads;
    write_th = thread(&RadioManager::write_loop,this);
    write_th.detach();

    read_th = thread(&RadioManager::read_loop,this);
    read_th.detach();

    return OPEN_SUCCESS;
}/*}}}*/

int RadioManager::closeSerial()/*{{{*/
{
    unique_lock<mutex> rlck(is_reading_mtx);    // guarantees release on exit.
    unique_lock<mutex> wlck(is_writing_mtx);    // guarantees release on exit.

    //fsync(m_wfd);
    // revert to old config settings
    int exitCode = 0;
    if(m_rfd != -1){
        if(tcsetattr(m_rfd,TCSAFLUSH, &m_oldConfig) < 0){
            exitCode |= CONFIG_APPLY_FAIL;
        }
        if(close(m_rfd) < 0){
            exitCode |= CLOSE_FAIL;
        }
        m_rfd = -1;
    }
    if(m_wfd != -1){
        if(tcsetattr(m_wfd,TCSAFLUSH, &m_oldConfig) < 0){
            exitCode |= CONFIG_APPLY_FAIL;
        }
        if(close(m_wfd) < 0){
            exitCode |= CLOSE_FAIL;
        }
        m_wfd = -1;
    }

    is_open = false;
    wake_write_loop();

    //cout << "close exit: " << exitCode << endl; // debug

    return exitCode;
}/*}}}*/

void RadioManager::wake_write_loop()/*{{{*/
{
    if(write_cv_mtx.try_lock()){
        write_cv.notify_one();
        write_cv_mtx.unlock();
    }
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
    int z_result = compress( dataCompressed, (uLongf *)&sizeDataCompressed, data, numBytes);
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

        wake_write_loop();

        delete [] pkt_to_send;
        numSent = numTotalBytesForPkts;
        msgID++;
    }
    return numSent;
}/*}}}*/

void RadioManager::write_loop()/*{{{*/
{
    byte window_buf[NUM_PKTS_PER_ACK*MAX_PKT_SIZE];
    unique_lock<mutex> lck(write_cv_mtx);
    while( is_open ){
        // if nothing to send, wait until notify
        while(to_send.empty() && to_resend.empty() && send_window.empty()) 
            write_cv.wait(lck);

        if(!is_open){
            //cout << "write loop exit top" << endl; // debug
            return;
        }

        // resend packets get priority
        // add packets to send_window, until full or to_resend is empty
        size_t num_pkts_added = 0;
        to_resend_mtx.lock();  // MUTEX LOCK
        while(send_window.size() < NUM_PKTS_PER_ACK && num_pkts_added < to_resend.size()){
            Packet pkt = to_resend[num_pkts_added++];
            send_window.push_back(pkt);
        }
        // from to_resend, remove the packets that were added to send_window
        if(num_pkts_added)
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
        if(num_pkts_added)
            to_send.erase(to_send.begin(),to_send.begin()+num_pkts_added);
        to_send_mtx.unlock(); // MUTEX UNLOCK

        /*
        // the packets in send_window will need to be acknowledged
        to_ack_mtx.lock();
        for( auto pkt : send_window )
            to_ack.push_back(pkt);
        to_ack_mtx.unlock();
        */


        // write the packet data to the window buffer
        byte * window_ptr = window_buf;
        for(auto & pkt_iter : send_window){
            pkt_iter.send_count--;
            memcpy(window_ptr,pkt_iter.data,pkt_iter.len);
            //print_pkt(*pkt_iter); // debug
            num_pkts++;
            window_ptr += pkt_iter.len;
        }
        //print_hex(window_buf, window_ptr - window_buf);

        // write the data
        size_t num_bytes_to_send = window_ptr - window_buf;
        //cout << " num bytes to send: " << num_bytes_to_send << endl;
        window_ptr = window_buf;

        /*
        string haystack((char*)window_buf, num_bytes_to_send);
        string needle;
        needle.resize(3);
        for(auto &c : needle)
            c = '+';

        size_t esc_loc = haystack.find(needle);
        if(esc_loc)
            cout << "!!!! escape code present ~~~ " << endl;
        */


        static size_t bytes_sent = 0;
        while(num_bytes_to_send > 0){
            int num_bytes_sent = 0;
            size_t max_bytes = 500;

            if(max_bytes > num_bytes_to_send)
                max_bytes = num_bytes_to_send;

            is_writing_mtx.lock();
            num_bytes_sent = write(m_wfd,window_ptr,max_bytes);
            is_writing_mtx.unlock();

            if(num_bytes_sent < 0){
                //cout << " exiting write loop below write" << endl; // debug
                is_open = false;
                return;
            }

            num_bytes_to_send -= num_bytes_sent;
            bytes_sent += num_bytes_sent;
            window_ptr += num_bytes_sent;
            //cout << "num_bytes_sent: " << num_bytes_sent << " total: " << bytes_sent << endl;

            if(tcdrain(m_wfd) != 0){
                int errsv = errno;
                //cout << "tcdrain: " << errsv << endl;     // debug
                char buf[40];
                strerror_r(errsv,buf,40);
                //cout << buf << endl;          // debug
            }

            //if(tcflush(m_wfd, TCOFLUSH) != 0 ){
                //cout << "flush issue" << endl; // debug
            //}
            size_t send_time = 1e6 * num_bytes_sent / (115200/9)+100000;
            //cout << "sleep_time " << send_time << endl;   // debug
            usleep(send_time);
        }

        send_window.clear();
    }
    //cout << "exiting the write thread, bottom" << endl; // debug
}/*}}}*/

void RadioManager::read_loop()/*{{{*/
{
    byte read_buf[READ_BUF_SIZE];   // used for read() call
    bool partial_pkt = false;       // denotes whether current_pkt is awaiting completion
    string current_pkt;             // holds the packet data starting at the ID, excluding header and footer
    string in_data;                 // holds the latest data from read_buf, allows prefacing with broken headers
    string header((char *)(&HEADER),4);     // header bytes
    string footer((char *)(&FOOTER),4);     // footer bytes
    m_ack_count = 0;
    m_bad_crc = 0;

    while(is_open){
        // read in new packet
        //cout << "wait " << endl;    // debug
        // check for bytes to be read
        int select_ret = call_select(m_rfd, 2, 0);

        // -1 indicates error so exit the thread
        if(select_ret < 0){
            is_open = false;    // set to false to notify other threads
            return;
        }

        // while there are bytes to read...
        while(select_ret > 0){
            int bytes_read = 0;

            // read call
            is_reading_mtx.lock();
            bytes_read = read(m_rfd, read_buf, READ_BUF_SIZE);
            is_reading_mtx.unlock();

            // if error, exit the thread
            if(bytes_read < 0){
                is_open = false;
                //cout << "exit after read call < 0 " << endl; // debug
                return;
            }
            //cout << "bytes_read: " << bytes_read << endl;   // debug

            //cout << "read_buf:" << endl;    // debug
            //print_hex(read_buf, bytes_read); // debug

            in_data += string((char *)read_buf, bytes_read);

            //cout << "in_data: " << endl; // debug
            //print_hex((byte*)in_data.c_str(), in_data.size());  // debug

            if(bytes_read <= 4){
                int n_end_foot = find_partial_end(footer, in_data);
                int n_end_pkt = find_partial_end(current_pkt, footer);
                if(partial_pkt && n_end_foot + n_end_pkt == 4){
                    current_pkt.resize(current_pkt.size() - n_end_pkt);
                    verify_crc(current_pkt);
                    partial_pkt = false;
                    in_data.clear();
                }
                continue;
            }

            size_t end_head_bytes = find_partial_end(in_data,header);

            //cout << "end head bytes " << end_head_bytes << endl;


            size_t search_from = 0;
            if(partial_pkt){
                partial_pkt = false;
                //cout << "has partial pkt" << endl; // debug
                size_t footer_index = in_data.find(footer);
                size_t next_header_index = in_data.find(header,0);

                if(footer_index != string::npos){
                    if(next_header_index == string::npos || footer_index < next_header_index){
                        current_pkt += string((char *)in_data.c_str(),footer_index);
                        //cout << "partial pkt completed, no next head, or foot < head" << endl;  // debug
                        //print_hex((byte*)current_pkt.c_str(),current_pkt.size());               // debug
                        verify_crc(current_pkt);
                    } else{
                        //cout << "partial pkt bad: foot > head" << endl;                         // debug
                        //print_hex((byte*)current_pkt.c_str(),current_pkt.size());               // debug
                        current_pkt += string((char *)in_data.c_str(), next_header_index);
                        // since we couldn't find a footer it's likely that the footer was corrupted
                        // so I pull off any footer bytes in the hopes that the pkt will be okay
                        char bb = *current_pkt.end();
                        while(bb == (char)0xFF || bb == (char)0xFE){
                            current_pkt.pop_back();
                            bb = *current_pkt.end();
                        }
                        verify_crc(current_pkt);
                    }
                } else if(next_header_index == string::npos){
                    //cout << "partial pkt appending..." << endl;                             // debug
                    //print_hex((byte*)current_pkt.c_str(),current_pkt.size());               // debug
                    current_pkt += in_data;
                    partial_pkt = true;
                } else{
                    //cout << "DROPPED PACKET partial pkt bad footer not found"<< endl;     // debug
                    //print_hex((byte*)current_pkt.c_str(),current_pkt.size());             // debug
                    current_pkt += string((char *)in_data.c_str(), next_header_index);
                    // since we couldn't find a footer it's likely that the footer was corrupted
                    // so I pull off any footer bytes in the hopes that the pkt will be okay
                    char bb = *current_pkt.end();
                    while(bb == (char)0xFF || bb == (char)0xFE){
                        current_pkt.pop_back();
                        bb = *current_pkt.end();
                    }
                    verify_crc(current_pkt);
                }
                if(next_header_index != string::npos){
                    search_from = next_header_index;
                }
            }

            size_t num_headers = count(in_data, header);
            //cout << "num_headers : " << num_headers << endl;

            for(size_t i = 0; i < num_headers; i++){
                //cout << "for loop " << i << endl;   // debug
                size_t header_index = in_data.find(header,search_from);
                size_t footer_index = in_data.find(footer,header_index);
                size_t next_header_index = in_data.find(header,header_index+4);

                if(footer_index != string::npos){
                    if(next_header_index == string::npos || footer_index < next_header_index){
                        //cout << "for loop framed: " << endl; // debug
                        //print_hex((byte*)(in_data.c_str() + header_index + 4), footer_index - header_index - 4);    // debug
                        current_pkt = string(in_data.c_str() + header_index + 4, footer_index - header_index - 4);
                        verify_crc(current_pkt);
                    } else{
                        current_pkt = string((char *)(in_data.c_str() + header_index + 4), next_header_index - header_index - 4);
                        // since we couldn't find a footer it's likely that the footer was corrupted
                        // so I pull off any footer bytes in the hopes that the pkt will be okay
                        char bb = *current_pkt.end();
                        while(bb == (char)0xFF || bb == (char)0xFE){
                            current_pkt.pop_back();
                            bb = *current_pkt.end();
                        }

                        verify_crc(current_pkt);
                    }
                } else if(next_header_index == string::npos){
                    //cout << "for loop start partial: " << endl; // debug
                    //print_hex((byte*)(in_data.c_str() + header_index + 4),in_data.size() - header_index - 4);
                    current_pkt = string((char *)(in_data.c_str() + header_index + 4),in_data.size() - header_index - 4);
                    partial_pkt = true;
                } else{
                    //cout << "MISFRAMED PACKET!" << endl;
                    current_pkt = string((char *)(in_data.c_str() + header_index + 4), next_header_index - header_index - 4);
                    // since we couldn't find a footer it's likely that the footer was corrupted
                    // so I pull off any footer bytes in the hopes that the pkt will be okay
                    char bb = *current_pkt.end();
                    while(bb == (char)0xFF || bb == (char)0xFE){
                        current_pkt.pop_back();
                        bb = *current_pkt.end();
                    }

                    verify_crc(current_pkt);
                }
                if(next_header_index != string::npos)
                    search_from = next_header_index;
            }
            if(end_head_bytes)
                in_data = string(header,end_head_bytes);
            else
                in_data.clear();
            select_ret = call_select(m_rfd, 2, 0);
        }
    }
    //cout << "exit end of read " << endl;    // debug
}/*}}}*/

void RadioManager::verify_crc(string data){/*{{{*/
    static CRC32 crc;
    //static vector<Packet> ack_resend_wait;

    crc.reset();
    crc.add(data.c_str(),data.size() - 4);
    byte h[4];
    crc.getHash(h);
    string hash((char *)h,4);

    if(hash == string(data.c_str() + data.size() - 4, 4)){
        cout << "verified" << endl;
        m_ack_count++;
        /*
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
                wake_write_loop();
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
                wake_write_loop();
            }
        }
    } else if(!to_ack.empty()){
        request_ack_resend();

        to_ack_mtx.lock();
        ack_resend_wait = to_ack;
        to_ack.clear();
        to_ack_mtx.unlock();
    */
    } else{
        m_bad_crc++;
        cout << "not verified" << endl;
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

    wake_write_loop();
}/*}}}*/

bool RadioManager::send_in_progress()/*{{{*/
{
    return !(to_send.empty() && to_resend.empty() && send_window.empty());
}/*}}}*/
