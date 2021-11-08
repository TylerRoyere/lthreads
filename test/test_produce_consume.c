#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lthread.h"

/* Ed, man! !man ed */
/* the ' ' in front of the '\n' for empty is lines is necesary, otherwise
 * strtok will skip it
 */
const char *content = 
"Note the consistent user interface and error reportage. Ed is generous enough\n"
"to flag errors, yet prudent enough not to overwhelm the novice with verbosity.\n"
" \n"
"'Ed is the standard text editor.'\n"
" \n"
"Ed, the greatest WYGIWYG editor of all.\n"
" \n"
"ED IS THE TRUE PATH TO NIRVANA! ED HAS BEEN THE CHOICE OF EDUCATED AND IGNORANT\n"
"ALIKE FOR CENTURIES! ED WILL NOT CORRUPT YOUR PRECIOUS BODILY FLUIDS!! ED IS\n"
"THE STANDARD TEXT EDITOR! ED MAKES THE SUN SHINE AND THE BIRDS SING AND THE\n"
"GRASS GREEN!!\n"
" \n"
"When I use an editor, I don't want eight extra KILOBYTES of worthless help\n"
"screens and cursor positioning code! I just want an EDitor!! Not a 'viitor'.\n"
"Not a 'emacsitor'. Those aren't even WORDS!!!! ED! ED! ED IS THE STANDARD!!!\n"
" \n"
"TEXT EDITOR.\n"
" \n"
"When IBM, in its ever-present omnipotence, needed to base their 'edlin' on a\n"
"Unix standard, did they mimic vi? No. Emacs? Surely you jest. They chose the\n"
"most karmic editor of all. The standard.\n"
" \n"
"Ed is for those who can remember what they are working on. If you are an idiot,\n"
"   you should use Emacs. If you are an Emacs, you should not be vi. If you use\n"
"   ED, you are on THE PATH TO REDEMPTION. THE SO-CALLED 'VISUAL' EDITORS HAVE\n"
"   BEEN PLACED HERE BY ED TO TEMPT THE FAITHLESS. DO NOT GIVE IN!!! THE MIGHTY\n"
"   ED HAS SPOKEN!!!\n";

struct list {
    char *content;
    struct list *next;
};

struct list *queue = NULL; 

void *
produce(void *data)
{
    (void)data;
    char *text = strdup(content);
    char *line = NULL;
    struct list **curr = &queue, *temp;

    /* Start off by allocating first node and starting tokenization */
    temp = calloc(1, sizeof(*temp));
    line = strtok(text, "\n");

    /* While there are content lines left */
    while ( line != NULL ) {
        /* Copy content */
        temp->content = strdup(line);

        /* Update previous node->next to point to new node */
        *curr = temp;
        /* Move previous node->next new node next pointer */
        curr = &temp->next;

        /* Allocate new node */
        temp = calloc(1, sizeof(*temp));
        
        /* Yield to be consumed */
        lthread_yield();

        /* Get next line */
        line = strtok(NULL, "\n");
    }

    /* Mark end of input */
    temp->content = NULL;
    *curr = temp;

    free(text);

    return NULL;
}

void *
consume(void *data)
{
    (void)data;

    size_t size = 128, used = 0, n = 0;
    char *buffer = calloc(size, sizeof(char));

    /* Wait for producer to start putting stuff in queue */
    while (queue == NULL) ;

    struct list *curr = queue;
    struct list *last = curr;

    /* Producer will indicate end of queue with NULL content */
    while (curr->content != NULL) {
        /* Append queue entry content and newline to buffer */
        n = strlen(curr->content);
        /* Allocate more space if necessary */
        if (n + used + 1 > size) {
            size += n + 1;
            size *= 2;
            buffer = realloc(buffer, size);
        }
        strncat(buffer, curr->content, n);
        strncat(buffer, "\n", 2);
        used += n + 1;

        /* Wait for the next entry to be written */
        while (curr->next == NULL) lthread_yield();

        /* Move to next queue entry */
        last = curr;
        curr = curr->next;

        /* Free entry that was just processed */
        free(last->content);
        free(last);
    }

    /* Free last entry */
    free(curr);

    /* Ensure the output is null terminated */
    buffer[used] = '\0';

    return buffer;
}

int main(int argc, char *argv[])
{
    (void)argc, (void)argv;

    char *text;

    lthread producer, consumer;

    lthread_init();

    lthread_create(&producer, produce, NULL);
    lthread_create(&consumer, consume, NULL);

    lthread_join(producer, NULL);
    lthread_join(consumer, (void**)&text);

    printf("%s\n", text);

    if (strcmp(text, content) != 0) {
        printf("Content doesn't match consumer output\n");
        return 1;
    }

    free(text);

    return 0;
}
