#include <iostream>
using namespace std;

int main() {
    cout << "sizeof(void*): " << sizeof(void*) << endl;
    cout << "sizeof(size_t): " << sizeof(size_t) << endl;
    cout << "Total large struct: " << sizeof(void*) + sizeof(size_t) + sizeof(size_t) << endl;
    cout << "SSO_THRESHOLD: " << sizeof(void*) + sizeof(size_t) + sizeof(size_t) - 1 << endl;
    return 0;
}