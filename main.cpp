#include "radiomanager.h"
#include <thread>
#include <chrono>
#include <unistd.h>
#include <random>

using std::cout;
using std::cin;
using std::endl;

struct RadioData{
    // gps data
    float lat;
    float lon;
    float alt;
    float speed;
    float course;
    // none of these numbers will be bigger than 255
    unsigned char num_sats;
    unsigned char quality;
    unsigned char year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char minute;
    unsigned char darkMode;
    unsigned char second;
    char warnCode;

    // spectrometer data
    unsigned long fileNum;
    short exposureTime;
    unsigned char numExposures;
    float spec[2048];
};

int main(){
    RadioManager radio;
    RadioData data;
    std::default_random_engine gen;
    std::uniform_real_distribution<float> dist(0.0,100.0);

    cout << sizeof(data) << endl;

    int total_bytes = 0;
    int errorCode = -1;

START:
    cout << "hello" << endl;
    errorCode = radio.setUpSerial();
    cout << "Serial setup code: " << errorCode << endl << endl;
    while(errorCode != 0){
        char in;
        bool valid = false;
        while(!valid){
            cout << "Try again? [y/n]: ";
            cin >> in;
            cout << endl;
            if(in=='Y'||in=='N')
                in+='A'-'a';
            if(in!='y' && in!='n'){
                cout << "invalid" << endl << endl;
                valid = false;
            }else{
                valid = true;
            }
        }
        if(in == 'y'){
            radio.closeSerial();
            errorCode = radio.setUpSerial();
        } else{
            exit(0);
        }
        cout << "Serial setup code: " << errorCode << endl << endl;
    }

    data.fileNum = 0;

    for(;;){
        for(int i = 0; i < 2048; i++){
            data.spec[i] = dist(gen);
        }
        data.fileNum++;
        int numBytes = radio.send((byte*)&data,sizeof(RadioData));

        if(numBytes>0){
            total_bytes += numBytes;
            //cout << "Sent: " << numBytes << endl;
            //cout << "Total: " << total_bytes << endl;
            //cout << "\t wait ";

            for(int i = 0; i > 0; i--){
                //cout << i << " ";
                //cout.flush();
                //sleep(1);
            }
            //cout << endl << endl;
        } else{
            cout << "Failed to send! \n";
            errorCode = radio.closeSerial();
            goto START; // im a bad boy, but it works
        }

        if(total_bytes > 1000000){
            //cout << "Total Sent: " << total_bytes << endl;
            while(radio.send_in_progress()){};

            radio.closeSerial();
            return 0;
        }
    }



    return 0;
}
