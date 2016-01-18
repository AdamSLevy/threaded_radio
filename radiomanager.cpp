#include "radiomanager.h"

class write_err: public exception/*{{{*/
{
	virtual const char * what() const throw()
	{
		return "A write error occurred.";
	}
} write_err;/*}}}*/

RadioManager::RadioManager(): /*HEADER(0xFAffFFfa),*/ FOOTER(0xFeffFFfe)/*{{{*/
{
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
        m_fd = -1;
        //write_th.join();
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
    if(msgID > 0xFB)
        msgID = 0;

    size_t sizeDataCompressed = (numBytes * 1.1) + 12;
    byte dataCompressed[sizeDataCompressed];
    int z_result = compress( dataCompressed, &sizeDataCompressed, data, numBytes);
    /*}}}*/
    if(Z_OK == z_result){
        // set up 
        size_t numPkts = sizeDataCompressed / PKT_DATA_SIZE + 1;
        size_t numTotalBytesForPkts = HEAD_PKT_SIZE + numPkts * MAX_PKT_SIZE - PKT_DATA_SIZE
                                     + sizeDataCompressed - (numPkts - 1)*PKT_DATA_SIZE;
        
        size_t bytesRemaining = sizeDataCompressed;
        byte pktID = 0;
        Packet * pkt_to_send = new Packet[numPkts+1];

        // create HEAD PKT/*{{{*/
        byte * pkt_data = pkt_to_send[0].data;

        // ID
        pkt_data[ID_OFFSET]=msgID;
        pkt_data[ID_OFFSET+1]=pktID;

        // DATA: number of packets and compressed data crc
        pkt_data[PKT_DATA_OFFSET]=(byte)numPkts;
        m_crc.reset();
        m_crc.add(dataCompressed,sizeDataCompressed);
        m_crc.getHash(pkt_data+PKT_DATA_OFFSET+1);

        // PKT CRC
        m_crc.reset();
        m_crc.add(pkt_data+ID_OFFSET,ID_SIZE + 1 + CRC_SIZE);
        m_crc.getHash(pkt_data+CRC_OFFSET(1+CRC_SIZE));

        pkt_to_send[pktID].len = HEAD_PKT_SIZE;
        /*}}}*/

        // create remaining data filled packets /*{{{*/ 
        for(pktID = 1; pktID < numPkts+1; pktID++){
            int data_len = PKT_DATA_SIZE;
            if(bytesRemaining < PKT_DATA_SIZE)
                data_len = bytesRemaining;
            
            bytesRemaining -= data_len;

            pkt_data = pkt_to_send[pktID].data;
            byte pkt_len = MAX_PKT_SIZE - PKT_DATA_SIZE + data_len;
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
        shared_mem_mtx.lock();      // MUTEX LOCK
        for(pktID = 0; pktID < numPkts+1; pktID++){
            to_send.push_back(pkt_to_send[pktID]);
        }
        shared_mem_mtx.unlock();    // MUTEX UNLOCK

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
    byte * window_buf = (byte *) calloc(MAX_PKTS_WRITE_LOOP*MAX_PKT_SIZE,sizeof(byte));
    unique_lock<mutex> lck(write_cv_mtx);
    while(is_open && (!end_thread || !send_window.empty() || !to_send.empty())){
        // add packets to send_window, until full or to_send is empty
        int num_pkts_added = 0;
        while(send_window.size() < MAX_PKTS_WRITE_LOOP && num_pkts_added < to_send.size()){
            Packet pkt = to_send[num_pkts_added++];
            send_window.push_back(pkt);
        }

        shared_mem_mtx.lock();  // MUTEX LOCK
        // from to_send, remove the packets that were added to send_window
        to_send.erase(to_send.begin(),to_send.begin()+num_pkts_added);
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
        shared_mem_mtx.unlock(); // MUTEX UNLOCK

        // write the data
        size_t num_bytes_to_send = window_ptr - window_buf;
        cout << " num bytes to send: " << num_bytes_to_send << endl;
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
                cout << err.what() << endl;
                is_open = false;
                free(window_buf);
                return;
            }
            num_bytes_to_send -= num_bytes_sent;
            bytes_sent += num_bytes_sent;
            window_ptr += num_bytes_sent;
            cout << "num_bytes_sent: " << num_bytes_sent << " total: " << bytes_sent << endl;

            if(tcdrain(m_fd) != 0){
                int errsv = errno;
                cout << "tcdrain: " << errsv << endl;
                char buf[40];
                strerror_r(errsv,buf,40);
                cout << buf << endl;
            }
            if(tcflush(m_fd, TCIOFLUSH) != 0 ){
                cout << "flush issue" << endl;
            }
            size_t send_time = 1e6 * num_bytes_sent / (115200/9)+100000;
            cout << "sleep_time " << send_time << endl;
            usleep(send_time);
        }


        // get ACK

        // parse ACK
        
        // populate send_window
        send_window.clear();

        // if nothing to send, wait until notify
        while(to_send.empty() && send_window.empty() && !end_thread && is_open) 
            write_cv.wait(lck);
    }
    //cout << "exiting the thread, free window buf" << endl;
    free(window_buf);
}/*}}}*/
