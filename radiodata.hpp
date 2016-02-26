#ifndef RADIODATA
#define RADIODATA

#define NUM_SPEC_CHANNELS 2048

#define MODE_COLLECT            0
#define MODE_OFFSET             1
#define MODE_DARK_CURRENT       2
#define MODE_SHUTTER_CLOSED     3

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
    unsigned char hour = 0;
    unsigned char minute = 0;
    unsigned char second = 0;
    unsigned char darkMode = MODE_COLLECT;
    char warnCode = 'A';

    // spectrometer data
    unsigned int fileNum = 0;
    short exposureTime = 0;
    unsigned char numExposures = 0;
    float spec[NUM_SPEC_CHANNELS] = {0};
};

#endif // RADIODATA

