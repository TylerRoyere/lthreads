#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lthread.h"

const char *content = 
"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor\n"
"incididunt ut labore et dolore magna aliqua. Sit amet facilisis magna etiam\n"
"tempor orci eu lobortis. Massa ultricies mi quis hendrerit. Et malesuada fames\n"
"ac turpis egestas integer. Placerat vestibulum lectus mauris ultrices eros in\n"
"cursus. Viverra adipiscing at in tellus integer feugiat scelerisque. Malesuada\n"
"nunc vel risus commodo viverra maecenas accumsan. Mi tempus imperdiet nulla\n"
"malesuada pellentesque elit eget gravida cum. Lacus sed viverra tellus in hac.\n"
"Lobortis scelerisque fermentum dui faucibus in ornare quam viverra orci. Nisl\n"
"rhoncus mattis rhoncus urna neque viverra justo nec. Interdum varius sit amet\n"
"mattis. Aliquam faucibus purus in massa tempor nec.\n"
"\n"
"Augue interdum velit euismod in pellentesque. Ut pharetra sit amet aliquam. Est\n"
"pellentesque elit ullamcorper dignissim. Malesuada fames ac turpis egestas\n"
"maecenas pharetra convallis. Ornare arcu dui vivamus arcu felis bibendum ut\n"
"tristique et. Habitant morbi tristique senectus et netus et malesuada fames.\n"
"Turpis egestas maecenas pharetra convallis posuere morbi. Eu facilisis sed odio\n"
"morbi. Ac tincidunt vitae semper quis lectus nulla. Sit amet mauris commodo\n"
"quis imperdiet massa tincidunt. Non nisi est sit amet facilisis. Tortor id\n"
"aliquet lectus proin nibh nisl. Nec feugiat nisl pretium fusce. Adipiscing enim\n"
"eu turpis egestas. Sagittis eu volutpat odio facilisis mauris sit amet massa\n"
"vitae. Feugiat nisl pretium fusce id. Commodo nulla facilisi nullam vehicula\n"
"ipsum a arcu. Dictum at tempor commodo ullamcorper. Porta nibh venenatis cras\n"
"sed felis eget velit aliquet.\n"
"\n";

const size_t content_length = sizeof(content);

void *
read_file_to_str_job(void *data)
{
    const char *filename = (const char *)data;
    size_t size = 128, used = 0, n = 0;
    FILE *file;
    char *buffer = calloc(size, sizeof(char));

    printf("Opening file %s for reading\n", filename);
    file = fopen(filename, "r");
    if (file == NULL) {
        perror("Failed to open file for reading");
        return NULL;
    }

    while ( (n = fread(buffer+used, sizeof(char), size-used, file)) ) {
        used += n;
        if (used == size) {
            buffer = realloc(buffer, size * 2);
            memset(buffer + size, 0, size);
            size *= 2;
        }
    }

    if (ferror(file)) {
        perror("fread ended with error");
    }

    if (fclose(file) == EOF) {
        perror("Failed to close input file");
    }
    return buffer;
}

struct filename_and_contents {
    const char *filename;
    const char *contents;
};

void *
write_str_to_file_job(void *data)
{
    struct filename_and_contents *info = data;
    const char *filename = info->filename;
    const char *contents = info->contents;
    FILE *file; 
    size_t size = strlen(contents) + 1, written = 0;
    printf("Opening file %s for writing\n", filename);
    file = fopen(filename, "w");
    if (file == NULL) {
        perror("Failed to open file to writing");
        return NULL;
    }

    written = fwrite(contents, sizeof(char), size, file);

    if (written < size) {
        perror("Bytes written was fewer than content length");
        return NULL;
    }

    if (fclose(file) == EOF) {
        perror("Failed to close output file");
        return NULL;
    }

    return (void*)1;
}

int
main(int argc, char *argv[])
{
    lthread t;
    char *text;
    void *retval;
    char filename[] = "test_io.txt";
    struct filename_and_contents info = {
        .filename = filename,
        .contents = content,
    };

    (void) argc;
    (void) argv;

    lthread_init();

    lthread_create(&t, write_str_to_file_job, &info);

    lthread_join(t, &retval);
    if (retval == NULL) {
        printf("write_str_to_file_job failed\n");
        return 1;
    }

    lthread_create(&t, read_file_to_str_job, filename);

    lthread_join(t, (void**)&text);

    if (text != NULL) {
        printf("%s\n", text);
        if (strncmp(text, content, content_length) != 0) {
            printf("Content and read text don't match\n");
            return 1;
        }
    }
    else {
        printf("read_file_to_str_job failed\n");
        return 1;
    }
    free(text);

    if (remove(filename)) {
        perror("Failed to remove file");
        return 1;
    }

    return 0;
}
