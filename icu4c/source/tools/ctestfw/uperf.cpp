// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/********************************************************************
 * COPYRIGHT:
 * Copyright (c) 2002-2012, International Business Machines Corporation and
 * others. All Rights Reserved.
 ********************************************************************/

// Defines _XOPEN_SOURCE for access to POSIX functions.
// Must be before any other #includes.
#include "uposixdefs.h"

#include "unicode/uperf.h"
#include "uoptions.h"
#include "cmemory.h"
#include <stdio.h>
#include <stdlib.h>

#if !UCONFIG_NO_CONVERSION

UPerfFunction::~UPerfFunction() {}

static const char delim = '/';
static int32_t execCount = 0;
UPerfTest* UPerfTest::gTest = nullptr;
static const int MAXLINES = 40000;
const char UPerfTest::gUsageString[] =
    "Usage: %s [OPTIONS] [FILES]\n"
    "\tReads the input file and prints out time taken in seconds\n"
    "Options:\n"
    "\t-h or -? or --help   this usage text\n"
    "\t-v or --verbose      print extra information when processing files\n"
    "\t-s or --sourcedir    source directory for files followed by path\n"
    "\t                     followed by path\n"
    "\t-e or --encoding     encoding of source files\n"
    "\t-u or --uselen       perform timing analysis on non-null terminated buffer using length\n"
    "\t-f or --file-name    file to be used as input data\n"
    "\t-p or --passes       Number of passes to be performed. Requires Numeric argument.\n"
    "\t                     Cannot be used with --time\n"
    "\t-i or --iterations   Number of iterations to be performed. Requires Numeric argument\n"
    "\t-t or --time         Threshold time for looping until in seconds. Requires Numeric argument.\n"
    "\t                     Cannot be used with --iterations\n"
    "\t-l or --line-mode    The data file should be processed in line mode\n"
    "\t-b or --bulk-mode    The data file should be processed in file based.\n"
    "\t                     Cannot be used with --line-mode\n"
    "\t-L or --locale       Locale for the test\n";

enum
{
    HELP1,
    HELP2,
    VERBOSE,
    SOURCEDIR,
    ENCODING,
    USELEN,
    FILE_NAME,
    PASSES,
    ITERATIONS,
    TIME,
    LINE_MODE,
    BULK_MODE,
    LOCALE,
    OPTIONS_COUNT
};


static UOption options[OPTIONS_COUNT+20]={
    UOPTION_HELP_H,
    UOPTION_HELP_QUESTION_MARK,
    UOPTION_VERBOSE,
    UOPTION_SOURCEDIR,
    UOPTION_ENCODING,
    UOPTION_DEF( "uselen",        'u', UOPT_NO_ARG),
    UOPTION_DEF( "file-name",     'f', UOPT_REQUIRES_ARG),
    UOPTION_DEF( "passes",        'p', UOPT_REQUIRES_ARG),
    UOPTION_DEF( "iterations",    'i', UOPT_REQUIRES_ARG),
    UOPTION_DEF( "time",          't', UOPT_REQUIRES_ARG),
    UOPTION_DEF( "line-mode",     'l', UOPT_NO_ARG),
    UOPTION_DEF( "bulk-mode",     'b', UOPT_NO_ARG),
    UOPTION_DEF( "locale",        'L', UOPT_REQUIRES_ARG)
};

UPerfTest::UPerfTest(int32_t argc, const char* argv[], UErrorCode& status)
        : _argc(argc), _argv(argv), _addUsage(nullptr),
          ucharBuf(nullptr), encoding(""),
          uselen(false),
          fileName(nullptr), sourceDir("."),
          lines(nullptr), numLines(0), line_mode(true),
          buffer(nullptr), bufferLen(0),
          verbose(false), bulk_mode(false),
          passes(1), iterations(0), time(0),
          locale(nullptr) {
    init(nullptr, 0, status);
}

UPerfTest::UPerfTest(int32_t argc, const char* argv[],
                     UOption addOptions[], int32_t addOptionsCount,
                     const char *addUsage,
                     UErrorCode& status)
        : _argc(argc), _argv(argv), _addUsage(addUsage),
          ucharBuf(nullptr), encoding(""),
          uselen(false),
          fileName(nullptr), sourceDir("."),
          lines(nullptr), numLines(0), line_mode(true),
          buffer(nullptr), bufferLen(0),
          verbose(false), bulk_mode(false),
          passes(1), iterations(0), time(0),
          locale(nullptr) {
    init(addOptions, addOptionsCount, status);
}

