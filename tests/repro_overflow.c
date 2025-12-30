#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mocking the vulnerable function pattern found in src/types.c
char* vulnerable_list_to_str(int count) {
    char buf[4096]; // Fixed buffer
    strcpy(buf, "(");
    
    char element[] = "1234567890"; // 10 chars
    
    for (int i = 0; i < count; i++) {
        // In the real code: strcat(buf, val_to_str(car(v)));
        // This is the classic buffer overflow pattern
        strcat(buf, element);
        strcat(buf, " ");
    }
    strcat(buf, ")");
    return strdup(buf);
}

int main() {
    printf("=== Test: Buffer Overflow ===\n");
    // 4096 / 11 chars per element ~= 372 elements to overflow
    // Let's try 400
    printf("Attempting to write 400 elements to 4096 byte buffer...\n");
    char* result = vulnerable_list_to_str(400);
    printf("Result length: %lu\n", strlen(result));
    free(result);
    return 0;
}

