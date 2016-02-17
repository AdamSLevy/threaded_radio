#ifndef RADIODATA
#define RADIODATA

struct RadioData{
    // gps data
    float lat;
    float lon;
    float alt;
    float speed;
    float course;
    // none of these numbers will be bigger than 255
    unsigned char num_sats = 0;
    unsigned char quality = 0;
    unsigned char year = 0;
    unsigned char month = 0;
    unsigned char day = 0;
    unsigned char hour = 0;
    unsigned char minute = 0;
    unsigned char second = 0;
    unsigned char darkMode = 0;
    char warnCode = 'A';

    // spectrometer data
    unsigned long fileNum;
    short exposureTime;
    unsigned char numExposures;
    float spec[2048];
};

#endif // RADIODATA

