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

    data.lat = 5;
    data.lon = 6;
    data.alt = 7;
    data.course = 8;
    data.speed = 9;
    data.num_sats = 35;
    data.quality = 8;
    data.hour = 16;
    data.minute = 20;
    data.second = 5;
    data.exposureTime = 500;
    data.numExposures = 1;
    data.darkMode = MODE_COLLECT;
    

    data.fileNum = 0;
    cout << "file num: "        << (unsigned int)data.fileNum         << endl;
    cout << "lat: "             << (unsigned int)data.lat             << endl;
    cout << "lon: "             << (unsigned int)data.lon             << endl;
    cout << "alt: "             << (unsigned int)data.alt             << endl;
    cout << "speed: "           << (unsigned int)data.speed           << endl;
    cout << "course: "          << (unsigned int)data.course          << endl;
    cout << "num_sats: "        << (unsigned int)data.num_sats        << endl;
    cout << "quality: "         << (unsigned int)data.quality         << endl;
    cout << "hour: "            << (unsigned int)data.hour            << endl;
    cout << "min: "             << (unsigned int)data.minute          << endl;
    cout << "sec: "             << (unsigned int)data.second          << endl;
    cout << "darkMode: "        << (unsigned int)data.darkMode        << endl;
    cout << "exposureTime: "    << (unsigned int)data.exposureTime    << endl;
    cout << "numExposures: "    << (unsigned int)data.numExposures    << endl;


    for(;;){
        for(int i = 0; i < 2048; i++){
            data.spec[i] = i; //dist(gen);
        }
        int numBytes = radio.queue_data((byte*)&data,sizeof(RadioData));
        data.fileNum++;

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
        sleep(1);
    }



    return 0;
}
