#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <boost/asio.hpp>
#include <sstream>
#include <string.h>

#include "leveldb/env.h"
#include "leveldb/perf_count.h"
#include "port/port.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

using boost::asio::ip::tcp;

void command_help();

int
main(
    int argc,
    char ** argv)
{
    bool error_seen, csv_header, diff_mode, running, verbose;
    int error_counter;
    unsigned diff_seconds;
    unsigned top_metrics = leveldb::ePerfCountEnumSize;
    unsigned bot_metrics = 0;
    char ** cursor;
    char *graphite = NULL;
    char *port = NULL;

    running=true;
    error_seen=false;
    error_counter=0;

    csv_header=false;
    verbose=false;
    diff_mode=false;
    diff_seconds=1;


    for (cursor=argv+1; NULL!=*cursor && running; ++cursor)
    {
        // option flag?
        if ('-'==**cursor)
        {
            char flag;

            flag=*((*cursor)+1);
            switch(flag)
            {
                case 'h':  csv_header=true; break;
                case 'v':  verbose=true; break;
                case 'd':
                    diff_mode=true;
                    ++cursor;
                    diff_seconds=strtoul(*cursor, NULL, 10);
                    break;
                case 'g':
                    ++cursor;
                    graphite = (char*) malloc(strlen(*cursor) + 1); //memleak LOL
                    strcpy(graphite, *cursor);
                    break;
                case 'p':
                    ++cursor;
                    port = (char *) malloc(strlen(*cursor) + 1); //memleak LOL
                    strcpy(port, *cursor);
                    break;
                case 't':
                    ++cursor;
                    top_metrics=strtoul(*cursor, NULL, 10); 
                    break;
                case 'b':
                    ++cursor;
                    bot_metrics=strtoul(*cursor, NULL, 10); 
                    break;
                default:
                    fprintf(stderr, " option \'%c\' is not valid\n", flag);
                    command_help();
                    running=false;
                    error_counter=1;
                    error_seen=true;
                    break;
            }   // switch
        }   // if

        // non flag params
        else
        {
            fprintf(stderr, " option \'%s\' is not valid\n", *cursor);
            command_help();
            running=false;
            error_counter=1;
            error_seen=true;
        }   // else
    }   // for

    // attach to shared memory if params looking good
    if (!error_seen)
    {
        // connect to graphite
        if (graphite == NULL || port == NULL) {
            fprintf(stderr, "ERROR: graphite settings (-g and -p) required\n");
            exit(EXIT_FAILURE);
        }
        boost::asio::io_service io_service;
        tcp::resolver resolver(io_service);
        tcp::resolver::query query(graphite, port);
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        tcp::socket socket(io_service);
        boost::asio::connect(socket, endpoint_iterator);

        const leveldb::PerformanceCounters * perf_ptr;
        bool first_pass;

        first_pass=true;
        perf_ptr=leveldb::PerformanceCounters::Init(true);

        if (NULL!=perf_ptr)
        {
            uint64_t first_time;
            int loop;

            first_time=leveldb::port::TimeMicros();

            if (csv_header)
            {
                csv_header=false;
                printf("time, diff time, name, count\n");
            }   // if

            if (diff_mode)
            {
                uint64_t prev_counters[leveldb::ePerfCountEnumSize], cur_counters[leveldb::ePerfCountEnumSize];
                uint64_t cur_time;

                do
                {
                    // capture state before reporting
                    cur_time=leveldb::port::TimeMicros();
                    for (loop=0; loop<leveldb::ePerfCountEnumSize; ++loop)
                    {
                        cur_counters[loop]=perf_ptr->Value(loop);
                    }   // for

                    if (!first_pass)
                    {
                        for (loop=bot_metrics; loop < top_metrics; ++loop)
                        {
                            long int t = static_cast<long int>(time(NULL));
                            std::stringstream ss;
                            ss << "pl3.boost.test." << leveldb::PerformanceCounters::GetNamePtr(loop)
                               << " " << cur_counters[loop]-prev_counters[loop]
                               << " " << t
                               << std::endl;
                            std::string message(ss.str());
                            boost::system::error_code ignored_error;
                            boost::asio::write(socket, boost::asio::buffer(message), ignored_error);
                            if (verbose)
                              fprintf(stdout, "DB activity: %s", message.c_str());
                        }   // for
                    }   // if

                    first_pass=false;

                    // save for next pass
                    //  (counters are "live" so use data previously reported to maintain some consistency)
                    for (loop=0; loop<leveldb::ePerfCountEnumSize; ++loop)
                    {
                        prev_counters[loop]=cur_counters[loop];
                    }   // for

                    sleep(diff_seconds);
                } while(true);
            }   // if

            // one time dump
            else
            {
                for (loop=0; loop<leveldb::ePerfCountEnumSize; ++loop)
                {
                    printf("%" PRIu64 ", %u, %s, %" PRIu64 "\n",
                           first_time, 0,
                           leveldb::PerformanceCounters::GetNamePtr(loop),
                           perf_ptr->Value(loop));
                }   // for
            }   // else
        }   // if
        else
        {
            fprintf(stderr, "unable to attach to shared memory, error %d\n",
                    leveldb::PerformanceCounters::m_LastError);
            ++error_counter;
            error_seen=true;
        }   // else
    }   // if

    if (error_seen)
        command_help();

    return( error_seen && 0!=error_counter ? 1 : 0 );

}   // main


void
command_help()
{
    fprintf(stderr, "perf_dump [option]*\n");
    fprintf(stderr, "  options\n");
    fprintf(stderr, "      -h    print csv formatted header line (once)\n");
    fprintf(stderr, "      -v    be verbose with what you send to graphite\n");
    fprintf(stderr, "      -d n  print diff ever n seconds\n");
    fprintf(stderr, "      -g ip send metrics to graphite server at ip\n");
    fprintf(stderr, "      -p pt send metrics to port pt (2003 is plaintext)\n");
    fprintf(stderr, "      -t n  collect metrics about metric n\n");
    fprintf(stderr, "      -b n  collect metrics below metric n\n");
}   // command_help

namespace leveldb {


}  // namespace leveldb

