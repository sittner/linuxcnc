/*
** tool_watch: diagnostic tool for monitoring tool data.
** NML channels have been removed; this tool is no longer functional.
*/
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    fprintf(stderr, "tool_watch: NML channels have been removed. "
                    "Use the REST/WebSocket API instead.\n");
    return 1;
} // main()
