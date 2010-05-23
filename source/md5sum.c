/*
 * md5sum.c: a simple wrapper for getting md5 hashes from the command line
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: md5sum.c 578 2005-08-21 06:37:53Z wd $");

/* undefine macros so we don't use the main code's stuff */
#undef printf
#undef strlen

/* prototypes */
void usage(char **argv);

void usage(char **argv) {
    printf("usage: %s: [-hq] [-s string|file]\n", argv[0]);
    exit(0);
}

int main(int argc, char **argv) {
    char opt;
    int quiet = 0;
    int file = 1;
    char md5buf[33];
    char *string = "";

    if (argc < 2)
        usage(argv);

    while ((opt = getopt(argc, argv, "hqs:")) != -1) {
        switch (opt) {
            case 'q':
                quiet = 1;
                break;
            case 's':
                string = optarg;
                file = 0;
                break;
            case 'h':
            case '?':
            default:
                usage(argv);
        }
    }
        
    if (file && argv[optind] == NULL)
        usage(argv);
    else if (file)
        string = argv[optind];

    if (file) {
        if (!md5_file(string, md5buf)) 
            printf("File %s does not exist!\n", string);
        else {
            if (quiet)
                printf("%s\n", md5buf);
            else
                printf("MD5 (%s) = %s\n", string, md5buf);
        }
    } else {
        md5_data((unsigned char *)string, strlen(string), md5buf);
        if (quiet)
            printf("%s\n", md5buf);
        else
            printf("MD5 (\"%s\") = %s\n", string, md5buf);
    }

    return 0;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
