#include <stdio.h>
#include <android/log.h>
#define TAG "HelloWorld"
int main(void) {
    printf("hello world\n");
    __android_log_print(ANDROID_LOG_INFO, TAG, "hello world");
    return 0;
}
