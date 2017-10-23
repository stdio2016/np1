#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OUT_OF_MEMORY 2
size_t buf_size = 10;
char *buf;
int readaline(FILE *f) {
    int ch = fgetc(f);
    size_t i = 0;
    while (ch != EOF && ch != '\n') {
        if (i == buf_size - 1) {
            char *newbuf = (char *)realloc(buf, buf_size * 2);
            if (newbuf == NULL) return OUT_OF_MEMORY;
            buf = newbuf;
            buf_size = buf_size * 2;
        }
        buf[i++] = ch;
        ch = fgetc(f);
    }
    buf[i] = '\0';
    return (i == 0 && ch == EOF) ? 0 : 1;
}
size_t *Failfunc;
char *Delim;
size_t DelimLen;
void kmp(void)
{
    size_t i, matched = 1;
    Failfunc[0] = 0;
    for (i = 1; i < DelimLen; i++) {
        if (Delim[matched - 1] == Delim[i]) {
            Failfunc[i] = Failfunc[matched - 1];
            matched++;
        }
        else {
            Failfunc[i] = matched;
            while (matched > 0 && Delim[matched - 1] != Delim[i]) {
                matched = Failfunc[matched - 1];
            }
            matched++;
        }
    }
}

char *kmpsearch(char *arg) {
    size_t i;
    size_t matched = 0;
    for (i = 0; arg[i]; i++) {
        if (arg[i] == Delim[matched]) {
            matched++;
            if (matched == DelimLen) {
                return arg + (i + 1 - DelimLen);
            }
        }
        else {
            if (Failfunc[matched] > 0) {
                matched = Failfunc[matched] - 1;
                i--;
            }
            else {
                matched = 0;
            }
        }
    }
    return NULL;
}

enum CmdMode {
    CmdMode_FILE, CmdMode_INTERACTIVE
};

int cmd(FILE *f, enum CmdMode mode)
{
    int status;
    while((status = readaline(f)) == 1) {
        if (mode == CmdMode_FILE) puts(buf);
        // get instruction and argument
        char *ptr = buf, *instr = buf, *arg = NULL;
        while (*ptr != '\0' && *ptr != ' ') {
            ptr++;
        }
        if (*ptr == ' ') {
            arg = ptr + 1;
            *ptr = '\0';
        }
        else {
            arg = ptr;
        }
        // test instruction
        if (strcmp(instr, "reverse") == 0) {
            size_t len = strlen(arg);
            size_t i;
            for (i = len; i > 0; i--) {
                putchar(arg[i-1]);
            }
            puts("");
        }
        else if (strcmp(instr, "split") == 0) {
            char *split = kmpsearch(arg); // get first delimiter
            int first = 1;
            while (split != NULL) {
                *split = '\0';
                if (!first) putchar(' '); first = 0;
                printf("%s", arg);
                // get next delimiter
                arg = split + DelimLen;
                split = kmpsearch(arg);
            }
            if (1 || *arg) { // show last token
                if (!first) putchar(' '); first = 0;
                printf("%s", arg);
            }
            puts("");
        }
        else if (strcmp(instr, "exit") == 0) {
            return 1;
        }
    }
    if (status == OUT_OF_MEMORY) {
        puts("out of memory");
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        puts("usage: ./Warmup_sample <file name> <separator>");
        return 1;
    }
    buf = malloc(buf_size);
    if (NULL == buf) {
        puts("out of memory");
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (NULL == f) {
        puts("cannot open file");
        return 1;
    }
    Delim = argv[2];
    DelimLen = strlen(Delim);
    Failfunc = (size_t *) malloc(sizeof(size_t) * DelimLen);
    if (NULL == Failfunc) {
        puts("out of memory");
        return 1;
    }
    kmp();
    printf("-----------Input file %s-------------------\n", argv[1]);
    int y = cmd(f, CmdMode_FILE);
    fclose(f);
    if (y == 1) {
        exit(0);
    }
    printf("-----------End of input file %s------------\n", argv[1]);
    printf("****************User input*******************\n");
    cmd(stdin, CmdMode_INTERACTIVE);
    free(Failfunc);
    free(buf);
    return 0;
}
