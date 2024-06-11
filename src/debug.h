#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG_LEVEL_WARN

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

#ifdef DEBUG_LEVEL_LOG
    #define LOG(fmt, ...) printf(BLU "[LOG] " fmt RESET, ##__VA_ARGS__)
    #define WARN(fmt, ...) fprintf(stderr,YEL "[WARN] " fmt RESET, ##__VA_ARGS__)
    #define ERROR(fmt, ...) fprintf(stderr,RED "[ERR] " fmt RESET, ##__VA_ARGS__)
#elif defined(DEBUG_LEVEL_WARN)
    #define LOG(fmt, ...)
    #define WARN(fmt, ...) fprintf(stderr,YEL "[WARN] " fmt RESET, ##__VA_ARGS__)
    #define ERROR(fmt, ...) fprintf(stderr,RED "[ERR] " fmt RESET, ##__VA_ARGS__)
#elif defined(DEBUG_LEVEL_ERR)
    #define LOG(fmt, ...)
    #define WARN(fmt, ...)
    #define ERROR(fmt, ...) fprintf(stderr,RED "[ERR] " fmt RESET, ##__VA_ARGS__)
#else
    #define LOG(fmt, ...)
    #define WARN(fmt, ...)
    #define ERROR(fmt, ...)
#endif

#endif