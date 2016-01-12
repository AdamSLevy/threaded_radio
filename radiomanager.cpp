#include "radiomanager.h"

class msg_too_big: public exception
{
	virtual const char * what() const throw()
	{
		return "The data provided was too big.";
	}
} msg_too_big;

RadioManager::RadioManager(): HEADER(0xFAffFFfa), FOOTER(0xFeffFFfe)
{
    m_ttyPortName = "/dev/tty";
    m_ttyPortName += DEFAULT_TTY_PORT_NAME;

    msg_buf = (byte *) calloc(MSG_BUF_SIZE,sizeof(byte));
    shared_buf = (byte *) calloc(SHARED_BUF_SIZE,sizeof(byte));

    end_thread = false;
    m_fd = -1;

    write_th = thread(&RadioManager::write_loop,this);
    write_th.detach();
}

RadioManager::~RadioManager(){
    write_cv_mtx.lock();
    write_cv.notify_one();
    end_thread = true;
    write_cv_mtx.unlock();
    closeSerial();

    free(msg_buf);
    free(shared_buf);
}

int RadioManager::setUpSerial()
{
    // open the port
    m_fd = open(m_ttyPortName.c_str(), O_RDWR | O_NOCTTY );

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
    return OPEN_SUCCESS;
}

int RadioManager::closeSerial()
{
    //fsync(m_fd);
    // revert to old config settings
    int exitCode = 0;
    if(m_fd != -1){
        if(tcsetattr(m_fd,TCSAFLUSH, &m_oldConfig) < 0){
            exitCode |= CONFIG_APPLY_FAIL;
        }
        if(close(m_fd) < 0){
            exitCode |= CLOSE_FAIL;
        }
    }

    return exitCode;
}

#include <iomanip>
void print_hex(byte * data, size_t len)
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
}

void print_hex(byte bb)
{
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
}

int RadioManager::send(byte * data, const ulong numBytes)
{
    static byte msgID = 0;
    static byte * write_ptr = shared_buf;
    static byte * head_ptr = (byte*)&HEADER;
    static byte * foot_ptr = (byte*)&FOOTER;
    size_t sizeDataCompressed = (numBytes * 1.1) + 12;
    byte dataCompressed[sizeDataCompressed];
    int z_result = compress( dataCompressed, &sizeDataCompressed, data, numBytes);
    int numSent = -1;
    if(Z_OK == z_result){
        size_t numPkts = sizeDataCompressed / PKT_DATA_SIZE + 1;
        size_t numTotalBytesForPkts = HEAD_PKT_SIZE + numPkts * MAX_PKT_SIZE - PKT_DATA_SIZE
                                     + sizeDataCompressed - (numPkts - 1)*PKT_DATA_SIZE;
        if(numTotalBytesForPkts > MSG_BUF_SIZE)
            throw msg_too_big;
        size_t bytesRemaining = sizeDataCompressed;
        byte pktID = 0;
        byte * msg_ptr = msg_buf;
        Packet * pkt_to_send = new Packet[numPkts+1];

        size_t remBytesInSharedBuf = SHARED_BUF_SIZE - (write_ptr - shared_buf);

        // reset write_ptr if there isn't enough room for the pkts
        if(numTotalBytesForPkts > remBytesInSharedBuf){
            cout << "write_ptr_loop" << endl;
            write_ptr = shared_buf;
        }

        // HEAD PKT
        // HEADER
        for(int i = 0; i < HEADER_SIZE; i++)
            msg_ptr[i]=head_ptr[i];

        // ID
        msg_ptr[ID_OFFSET]=msgID;
        msg_ptr[ID_OFFSET+1]=pktID;

        // DATA: number of packets and compressed data crc
        msg_ptr[PKT_DATA_OFFSET]=(byte)numPkts;
        m_crc.reset();
        m_crc.add(dataCompressed,sizeDataCompressed);
        m_crc.getHash(msg_ptr+PKT_DATA_OFFSET+1);

        // PKT CRC
        m_crc.reset();
        m_crc.add(msg_ptr+ID_OFFSET,ID_SIZE + 1 + CRC_SIZE);
        m_crc.getHash(msg_ptr+CRC_OFFSET(1+CRC_SIZE));

        // FOOTER
        for(int i = 0; i < FOOTER_SIZE; i++)
            msg_ptr[FOOTER_OFFSET(1+CRC_SIZE) + i ]=foot_ptr[i];

        //cout << m_crc.getHash() << endl;
        //print_hex(msg_ptr, HEAD_PKT_SIZE);
        msg_ptr += HEAD_PKT_SIZE;

        pkt_to_send[pktID].msg = msgID;
        pkt_to_send[pktID].pkt = pktID;
        pkt_to_send[pktID].len = HEAD_PKT_SIZE;
        pkt_to_send[pktID].ptr = write_ptr;


        for(pktID = 1; pktID < numPkts+1; pktID++){
            int len = PKT_DATA_SIZE;
            if(bytesRemaining < PKT_DATA_SIZE){
                len = bytesRemaining;
                cout << "bytesRemaining < pkt_da " << len << endl;
                print_hex(pktID);
            }
            bytesRemaining -= len;

            // HEADER
            for(int i = 0; i < HEADER_SIZE; i++)
                msg_ptr[i]=head_ptr[i];

            // ID
            msg_ptr[ID_OFFSET]=msgID;
            msg_ptr[ID_OFFSET+1]=pktID;

            // DATA
            memcpy(msg_ptr+PKT_DATA_OFFSET,dataCompressed+(pktID-1)*PKT_DATA_SIZE, len);

            // PKT CRC
            m_crc.reset();
            m_crc.add(msg_ptr+ID_OFFSET,len+ID_SIZE);
            m_crc.getHash(msg_ptr+CRC_OFFSET(len));

            // FOOTER
            for(int i = 0; i < FOOTER_SIZE; i++)
                msg_ptr[FOOTER_OFFSET(len) + i]=foot_ptr[i];


            pkt_to_send[pktID].msg = msgID;
            pkt_to_send[pktID].pkt = pktID;
            pkt_to_send[pktID].len = MAX_PKT_SIZE - PKT_DATA_SIZE + len;
            pkt_to_send[pktID].ptr = msg_ptr;
            //cout << m_crc.getHash() << endl;
            //print_hex(msg_ptr, MAX_PKT_SIZE - PKT_DATA_SIZE + len);
            msg_ptr += MAX_PKT_SIZE;
        }
        msgID++;
        if(msgID > 0xFB)
            msgID = 0;

        // copy pkts to shared mem
        shared_mem_mtx.lock();
        memcpy(write_ptr, msg_buf, numTotalBytesForPkts);
        for(pktID = 0; pktID < numPkts+1; pktID++){
            send_q.push_back(pkt_to_send[pktID]);

            cout << "Msg: ";
            print_hex(pkt_to_send[pktID].msg);
            cout << " Packet: ";
            print_hex(pkt_to_send[pktID].pkt);
            cout << " len: ";
            print_hex(pkt_to_send[pktID].len);
            cout << endl;
        }

        //if(write_cv_mtx.try_lock()){
        //    write_cv.notify_one();
        //    write_cv_mtx.unlock();
        //}

        shared_mem_mtx.unlock();

        //print_hex(write_ptr, numTotalBytesForPkts);
        write_ptr += numTotalBytesForPkts;
        delete [] pkt_to_send;

        numSent = numTotalBytesForPkts;
    }
    return numSent;
}

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
}

