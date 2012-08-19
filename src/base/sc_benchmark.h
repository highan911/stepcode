#ifndef SC_BENCHMARK_H
#define SC_BENCHMARK_H
/// \file sc_benchmark.h memory info, timers, etc for benchmarking

#include "scl_export.h"

#ifdef __cplusplus
#include <iostream>
#include <string>

extern "C" {
#endif

typedef struct {
    long virtMemKB, physMemKB, userMilliseconds, sysMilliseconds;
} benchVals;

/** return a benchVals struct with four current statistics for this process:
 * virtual and physical memory use in kb,
 * user and system cpu time in ms
 *
 * not yet implemented for OSX or Windows.
 */
SCL_BASE_EXPORT benchVals getMemAndTime( );

#ifdef __cplusplus
}


/** reports the difference in memory and cpu use between when the
 * constructor is called and when stop() or the destructor is called.
 *
 * if the destructor is called and stop() had not previously been
 * called, the results are printed to the ostream given in the
 * constructor, prefixed by the description.
 *
 * depends on getMemAndTime() above - may not work on all platforms.
 */
SCL_BASE_EXPORT class benchmark {
protected:
    benchVals initialVals, laterVals;
    std::ostream& ostr;
    std::string descr;
    bool debug, stopped;
public:
    benchmark( std::string description = "", bool debugMessages = true, std::ostream& o_stream = std::cout );

    /// if 'stopped' is false, uses str(true) to print to ostream
    ~benchmark( );
    void reset( );
    benchVals get( );
    void stop( );

    /// converts data member 'laterVals' into a string and returns it
    std::string str( );

    /// outputs result of str() on ostream 'ostr'
    void out( );

    /// converts 'bv' into a string, prefixed by data member 'descr'
    std::string str( const benchVals& bv );
};


#endif //__cplusplus
#endif //SC_BENCHMARK_H
