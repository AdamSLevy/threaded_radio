#include <iostream>

using std::cout;
using std::endl;

int main(){
    cout << "char:        " << sizeof(char) << endl;
    cout << "short:       " << sizeof(short) << endl;
    cout << "int:         " << sizeof(int) << endl;
    cout << "long:        " << sizeof(long) << endl;
    cout << "float:       " << sizeof(float) << endl;
    cout << "double:      " << sizeof(double) << endl;
    cout << "long double: " << sizeof(long double) << endl;

    int day = 24;
    unsigned char ucday = day;

    float second = 2.5;
    unsigned char ucsec = second;

    cout << "ucday: " << (int)ucday << endl;
    cout << "ucsec: " << (int)ucsec << endl;

    return 0;
}