void RadioManager::write_loop()
{
    deque<Packet> out_q;
    byte * out_buf = (byte *) calloc(MAX_PKTS_WRITE_LOOP*MAX_PKT_SIZE,sizeof(byte));

    unique_lock<mutex> lck(write_cv_mtx);
    while(true){
        while(send_q.empty() && out_q.empty()){
            /*
            cout << "before write_cv.wait" << endl;
            sleep(1);
            //cout << "try lock... ";
            //if(lck.try_lock()){
            //    cout << "wasn't locked, now is" << endl;
            //} else{
            //    cout << "is locked " << endl;
            //}
            write_cv.wait(lck);
            if(end_thread){
                cout << "goodbye" << endl;
                free(out_buf);
                return;
            }
            */
            //cout << "empty..." << endl;
            sleep(4);
        }

        int num_pkts_added = 0;
        //cout << "qing Packet's" << endl;
        while(out_q.size() < MAX_PKTS_WRITE_LOOP && !send_q.empty()){
            Packet pkt = send_q[num_pkts_added++];
            out_q.push_back(pkt);
        }
        byte * out_ptr = out_buf;

        //cout << "acquiring mutex... ";
        shared_mem_mtx.lock();  // MUTEX LOCK
        cout << " mutex acquired" << endl;

        cout << "erase send_q... ";
        send_q.erase(send_q.begin(),send_q.begin()+num_pkts_added);
        cout << " done" << endl;

        cout << "memcpy pkts... " << endl;
        for(int p = 0; p < out_q.size(); p++){
            cout << p << " ";
            Packet pkt = out_q[p];
            if(pkt.ptr[ID_OFFSET] == pkt.msg && pkt.ptr[ID_OFFSET+1] == pkt.pkt && // verify memory is still good
                    pkt.ptr[0] == ((byte*)&HEADER)[0] && pkt.ptr[1] == ((byte*)&HEADER)[1] && 
                    pkt.ptr[2] == ((byte*)&HEADER)[2] && pkt.ptr[1] == ((byte*)&HEADER)[2]){

                cout << "Msg: ";
                print_hex(pkt.msg);
                cout << " Packet: ";
                print_hex(pkt.pkt);
                cout << " len: ";
                print_hex(pkt.len);
                cout << endl;

                memcpy(out_ptr,pkt.ptr,pkt.len);
                out_ptr += pkt.len;
                out_q[p].send_count--;
            } else{
                out_q[p].send_count=0;
            }
        void* ptr = &out_ptr;
        print_hex((byte*)ptr,sizeof(out_ptr));
        ptr = &out_buf;
        print_hex((byte*)ptr,sizeof(out_buf));
        }
        cout << " done" << endl;

        print_hex(out_buf, out_ptr - out_buf);
        shared_mem_mtx.unlock(); // MUTEX UNLOCK
        cout << "mutex released" << endl;

        // write the data
        cout << (out_ptr - out_buf) << endl;
        print_hex(out_buf, out_ptr - out_buf);
        out_q.clear();
    }
}
