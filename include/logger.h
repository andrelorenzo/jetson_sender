#ifndef LOGGER_H_
#define LOGGER_H_


typedef enum{
    DEBUG = 0U,
    INFO,
    WARN,
    ERROR,
    FATAL,
    NONE
}verb_e;

static verb_e max_verbosity = WARN;

void Logger(verb_e verbosity, const char * format, ...);
verb_e LoggerGetVerbsity(void);
void LoggerSetVerbsity(verb_e);



#ifndef UNUSED_VAR
#define UNUSED_VAR(a) (void)(a)
#endif
#ifndef UNUSED_FN
#define UNUSED_FN (void)
#endif
#ifndef ARRAY_LEN
#define ARRAY_LEN(arr) (sizeof(arr)/sizeof((arr)[0]))
#endif

#ifdef LOGGER_IMP

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
#define LOGGER_WEAK __attribute__((weak))
#else
#define LOGGER_WEAK __weak
#endif

#include "stdarg.h"
#include "stdio.h"


extern void printOut(verb_e verbosity, const char * msg, size_t size);
/// @brief Get the actual verbosity level
/// @return the actual verbosity level
verb_e LoggerGetVerbsity(){
    return max_verbosity;
}
/// @brief Set the max verbosity level
/// @param max_verb max verbosity level, i.e if max_verb is set to WARN, only WARN, ERROR AND FATAL will be printed 
void LoggerSetVerbsity(verb_e max_verb){
    max_verbosity = max_verb;
}

/// @brief Actual logging function
/// @param verbosity verbosity of the message, never set it to NONE
/// @param format msg to be send
/// @param  variadic arguments of the function
void Logger(verb_e verbosity, const char * format, ...){

    if(verbosity >= max_verbosity){
        va_list args;
        va_start(args, format);
        char msg[1024];
        int size = vsnprintf(msg, sizeof(msg), format, args);
        va_end(args);
        if(size < 0) size = 0;
        printOut(verbosity, msg, size);

    }
    

}
#endif // LOGGER_IMP

#endif
