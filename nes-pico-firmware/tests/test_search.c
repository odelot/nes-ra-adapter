#include "test_search.h"

unsigned long getMicros () {
    struct timespec tms;

    /* The C11 way */
    /* if (! timespec_get(&tms, TIME_UTC)) { */

    /* POSIX.1-2008 way */
    if (clock_gettime(CLOCK_REALTIME,&tms)) {
        return -1;
    }
    /* seconds, multiplied with 1 million */
    int64_t micros = tms.tv_sec * 1000000;
    /* Add full microseconds */
    micros += tms.tv_nsec/1000;
    /* round up if necessary */
    if (tms.tv_nsec % 1000 >= 500) {
        ++micros;
    }
    return micros;
}

// used to compare the time to search for a value in a list using sequencial search and binary search on PICO 
void test_search_method () {
    int list[10000];
    for (int k = 2; k < 10000; k += 1) {
        unsigned long sequencial, binary;
        for (int i = 0; i < k; i += 1)
        {
            list[i] = rand() % 10000;
        }
        

        // sort the list
        for (int i = 0; i < k; i += 1)
        {
            for (int j = 0; j < k - i - 1; j += 1)
            {
                if (list[j] > list[j + 1])
                {
                    int temp = list[j];
                    list[j] = list[j + 1];
                    list[j + 1] = temp;
                }
            }
        }

        // get timestamp before search on windows
        unsigned long start, end;
        int found = 0;
        start = getMicros();
        

        // iterate over the list and search for a random value
        
        for (int i = 0; i < 100000; i+= 1)
        {
            int search = rand() % 10000;
            for (int j = 0; j < k; j += 1)
            {
                if (list[j] == search)
                {
                    found +=1;
                }
            }
        }

        // get timestamp after search
        end = getMicros();
        sequencial = end - start;

        printf("Search time n:%d - sequencial search: %lu - hits: %d \n", k, sequencial, found);

        found = 0;
        // get timestamp before search
        start = getMicros();

        // iterate over the list and search for a random value using binary search
        for (int i=0; i < 100000; i +=1 ){
            int search = rand() % 10000;
            int left = 0;
            int right = k - 1;
            while (left < right)
            {
                int mid = right - (right - left) / 2;
                
                if (list[mid] < search)
                {
                    left = mid;
                }
                else
                {
                    right = mid - 1;
                }
            }
            if (list[right] == search)
            {
                found++;
            }
        }

        // get timestamp after search
        end = getMicros();
        binary = end - start;

        printf("Search time n:%d - binary search: %lu hits: %d\n", k, binary, found);
        if (binary < sequencial) {
            printf("Binary search is faster than sequencial search with vector with size %d\n", k);
            break;
        }
        
    }
}