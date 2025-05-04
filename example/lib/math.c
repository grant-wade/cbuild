#include "math.h"

int add(int a, int b) {
    return a + b;
}

int subtract(int a, int b) {
    return a - b;
}
int multiply(int a, int b) {
    return a * b;
}

int divide(int a, int b) {
    if (b == 0) {
        return 0; // Handle division by zero
    }
    return a / b;
}

int modulus(int a, int b) {
    if (b == 0) {
        return 0; // Handle division by zero
    }
    return a % b;
}

int power(int base, int exponent) {
    int result = 1;
    for (int i = 0; i < exponent; i++) {
        result *= base;
    }
    return result;
}
