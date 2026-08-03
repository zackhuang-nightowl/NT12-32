
#ifndef GETOPT_H
#define GETOPT_H

extern char *optarg;

#ifdef __cplusplus
extern "C" {
#endif

int getopt2(int argc, char *argv[], char *opstring);

#ifdef __cplusplus
}
#endif

#endif // GETOPT_H


