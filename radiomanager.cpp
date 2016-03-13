#include "radiomanager.hpp"

// V************* debug functions *********************************************V/*{{{*/
void print_hex(byte * data, size_t len)/*{{{*/
{
    size_t perLine = 20;
    cout << "   0:  ";
    for (size_t i = 0; i < len; i++){
        byte bb = data[i];
        byte hb = (bb&0xF0)>>4;
        byte lb = bb&0x0F;
        char hc;
        char lc;
        if (hb>0x09){
            hc = 'A' + (hb-0x0A);
        } else{
            hc = '0' + hb;
        }
        if (lb>0x09){
            lc = 'A' + (lb-0x0A);
        } else{
            lc = '0' + lb;
        }
        cout << hc << lc << " ";
        if (i%perLine == perLine - 1 && i!=len-1 ){
            cout << endl;
            cout << setw(4) << i << ":  ";
        }
    }
    cout << endl << endl;
}/*}}}*/

string to_hex(byte bb)/*{{{*/
{
    byte hb = (bb&0xF0)>>4;
    byte lb = bb&0x0F;
    string out = "00";
    if (hb>0x09){
        out[0] = 'A' + (hb-0x0A);
    } else{
        out[0] = '0' + hb;
    }
    if (lb>0x09){
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
    if (fd < 0){
        return -1;
    }
    fd_set rfds;            // stores which fd's should be watched for bytes available to read
    struct timespec tv;      // stores timeout for select
    
    FD_ZERO(&rfds);         // clear fd's
    FD_SET(fd, &rfds);    // add fd to rfds

    // set delay
    tv.tv_sec = delay_sec;
    tv.tv_nsec = delay_nsec;

    int ret = pselect(fd+1, &rfds, nullptr, nullptr, &tv, nullptr);   // will return on bytes available or timeout

    /*  // debug 
    if (ret < 0){
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
    if (to_search.size() < to_count.size()){
        return 0;
    }
    for (size_t i = 0; i < to_search.size() - to_count.size(); i++){
        if (to_count == string(to_search.c_str()+i, to_count.size())){
            count++;
        }
    }
    return count;
}/*}}}*/

size_t find_partial_end( const string & to_search, const string & to_find )/*{{{*/
{
    size_t max_offset = std::min(to_search.size(),to_find.size());
    for (int offset = max_offset - 1; offset >= 0; offset--){
        bool depth_partial = true;
        for (int place = offset; place >= 0; place--){
            if (to_search[to_search.size() - 1 - offset + place] != to_find[0 + place]){
                depth_partial = false;
                break;
            }
        }
        if (depth_partial){
            return offset + 1;
        }
    }
    return 0;
}/*}}}*/
// ^************** end helper NON RadioManager functions ***********************************^/*}}}*/

// V************** RadioManager member functions *******************************************V
RadioManager::RadioManager(): HEADER(HEADER_HEX),FOOTER(FOOTER_HEX)/*{{{*/
{
    m_port_name = DEFAULT_TTY_PORT_NAME;

    is_open = false;
    m_wfd = -1;
    m_rfd = -1;
    num_pkts = 0;       // debug
    m_num_resent = 0;
    m_num_sent = 0;

}/*}}}*/

RadioManager::~RadioManager()/*{{{*/
{
    close_serial();
    cout << " num pkts: " << num_pkts << endl;  // debug
    cout << " num acks received: " << m_ack_count << endl;  // debug
    cout << " num bad crc : " << m_bad_crc << endl; // debug
    cout << " num successfully sent : " << m_num_sent << endl; // debug
    cout << " num resent : " << m_num_resent << endl; // debug
}/*}}}*/

int RadioManager::open_serial( string port_name )/*{{{*/
{
    m_port_name = port_name;
    return open_serial();
}/*}}}*/

int RadioManager::open_serial()/*{{{*/
{
    // Can't open an already open port
    // Call close_serial() first
    if (is_open){
        return IS_ALREADY_OPEN;
    }

    // Open the port
    m_wfd = open(m_port_name.c_str(), O_WRONLY | O_NOCTTY );
    m_rfd = open(m_port_name.c_str(), O_RDONLY | O_NOCTTY );

    // Check for open success
    if (m_wfd < 0 || m_rfd < 0){
        return OPEN_FAIL;
    }

    // Check if valid serial port
    if (!isatty(m_wfd) || !isatty(m_rfd)){
        return NOT_A_TTY;
    }

    // Copy old config
    if (tcgetattr(m_wfd, &m_oldConfig) < 0){
        return GET_CONFIG_FAIL;
    }

    m_config = m_oldConfig;

    cfmakeraw(&m_config);

    m_config.c_cc[VMIN] = 0;
    m_config.c_cc[VTIME] = 5;

    // Set baud rate
    if (cfsetspeed(&m_config, B115200)){
        return BAUD_FAILED;
    }

    // Apply config to ports
    if (tcsetattr(m_wfd,TCSAFLUSH, &m_config) < 0){
        return CONFIG_APPLY_FAIL;
    }
    if (tcsetattr(m_rfd,TCSAFLUSH, &m_config) < 0){
        return CONFIG_APPLY_FAIL;
    }

    // Flush ports
    if (tcflush(m_wfd, TCIFLUSH) != 0 ){
        //std::cerr << "m_wfd flush issue" << endl;   // debug
    }
    if (tcflush(m_rfd, TCOFLUSH) != 0 ){
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

int RadioManager::close_serial()/*{{{*/
{
    unique_lock<mutex> rlck(is_reading_mtx);    // guarantees release on exit.
    unique_lock<mutex> wlck(is_writing_mtx);    // guarantees release on exit.

    //fsync(m_wfd);
    // revert to old config settings
    int exitCode = 0;
    if (m_rfd != -1){
        if (tcsetattr(m_rfd,TCSAFLUSH, &m_oldConfig) < 0){
            exitCode |= CONFIG_APPLY_FAIL;
        }
        if (close(m_rfd) < 0){
            exitCode |= CLOSE_FAIL;
        }
        m_rfd = -1;
    }
    if (m_wfd != -1){
        if (tcsetattr(m_wfd,TCSAFLUSH, &m_oldConfig) < 0){
            exitCode |= CONFIG_APPLY_FAIL;
        }
        if (close(m_wfd) < 0){
            exitCode |= CLOSE_FAIL;
        }
        m_wfd = -1;
    }

    is_open = false;
    clear_queued_data();
    wake_write_loop();

    //cout << "close exit: " << exitCode << endl; // debug

    return exitCode;
}/*}}}*/

void RadioManager::wake_write_loop()/*{{{*/
{
    if (write_cv_mtx.try_lock()){
        write_cv.notify_one();
        write_cv_mtx.unlock();
    }
}/*}}}*/

int RadioManager::queue_data(byte * data, const ulong num_bytes)/*{{{*/
{
    static byte msgID = 0;/*{{{*/
    const byte * foot_ptr = (byte *)(&FOOTER);
    CRC32 crc;
    
    int num_sent = -1;
    if (!is_open){
        return num_sent;
    }

    if (msgID == MAX_ID){
        msgID = 0;
    }

    size_t size_data_compressed = (num_bytes * 1.1) + 12;
    byte data_compressed[size_data_compressed];
    int z_result = compress( data_compressed, (uLongf *)&size_data_compressed, data, num_bytes);
    size_t num_pkts = size_data_compressed / PKT_DATA_SIZE + 1;
    /*}}}*/
    if (Z_OK == z_result && num_pkts < MAX_ID){
        // set up 
        if ((byte) num_pkts == MAX_ID){
            return -1;
        }

        size_t num_total_bytes_to_send = MSG_SIZE(size_data_compressed);
        
        size_t bytes_remaining = size_data_compressed;
        byte pktID = 0;
        Packet * pkt_to_send = new Packet[num_pkts+1];

        // create HEAD PKT/*{{{*/
        byte * pkt_data = pkt_to_send[0].data;

        // ID
        pkt_data[ID_OFFSET]=msgID;
        pkt_data[ID_OFFSET+1]=pktID;

        // DATA: number of packets and compressed data crc
        pkt_data[PKT_DATA_OFFSET] = (byte)num_pkts;

        crc.reset();
        crc.add(data_compressed,size_data_compressed);
        crc.getHash(pkt_data+PKT_DATA_OFFSET+1);

        // PKT CRC
        crc.reset();
        crc.add(pkt_data+ID_OFFSET,ID_SIZE + HEAD_PKT_DATA_SIZE);
        crc.getHash(pkt_data+CRC_OFFSET(HEAD_PKT_DATA_SIZE));

        pkt_to_send[pktID].len = HEAD_PKT_SIZE;
        /*}}}*/

        // create remaining data filled packets /*{{{*/ 
        for (pktID = 1; pktID < num_pkts+1; pktID++){
            size_t data_len = PKT_DATA_SIZE;
            if (bytes_remaining < PKT_DATA_SIZE){
                data_len = bytes_remaining;
            }
            bytes_remaining -= data_len;

            pkt_data = pkt_to_send[pktID].data;
            size_t pkt_len = PKT_SIZE(data_len);
            pkt_to_send[pktID].len = pkt_len;

            // ID
            pkt_data[ID_OFFSET]=msgID;
            pkt_data[ID_OFFSET+1]=pktID;

            // DATA
            memcpy(pkt_data+PKT_DATA_OFFSET,data_compressed+(pktID-1)*PKT_DATA_SIZE, data_len);

            // PKT CRC
            crc.reset();
            crc.add(pkt_data+ID_OFFSET,data_len+ID_SIZE);
            crc.getHash(pkt_data+CRC_OFFSET(data_len));

            // FOOTER
            if (data_len < PKT_DATA_SIZE){
                for (int i = 0; i < FOOTER_SIZE; i++){
                    pkt_data[FOOTER_OFFSET(data_len) + i]=foot_ptr[i];
                }
            }
        }
        /*}}}*/

        // copy pkts to shared mem
        to_send_mtx.lock();
        for (pktID = 0; pktID < num_pkts+1; pktID++){
            to_send.push_back(pkt_to_send[pktID]);
        }
        to_send_mtx.unlock();

        wake_write_loop();

        delete [] pkt_to_send;
        num_sent = num_total_bytes_to_send;
        msgID++;
    }
    return num_sent;
}/*}}}*/

void RadioManager::write_loop()/*{{{*/
{
    byte window_buf[WINDOW_BUF_SIZE];
    unique_lock<mutex> lck(write_cv_mtx);
    while ( is_open ){
        // If there is nothing to send, wait until notified
        while (is_open && to_send.empty() && to_resend.empty() && send_window.empty() && to_ack_ack.empty()){
            write_cv.wait(lck);
        }

        if (!is_open){      // If the port has been marked as closed, end the thread
            return;
        }

        // Populate send_window
        // send_window will be filled with packets until
        //      it has NUM_PKTS_PER_ACK or to_send and to_resend are empty
        
        // ACK ACKS
        // Add acknowledgements of acknowledgements first
        // Do not count toward total packet count in send window
        size_t ack_ack_pkts_added = 0;
        to_ack_ack_mtx.lock();
        while(ack_ack_pkts_added < to_ack_ack.size()){
            send_window.push_back(to_ack_ack[ack_ack_pkts_added++]);
        }
        to_ack_ack.clear();
        to_ack_ack_mtx.unlock();

        // RESEND packets get priority
        // Add to_resend Packets to send_window
        size_t resend_pkts_added = 0;
        to_resend_mtx.lock();
        while ((send_window.size() < (NUM_PKTS_PER_ACK + ack_ack_pkts_added))
                && (resend_pkts_added < to_resend.size())){
            //MsgPktID id;
            //id.msg_id = to_resend[resend_pkts_added].data[ID_OFFSET];      // debug
            //id.pkt_id = to_resend[resend_pkts_added].data[ID_OFFSET+1];    // debug
            //cout << "\t\tresending msg: " << to_hex(id.msg_id) << " pkt: " << to_hex(id.pkt_id) << endl; // debug
            send_window.push_back(to_resend[resend_pkts_added++]);
        }
        if (resend_pkts_added){
            m_num_resent += resend_pkts_added;                          //debug
            to_resend.erase(to_resend.begin(),to_resend.begin()+resend_pkts_added);
        }
        //cout << "\t\t resend_pkts_added: " << resend_pkts_added << endl;            // debug
        //cout << "\t\t to_resend size: " << to_resend.size() << endl;                // debug
        //cout << "\t\t to_send size: " << to_send.size() << endl << endl << endl;    // debug
        to_resend_mtx.unlock();

        // NEW DATA PKTS
        // Add to_send Packets to send_window
        size_t send_pkts_added = 0; 
        to_send_mtx.lock();
        while ((send_window.size() < (NUM_PKTS_PER_ACK + ack_ack_pkts_added))
                && (send_pkts_added < to_send.size())){
            send_window.push_back(to_send[send_pkts_added++]);
        }
        if (send_pkts_added){
            to_send.erase( to_send.begin(),to_send.begin() + send_pkts_added );
        }
        to_send_mtx.unlock();

        // Add send_window Packets to to_ack, so they will await acknowledgement
        // Exclude leading ack ack packets
        to_ack_mtx.lock();
        for ( size_t i = ack_ack_pkts_added; i < send_window.size(); i++){
            to_ack.push_back(send_window[i]);
        }
        to_ack_mtx.unlock();


        // Copy the Packet::data to the continuous window_buf
        byte * window_ptr = window_buf;
        for (auto & pkt_iter : send_window){
            if(window_ptr + pkt_iter.len < window_buf + WINDOW_BUF_SIZE){
                pkt_iter.send_rem--;
                memcpy(window_ptr,pkt_iter.data,pkt_iter.len);
                //print_pkt(*pkt_iter); // debug
                num_pkts++;
                window_ptr += pkt_iter.len;
            } else{
                cerr << "write_loop, window_buf, OVERFLOW PREVENTED" << endl;
                PRINT_DEBUG;
                break;
            }
        }
        //print_hex(window_buf, window_ptr - window_buf);       // debug

        // WRITE LOOP
        size_t bytes_remaining = window_ptr - window_buf;
        window_ptr = window_buf;
        while (bytes_remaining > 0){
            size_t bytes_per_write = min(bytes_remaining, (size_t)MAX_BYTES_PER_WRITE);     // limit bytes per write call

            // write call
            is_writing_mtx.lock();
            int num_bytes_sent = write(m_wfd, window_ptr, bytes_per_write);
            is_writing_mtx.unlock();

            if (num_bytes_sent < 0){                     // indicates write error, set_is open to false, end the thread
                is_open = false;
                return;
            }

            bytes_remaining -= num_bytes_sent;
            window_ptr      += num_bytes_sent;
            //cout << "num_bytes_sent: " << num_bytes_sent << " total: " << bytes_sent << endl;

            if (tcdrain(m_wfd) != 0){
                int errsv = errno;
                //cout << "tcdrain: " << errsv << endl;     // debug
                char buf[40];
                strerror_r(errsv,buf,40);
                PRINT_DEBUG;
                //cout << buf << endl;          // debug
            }

            // debug this timeout
            size_t send_time = 1e6 * num_bytes_sent / (115200/9);//+100000;
            //cout << "sleep_time " << send_time << endl;   // debug
            usleep(send_time);
        }
        send_window.clear();
    }
}/*}}}*/

void RadioManager::read_loop()/*{{{*/
{
    byte read_buf[READ_BUF_SIZE];   // used for read() call
    bool partial_pkt = false;       // denotes whether current_pkt is awaiting completion
    string current_pkt;             // holds the packet data starting at the ID, excluding header and footer
    string in_data;                 // holds the latest data from read_buf, allows prefacing with broken headers
    const string header((char *)(&HEADER),4);     // header bytes
    const string footer((char *)(&FOOTER),4);     // footer bytes
    m_ack_count = 0;
    m_bad_crc = 0;

    while (is_open){
        // while there are bytes to read...
        while (call_read_select() > 0){
            int bytes_read = 0;

            // read call
            is_reading_mtx.lock();
            bytes_read = read(m_rfd, read_buf, READ_BUF_SIZE);
            is_reading_mtx.unlock();

            // if error, exit the thread
            if (bytes_read < 0){
                is_open = false;
                return;
            }

            if (bytes_read == 0){
                continue;
            }
            //cout << "bytes_read: " << bytes_read << endl;   // debug

            //cout << "read_buf:" << endl;    // debug
            //print_hex(read_buf, bytes_read); // debug

            in_data += string((char *)read_buf, bytes_read);

            //cout << "in_data: " << endl; // debug
            //print_hex((byte*)in_data.c_str(), in_data.size());  // debug

            if (bytes_read <= 4){
                int n_end_foot = find_partial_end(footer, in_data);
                int n_end_pkt = find_partial_end(current_pkt, footer);
                if (partial_pkt && n_end_foot + n_end_pkt == 4){
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
            if (partial_pkt){
                partial_pkt = false;
                //cout << "has partial pkt" << endl; // debug
                size_t footer_index = in_data.find(footer);
                size_t next_header_index = in_data.find(header,0);

                if (footer_index != string::npos){
                    if (next_header_index == string::npos || footer_index < next_header_index){
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
                        size_t n = 0;
                        while ((bb == (char)0xFF || bb == (char)0xFE) && current_pkt.size() > 0 && n < 4){
                            current_pkt.pop_back();
                            bb = *current_pkt.end();
                            n++;
                        }
                        verify_crc(current_pkt);
                    }
                } else if (next_header_index == string::npos){
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
                    size_t n = 0;
                    while ((bb == (char)0xFF || bb == (char)0xFE) && current_pkt.size() > 0 && n < 4){
                        current_pkt.pop_back();
                        bb = *current_pkt.end();
                        n++;
                    }
                    verify_crc(current_pkt);
                }
                if (next_header_index != string::npos){
                    search_from = next_header_index;
                }
            }

            size_t num_headers = count(in_data, header);
            //cout << "num_headers : " << num_headers << endl;

            for (size_t i = 0; i < num_headers; i++){
                //cout << "for loop " << i << endl;   // debug
                size_t header_index = in_data.find(header,search_from);
                size_t footer_index = in_data.find(footer,header_index);
                size_t next_header_index = in_data.find(header,header_index+4);

                if (footer_index != string::npos){
                    if (next_header_index == string::npos || footer_index < next_header_index){
                        //cout << "for loop framed: " << endl; // debug
                        //print_hex((byte*)(in_data.c_str() + header_index + 4), footer_index - header_index - 4);    // debug
                        current_pkt = string(in_data.c_str() + header_index + 4, footer_index - header_index - 4);
                        verify_crc(current_pkt);
                    } else{
                        current_pkt = string((char *)(in_data.c_str() + header_index + 4), next_header_index - header_index - 4);
                        // since we couldn't find a footer it's likely that the footer was corrupted
                        // so I pull off any footer bytes in the hopes that the pkt will be okay
                        char bb = *current_pkt.end();
                        size_t n = 0;
                        while ((bb == (char)0xFF || bb == (char)0xFE) && current_pkt.size() > 0 && n < 4){
                            current_pkt.pop_back();
                            bb = *current_pkt.end();
                            n++;
                        }
                        verify_crc(current_pkt);
                    }
                } else if (next_header_index == string::npos){
                    //cout << "for loop start partial: " << endl; // debug
                    //print_hex((byte*)(in_data.c_str() + header_index + 4),in_data.size() - header_index - 4);
                    current_pkt = string((char *)(in_data.c_str() + header_index + 4),in_data.size() - header_index - 4);
                    if (current_pkt.size() > 0)
                        partial_pkt = true;
                } else{
                    //cout << "MISFRAMED PACKET!" << endl;
                    current_pkt = string((char *)(in_data.c_str() + header_index + 4), next_header_index - header_index - 4);
                    // since we couldn't find a footer it's likely that the footer was corrupted
                    // so I pull off any footer bytes in the hopes that the pkt will be okay
                    char bb = *current_pkt.end();
                    size_t n = 0;
                    while ((bb == (char)0xFF || bb == (char)0xFE) && current_pkt.size() > 0 && n < 4){
                        current_pkt.pop_back();
                        bb = *current_pkt.end();
                        n++;
                    }
                    verify_crc(current_pkt);
                }
                if (next_header_index != string::npos){
                    search_from = next_header_index;
                }
            }
            if (end_head_bytes){
                in_data = string(header,end_head_bytes);
            } else{
                in_data.clear();
            }
        }
        if(to_ack.size() > 0){
            to_ack_mtx.lock();
            to_resend_mtx.lock();
            for( auto p : to_ack ){
                to_resend.push_back(p);
            }
            to_ack.clear();
            to_resend_mtx.unlock();
            to_ack_mtx.unlock();
            wake_write_loop();
        }
    }
    //cout << "exit end of read " << endl;    // debug
}/*}}}*/

int RadioManager::call_read_select()/*{{{*/
{
    if (m_rfd < 0 || !is_open){
        return -1;
    }

    fd_set rfds;            // stores which fd's should be watched for bytes available to read
    struct timespec tv;      // stores timeout for select
    
    FD_ZERO(&rfds);         // clear fd's
    FD_SET(m_rfd, &rfds);    // add fd to rfds

    // set delay
    tv.tv_sec =  SELECT_SEC_DELAY;
    tv.tv_nsec = SELECT_NSEC_DELAY;

    int ret = pselect(m_rfd+1, &rfds, nullptr, nullptr, &tv, nullptr);   // will return on bytes available or timeout

    return ret;
}/*}}}*/

void RadioManager::verify_crc(string data){/*{{{*/
    CRC32 crc;

    if (data.size() < 6){
        return;
    }

    string received_crc = string(data.c_str() + data.size() - 4, 4);
    data.resize(data.size() - 4);   // remove crc from data

    // compute crc
    crc.reset();
    crc.add(data.c_str(),data.size());
    byte h[4];
    crc.getHash(h);
    string computed_crc((char *)h,4);

    if (computed_crc == received_crc){
        //cout << "---- ack verified ------" << endl;     // debug
        m_ack_count++;                  // debug

        // parse data into list of acknowledged packets
        MsgPktID id;

        id.msg_id = (byte) data[0];
        id.pkt_id = (byte) data[1];

        ack_ack(id);    // acknowledge the acknowledgement packet

        byte current_msg_id = (byte) data[0];

        vector<MsgPktID> ack_id;    // to hold list of acknowledged msg pkt ids
        ack_id.push_back(id);
        for (size_t i = 2; i < data.size(); i++){
            byte bb = (byte) data[i];
            // change msg id
            if (bb == 0xFF){
                current_msg_id = (byte) data[i+1];
                i++;
                continue;
            }
            id.msg_id = current_msg_id;
            id.pkt_id = bb;
            ack_id.push_back(id);
        }

        vector<Packet> replace_to_ack;
        size_t acks_remaining = ack_id.size();
        to_ack_mtx.lock();
        bool resend_lock_owned = false;
        bool last_was_acked = false;
        for (auto pkt : to_ack){
            if (acks_remaining){
                id.msg_id = pkt.data[ID_OFFSET];
                id.pkt_id = pkt.data[ID_OFFSET+1];
                for (auto a_id : ack_id){
                    if (a_id.pkt_id == id.pkt_id
                        &&  a_id.msg_id == id.msg_id){
                        // pkt acknowledged
                        pkt.acked = true;   // mark for removal
                        acks_remaining--;
                        //cout << "acknowledged msg: " << to_hex(id.msg_id) << " pkt: " << to_hex(id.pkt_id) << endl; // debug
                    }
                }
                if (!pkt.acked){
                    if( last_was_acked || pkt.num_acks_passed == MAX_ACKS_AUTO_RESEND){
                        id.msg_id = pkt.data[ID_OFFSET];      // debug
                        id.pkt_id = pkt.data[ID_OFFSET+1];    // debug
                        //cout << "\t queued for resend msg: " << to_hex(id.msg_id)
                            //<< " pkt: " << to_hex(id.pkt_id) << " send_rem: " << pkt.send_rem 
                            //<< " num_acks_passed: " << pkt.num_acks_passed << endl; // debug

                        if (!resend_lock_owned){
                            to_resend_mtx.lock();
                            resend_lock_owned = true;
                        }
                        pkt.send_rem--;
                        pkt.num_acks_passed = 0;
                        to_resend.push_back(pkt);

                    } else{
                        pkt.num_acks_passed++;
                        replace_to_ack.push_back(pkt);
                        //cout << "awaiting ack msg: " << to_hex(id.msg_id) << " pkt: " << to_hex(id.pkt_id) << endl; // debug
                    }
                }
            } else{
                if (resend_lock_owned){
                    to_resend_mtx.unlock();
                    resend_lock_owned = false;
                    wake_write_loop();
                }
                if(!pkt.acked){
                    replace_to_ack.push_back(pkt);
                    id.msg_id = pkt.data[ID_OFFSET];
                    id.pkt_id = pkt.data[ID_OFFSET+1];
                    //cout << "awaiting ack msg: " << to_hex(id.msg_id) << " pkt: " << to_hex(id.pkt_id) << endl; // debug
                }
            }
            last_was_acked = pkt.acked;
        }

        m_num_sent += ack_id.size() - acks_remaining;

        if (resend_lock_owned){
            to_resend_mtx.unlock();
            wake_write_loop();
        }
        to_ack = replace_to_ack;

        //cout << endl; // debug

        to_ack_mtx.unlock();

        //cout << endl; // debug


    } else{
        m_bad_crc++;    // debug
        //cout << "not verified" << endl; // debug
    }
}/*}}}*/

void RadioManager::ack_ack(MsgPktID id){/*{{{*/
    Packet ack;
    ack.data[ID_OFFSET] = id.msg_id;
    ack.data[ID_OFFSET+1] = id.pkt_id;
    CRC32 crc;
    crc.reset();
    crc.add(ack.data + ID_OFFSET, 2);
    crc.getHash(ack.data+CRC_OFFSET(0));
    memcpy(ack.data + FOOTER_OFFSET(0), (byte*)(&FOOTER), 4);
    ack.len = PKT_SIZE(0);

    to_ack_ack_mtx.lock();
    to_ack_ack.push_back(ack);
    to_ack_ack_mtx.unlock();

    wake_write_loop();
}/*}}}*/

void RadioManager::clear_queued_data()/*{{{*/
{
      to_send_mtx.lock();
    to_resend_mtx.lock();
       to_ack_mtx.lock();
       to_ack_ack_mtx.lock();
      to_send.clear();
    to_resend.clear();
       to_ack.clear();
       to_ack_ack.clear();
      to_send_mtx.unlock();
    to_resend_mtx.unlock();
       to_ack_mtx.unlock();
       to_ack_ack_mtx.unlock();
}/*}}}*/

bool RadioManager::send_in_progress()/*{{{*/
{
    cout << "num to_send: " << to_send.size() << endl;
    cout << "num to_resend: " << to_resend.size() << endl;
    cout << "num send_window: " << send_window.size() << endl;
    cout << "num to_ack: " << to_ack.size() << endl;
    cout << "num to_ack_ack: " << to_ack_ack.size() << endl;
    return !(to_send.empty() && to_resend.empty() && send_window.empty() && to_ack_ack.empty() && to_ack.empty());
}/*}}}*/

size_t RadioManager::queue_size()/*{{{*/
{
    to_send_mtx.lock();
    to_resend_mtx.lock();
    size_t queue = to_send.size() + to_resend.size() + send_window.size();
    to_send_mtx.unlock();
    to_resend_mtx.unlock();
    return queue;
}/*}}}*/