void UPerfTest::init(UOption addOptions[], int32_t addOptionsCount,
                     UErrorCode& status) {
    //initialize the argument list
    U_MAIN_INIT_ARGS(_argc, _argv);

    resolvedFileName = nullptr;

    // add specific options
    int32_t optionsCount = OPTIONS_COUNT;
    if (addOptionsCount > 0) {
        memcpy(options+optionsCount, addOptions, addOptionsCount*sizeof(UOption));
        optionsCount += addOptionsCount;
    }

    //parse the arguments
    _remainingArgc = u_parseArgs(_argc, const_cast<char**>(_argv), optionsCount, options);

    // copy back values for additional options
    if (addOptionsCount > 0) {
        memcpy(addOptions, options+OPTIONS_COUNT, addOptionsCount*sizeof(UOption));
    }

    // Now setup the arguments
    if(_argc==1 || options[HELP1].doesOccur || options[HELP2].doesOccur) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if(options[VERBOSE].doesOccur) {
        verbose = true;
    }

    if(options[SOURCEDIR].doesOccur) {
        sourceDir = options[SOURCEDIR].value;
    }

    if(options[ENCODING].doesOccur) {
        encoding = options[ENCODING].value;
    }

    if(options[USELEN].doesOccur) {
        uselen = true;
    }

    if(options[FILE_NAME].doesOccur){
        fileName = options[FILE_NAME].value;
    }

    if(options[PASSES].doesOccur) {
        passes = atoi(options[PASSES].value);
    }
    if(options[ITERATIONS].doesOccur) {
        iterations = atoi(options[ITERATIONS].value);
        if(options[TIME].doesOccur) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
    } else if(options[TIME].doesOccur) {
        time = atoi(options[TIME].value);
    } else {
        iterations = 1000; // some default
    }

    if(options[LINE_MODE].doesOccur) {
        line_mode = true;
        bulk_mode = false;
    }

    if(options[BULK_MODE].doesOccur) {
        bulk_mode = true;
        line_mode = false;
    }
    
    if(options[LOCALE].doesOccur) {
        locale = options[LOCALE].value;
    }

    int32_t len = 0;
    if(fileName!=nullptr){
        //pre-flight
        UErrorCode bufferStatus = U_ZERO_ERROR;
        ucbuf_resolveFileName(sourceDir, fileName, nullptr, &len, &bufferStatus);
        resolvedFileName = static_cast<char*>(uprv_malloc(len));
        if(resolvedFileName==nullptr){
            status= U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        if(bufferStatus == U_BUFFER_OVERFLOW_ERROR){
            bufferStatus = U_ZERO_ERROR;
        }
        ucbuf_resolveFileName(sourceDir, fileName, resolvedFileName, &len, &bufferStatus);
        if (U_FAILURE(bufferStatus)) {
            status = bufferStatus;
        }
        ucharBuf = ucbuf_open(resolvedFileName,&encoding,true,false,&status);

        if(U_FAILURE(status)){
            printf("Could not open the input file %s. Error: %s\n", fileName, u_errorName(status));
            return;
        }
    }
}

ULine* UPerfTest::getLines(UErrorCode& status){
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (lines != nullptr) {
        return lines;  // don't do it again
    }
    lines     = new ULine[MAXLINES];
    int maxLines = MAXLINES;
    numLines=0;
    const char16_t* line=nullptr;
    int32_t len =0;
    for (;;) {
        line = ucbuf_readline(ucharBuf,&len,&status);
        if(line == nullptr || U_FAILURE(status)){
            break;
        }
        lines[numLines].name  = new char16_t[len];
        lines[numLines].len   = len;
        memcpy(lines[numLines].name, line, len * U_SIZEOF_UCHAR);

        numLines++;
        len = 0;
        if (numLines >= maxLines) {
            maxLines += MAXLINES;
            ULine *newLines = new ULine[maxLines];
            if(newLines == nullptr) {
                fprintf(stderr, "Out of memory reading line %d.\n", static_cast<int>(numLines));
                status= U_MEMORY_ALLOCATION_ERROR;
                delete []lines;
                return nullptr;
            }

            memcpy(newLines, lines, numLines*sizeof(ULine));
            delete []lines;
            lines = newLines;
        }
    }
    return lines;
}
const char16_t* UPerfTest::getBuffer(int32_t& len, UErrorCode& status){
    if (U_FAILURE(status)) {
        return nullptr;
    }
    len = ucbuf_size(ucharBuf);
    buffer = static_cast<char16_t*>(uprv_malloc(U_SIZEOF_UCHAR * (len + 1)));
    u_strncpy(buffer,ucbuf_getBuffer(ucharBuf,&bufferLen,&status),len);
    buffer[len]=0;
    len = bufferLen;
    return buffer;
}
UBool UPerfTest::run(){
    if(_remainingArgc==1){
        // Testing all methods
        return runTest();
    }
    UBool res=false;
    // Test only the specified function
    for (int i = 1; i < _remainingArgc; ++i) {
        if (_argv[i][0] != '-') {
            char* name = const_cast<char*>(_argv[i]);
            if(verbose==true){
                //fprintf(stdout, "\n=== Handling test: %s: ===\n", name);
                //fprintf(stdout, "\n%s:\n", name);
            }
            char* parameter = strchr( name, '@' );
            if (parameter) {
                *parameter = 0;
                parameter += 1;
            }
            execCount = 0;
            res = runTest( name, parameter );
            if (!res || (execCount <= 0)) {
                fprintf(stdout, "\n---ERROR: Test doesn't exist: %s!\n", name);
                return false;
            }
        }
    }
    return res;
}
UBool UPerfTest::runTest(char* name, char* par ){
    UBool rval;
    char* pos = nullptr;

    if (name)
        pos = strchr( name, delim ); // check if name contains path (by looking for '/')
    if (pos) {
        path = pos+1;   // store subpath for calling subtest
        *pos = 0;       // split into two strings
    }else{
        path = nullptr;
    }

    if (!name || (name[0] == 0) || (strcmp(name, "*") == 0)) {
        rval = runTestLoop( nullptr, nullptr );

    }else if (strcmp( name, "LIST" ) == 0) {
        this->usage();
        rval = true;

    }else{
        rval = runTestLoop( name, par );
    }

    if (pos)
        *pos = delim;  // restore original value at pos
    return rval;
}


void UPerfTest::setPath( char* pathVal )
{
    this->path = pathVal;
}

// call individual tests, to be overridden to call implementations
UPerfFunction* UPerfTest::runIndexedTest( int32_t /*index*/, UBool /*exec*/, const char* & /*name*/, char* /*par*/ )
{
    // to be overridden by a method like:
    /*
    switch (index) {
        case 0: name = "First Test"; if (exec) FirstTest( par ); break;
        case 1: name = "Second Test"; if (exec) SecondTest( par ); break;
        default: name = ""; break;
    }
    */
    fprintf(stderr,"*** runIndexedTest needs to be overridden! ***");
    return nullptr;
}


UBool UPerfTest::runTestLoop( char* testname, char* par )
{
    int32_t    index = 0;
    const char*   name;
    UBool  run_this_test;
    UBool  rval = false;
    UErrorCode status = U_ZERO_ERROR;
    UPerfTest* saveTest = gTest;
    gTest = this;
    int32_t loops = 0;
    double t=0;
    int32_t n = 1;
    long ops;
    do {
        this->runIndexedTest( index, false, name );
        if (!name || (name[0] == 0))
            break;
        if (!testname) {
            run_this_test = true;
        }else{
            run_this_test = static_cast<UBool>(strcmp(name, testname) == 0);
        }
        if (run_this_test) {
            UPerfFunction* testFunction = this->runIndexedTest( index, true, name, par );
            execCount++;
            rval=true;
            if(testFunction==nullptr){
                fprintf(stderr,"%s function returned nullptr", name);
                return false;
            }
            ops = testFunction->getOperationsPerIteration();
            if (ops < 1) {
                fprintf(stderr, "%s returned an illegal operations/iteration()\n", name);
                return false;
            }
            if(iterations == 0) {
                n = time;
                // Run for specified duration in seconds
                if(verbose==true){
                    fprintf(stdout, "= %s calibrating %i seconds \n", name, static_cast<int>(n));
                }

                //n *=  1000; // s => ms
                //System.out.println("# " + meth.getName() + " " + n + " sec");
                int32_t failsafe = 1; // last resort for very fast methods
                t = 0;
                while (t < static_cast<int>(n * 0.9)) { // 90% is close enough
                    if (loops == 0 || t == 0) {
                        loops = failsafe;
                        failsafe *= 10;
                    } else {
                        //System.out.println("# " + meth.getName() + " x " + loops + " = " + t);
                        loops = static_cast<int>(static_cast<double>(n) / t * loops + 0.5);
                        if (loops == 0) {
                            fprintf(stderr,"Unable to converge on desired duration");
                            return false;
                        }
                    }
                    //System.out.println("# " + meth.getName() + " x " + loops);
                    t = testFunction->time(loops,&status);
                    if(U_FAILURE(status)){
                        printf("Performance test failed with error: %s \n", u_errorName(status));
                        break;
                    }
                }
            } else {
                loops = iterations;
            }

            double min_t=1000000.0, sum_t=0.0;
            long events = -1;

            for(int32_t ps =0; ps < passes; ps++){
                if(verbose==true){
                    fprintf(stdout,"= %s begin " ,name);
                    if(iterations > 0) {
                        fprintf(stdout, "%i\n", static_cast<int>(loops));
                    } else {
                        fprintf(stdout, "%i\n", static_cast<int>(n));
                    }
                }
                t = testFunction->time(loops, &status);
                if(U_FAILURE(status)){
                    printf("Performance test failed with error: %s \n", u_errorName(status));
                    break;
                }
                sum_t+=t;
                if(t<min_t) {
                    min_t=t;
                }
                events = testFunction->getEventsPerIteration();
                //print info only in verbose mode
                if(verbose==true){
                    if(events == -1){
                        fprintf(stdout, "= %s end: %f loops: %i operations: %li \n", name, t, static_cast<int>(loops), ops);
                    }else{
                        fprintf(stdout, "= %s end: %f loops: %i operations: %li events: %li\n", name, t, static_cast<int>(loops), ops, events);
                    }
                }
            }
            if(verbose && U_SUCCESS(status)) {
                double avg_t = sum_t/passes;
                if (loops == 0 || ops == 0) {
                    fprintf(stderr, "%s did not run\n", name);
                }
                else if(events == -1) {
                    fprintf(stdout, "%%= %s avg: %.4g loops: %i avg/op: %.4g ns\n",
                            name, avg_t, static_cast<int>(loops), (avg_t * 1E9) / (loops * ops));
                    fprintf(stdout, "_= %s min: %.4g loops: %i min/op: %.4g ns\n",
                            name, min_t, static_cast<int>(loops), (min_t * 1E9) / (loops * ops));
                }
                else {
                    fprintf(stdout, "%%= %s avg: %.4g loops: %i avg/op: %.4g ns avg/event: %.4g ns\n",
                            name, avg_t, static_cast<int>(loops), (avg_t * 1E9) / (loops * ops), (avg_t * 1E9) / (loops * events));
                    fprintf(stdout, "_= %s min: %.4g loops: %i min/op: %.4g ns min/event: %.4g ns\n",
                            name, min_t, static_cast<int>(loops), (min_t * 1E9) / (loops * ops), (min_t * 1E9) / (loops * events));
                }
            }
            else if(U_SUCCESS(status)) {
                // Print results in ndjson format for GHA Benchmark to process.
                fprintf(stdout,
                        "{\"biggerIsBetter\":false,\"name\":\"%s\",\"unit\":\"ns/iter\",\"value\":%.4f}\n",
                        name, (min_t*1E9)/(loops*ops));
            }
            delete testFunction;
        }
        index++;
    }while(name);

    gTest = saveTest;
    return rval;
}

/**
* Print a usage message for this test class.
*/
void UPerfTest::usage()
{
    puts(gUsageString);
    if (_addUsage != nullptr) {
        puts(_addUsage);
    }

    UBool save_verbose = verbose;
    verbose = true;
    fprintf(stdout,"Test names:\n");
    fprintf(stdout,"-----------\n");

    int32_t index = 0;
    const char* name = nullptr;
    do{
        this->runIndexedTest( index, false, name );
        if (!name)
            break;
        fprintf(stdout, "%s\n", name);
        index++;
    }while (name && (name[0] != 0));
    verbose = save_verbose;
}




void UPerfTest::setCaller( UPerfTest* callingTest )
{
    caller = callingTest;
    if (caller) {
        verbose = caller->verbose;
    }
}

UBool UPerfTest::callTest( UPerfTest& testToBeCalled, char* par )
{
    execCount--; // correct a previously assumed test-exec, as this only calls a subtest
    testToBeCalled.setCaller( this );
    return testToBeCalled.runTest( path, par );
}

UPerfTest::~UPerfTest(){
    delete[] lines;
    if(buffer!=nullptr){
        uprv_free(buffer);
    }
    if(resolvedFileName!=nullptr){
        uprv_free(resolvedFileName);
    }
    ucbuf_close(ucharBuf);
}

#endif
