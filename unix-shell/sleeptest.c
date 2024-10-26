#include <stdio.h>
#include <unistd.h> // For sleep()
#include <stdlib.h> // For atoi()

int main(int argc, char *argv[])
{
    int sleep_time = 60; // Default sleep time is 60 seconds

    // Check if the user passed an argument
    if (argc > 1)
    {
        sleep_time = atoi(argv[1]); // Convert the argument to an integer
        if (sleep_time <= 0)
        {
            printf("Invalid sleep time. Using default of 60 seconds.\n");
            sleep_time = 60;
        }
    }

    printf("Sleeping for %d seconds...\n", sleep_time);
    sleep(sleep_time); // Sleep for the specified number of seconds

    printf("\nDone sleeping for %d seconds.\n", sleep_time);
    return 0;
}
