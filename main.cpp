#include "radiomanager.hpp"
#include <thread>
#include <chrono>
#include <unistd.h>
#include <random>
#include <signal.h>

using std::cout;
using std::cin;
using std::endl;

#include "radiodata.hpp"

void print_hex(byte * data, size_t len);

RadioManager * radio_ptr;

void clean_up(int s){
    radio_ptr->~RadioManager();
    exit(0);
}

int main(){
    RadioManager radio;
    radio_ptr = &radio;
    RadioData data;
    std::default_random_engine gen;
    std::uniform_real_distribution<float> dist(0.0,100.0);

    struct sigaction sig;
    sig.sa_handler = clean_up;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;

    sigaction(SIGINT,&sig,NULL);

    cout << sizeof(data) << endl;

    int total_bytes = 0;
    int errorCode = -1;

START:
    cout << "hello" << endl;
    errorCode = radio.open_serial();
    cout << "Serial setup code: " << errorCode << endl << endl;
    while(errorCode != 0){
        char in;
        bool valid = false;
        while(!valid){
            cout << "Try again? [y/n]: ";
            cin >> in;
            cout << endl;
            if(in=='Y'||in=='N'){
                in+='A'-'a';
            }
            if(in!='y' && in!='n'){
                cout << "invalid" << endl << endl;
                valid = false;
            }else{
                valid = true;
            }
        }
        if(in == 'y'){
            radio.close_serial();
            errorCode = radio.open_serial();
        } else{
            exit(0);
        }
        cout << "Serial setup code: " << errorCode << endl << endl;
    }

    data.fileNum        = 0;
    data.lat            = 1;
    data.lon            = 2;
    data.alt            = 3;
    data.speed          = 4;
    data.course         = 5;
    data.num_sats       = 6;
    data.quality        = 7;
    data.year           = 8;
    data.month          = 9;
    data.day            = 10;
    data.hour           = 11;
    data.minute         = 12;
    data.second         = 13;
    data.darkMode       = 14;
    data.exposureTime   = 15;
    data.numExposures   = 16;
    for(int i = 0; i < 2048; i++){
        data.spec[i] = i; //dist(gen);
    }

    cout << "file num: "        << (unsigned int)data.fileNum         << endl;
    cout << "lat: "             << (unsigned int)data.lat             << endl;
    cout << "lon: "             << (unsigned int)data.lon             << endl;
    cout << "alt: "             << (unsigned int)data.alt             << endl;
    cout << "speed: "           << (unsigned int)data.speed           << endl;
    cout << "course: "          << (unsigned int)data.course          << endl;
    cout << "num_sats: "        << (unsigned int)data.num_sats        << endl;
    cout << "quality: "         << (unsigned int)data.quality         << endl;
    cout << "year: "            << (unsigned int)data.year            << endl;
    cout << "month: "           << (unsigned int)data.month           << endl;
    cout << "day: "             << (unsigned int)data.day             << endl;
    cout << "hour: "            << (unsigned int)data.hour            << endl;
    cout << "min: "             << (unsigned int)data.minute          << endl;
    cout << "sec: "             << (unsigned int)data.second          << endl;
    cout << "darkMode: "        << (unsigned int)data.darkMode        << endl;
    cout << "warnCode: "        << (unsigned int)data.warnCode        << endl;
    cout << "exposureTime: "    << (unsigned int)data.exposureTime    << endl;
    cout << "numExposures: "    << (unsigned int)data.numExposures    << endl;

    print_hex((byte*)&data,sizeof(RadioData));

    byte * start = (byte *)&data;

    cout << "lat: "             << (size_t)((byte *)(&(data.lat           )) - start) << endl; print_hex((byte *)&data.lat,           sizeof(float));
    cout << "lon: "             << (size_t)((byte *)(&(data.lon           )) - start) << endl; print_hex((byte *)&data.lon,           sizeof(float));
    cout << "alt: "             << (size_t)((byte *)(&(data.alt           )) - start) << endl; print_hex((byte *)&data.alt,           sizeof(float));
    cout << "speed: "           << (size_t)((byte *)(&(data.speed         )) - start) << endl; print_hex((byte *)&data.speed,         sizeof(float));
    cout << "course: "          << (size_t)((byte *)(&(data.course        )) - start) << endl; print_hex((byte *)&data.course,        sizeof(float));
    cout << "num_sats: "        << (size_t)((byte *)(&(data.num_sats      )) - start) << endl; print_hex((byte *)&data.num_sats,      sizeof(char));
    cout << "quality: "         << (size_t)((byte *)(&(data.quality       )) - start) << endl; print_hex((byte *)&data.quality,       sizeof(char));
    cout << "year: "            << (size_t)((byte *)(&(data.year          )) - start) << endl; print_hex((byte *)&data.year,          sizeof(char));
    cout << "month: "           << (size_t)((byte *)(&(data.month         )) - start) << endl; print_hex((byte *)&data.month,         sizeof(char));
    cout << "day: "             << (size_t)((byte *)(&(data.day           )) - start) << endl; print_hex((byte *)&data.day,           sizeof(char));
    cout << "hour: "            << (size_t)((byte *)(&(data.hour          )) - start) << endl; print_hex((byte *)&data.hour,          sizeof(char));
    cout << "min: "             << (size_t)((byte *)(&(data.minute        )) - start) << endl; print_hex((byte *)&data.minute,        sizeof(char));
    cout << "sec: "             << (size_t)((byte *)(&(data.second        )) - start) << endl; print_hex((byte *)&data.second,        sizeof(char));
    cout << "darkMode: "        << (size_t)((byte *)(&(data.darkMode      )) - start) << endl; print_hex((byte *)&data.darkMode,      sizeof(char));
    cout << "warnCode: "        << (size_t)((byte *)(&(data.warnCode      )) - start) << endl; print_hex((byte *)&data.warnCode,      sizeof(char));
    cout << "file num: "        << (size_t)((byte *)(&(data.fileNum       )) - start) << endl; print_hex((byte *)&data.fileNum,       sizeof(long));
    cout << "exposureTime: "    << (size_t)((byte *)(&(data.exposureTime  )) - start) << endl; print_hex((byte *)&data.exposureTime,  sizeof(short));
    cout << "numExposures: "    << (size_t)((byte *)(&(data.numExposures  )) - start) << endl; print_hex((byte *)&data.numExposures,  sizeof(char));
    cout << "spec: "            << (size_t)((byte *)( data.spec            ) - start) << endl; print_hex((byte *) data.spec,          sizeof(float)*2048);

    for(;;){
        for(int i = 0; i < 2048; i++){
            data.spec[i] = i; //dist(gen);
        }
        data.fileNum++;
        int numBytes = radio.queue_data((byte*)&data,sizeof(RadioData));

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
            errorCode = radio.close_serial();
            goto START; // im a bad boy, but it works
        }

        if(total_bytes > 0){//8240*1){
            //cout << "Total Sent: " << total_bytes << endl;
            while(radio.send_in_progress()){sleep(4);};

            //sleep(30);

            radio.close_serial();
            return 0;
        }
    }



    return 0;
}
