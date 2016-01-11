#include "radiomanager.h"

RadioManager::RadioManager(): HEADER(0xFAebDCcd), /*FOOTER(0xABcdEF00),*/ HEADER_SIZE(sizeof(long))
{
    m_ttyPortName = "/dev/tty";
    m_ttyPortName += DEFAULT_TTY_PORT_NAME;
}

RadioManager::~RadioManager(){
    closeSerial();
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
    if(tcsetattr(m_fd,TCSAFLUSH, &m_oldConfig) < 0){
        exitCode |= CONFIG_APPLY_FAIL;
    }
    if(close(m_fd) < 0){
        exitCode |= CLOSE_FAIL;
    }

    return exitCode;
}

int RadioManager::send(byte * data, const ulong numBytes)
{
    int numSent = 0;
    m_crc.reset();
    m_crc.add(data,numBytes);
    byte hash[m_crc.HashBytes];
    m_crc.getHash(hash);
    //cout << m_crc.getHash() << endl;
    numSent += write(m_fd, &HEADER, HEADER_SIZE);
    numSent += write(m_fd, hash, m_crc.HashBytes);
    numSent += write(m_fd, data, numBytes);
    cout << "fsync: " << fsync(m_fd) << endl;
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

