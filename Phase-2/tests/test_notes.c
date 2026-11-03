/* Exercises real note storage against a private temporary directory. */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "notes_service.h"

int main(void)
{
    char root[] = "/tmp/blackhand-notes-XXXXXX";
    assert(mkdtemp(root));
    assert(chdir(root) == 0);
    assert(mkdir("notes", 0700) == 0);
    notes_service_init();
    Note *first = notes_service_create("First", "original");
    assert(first);
    Note *second = notes_service_create("Second", "second");
    assert(second);

    /* A close/flush failure must not publish a note as saved. */
    struct rlimit original, limited;
    assert(getrlimit(RLIMIT_FSIZE, &original) == 0);
    limited = original;
    limited.rlim_cur = 64;
    signal(SIGXFSZ, SIG_IGN);
    assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    char large[1024]; memset(large, 'x', sizeof(large) - 1); large[1023] = '\0';
    Note *failed = notes_service_create("Too large", large);
    assert(setrlimit(RLIMIT_FSIZE, &original) == 0);
    assert(failed == NULL);
    assert(notes_service_note_count() == 2);

    /* A failed update must preserve both old disk contents and list order. */
    Note candidate = *first;
    candidate.content = large;
    assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    int result = notes_service_update_note(&candidate);
    assert(setrlimit(RLIMIT_FSIZE, &original) == 0);
    assert(result != 0);
    assert(strcmp(first->content, "original") == 0);
    assert(notes_service_list_all(NULL)[0] == second);
    FILE *file = fopen("notes/First.md", "r"); assert(file);
    char text[32]; assert(fgets(text, sizeof(text), file)); fclose(file);
    assert(strcmp(text, "original") == 0);

    /* Failure to remove the backing path must preserve the record. */
    assert(unlink("notes/First.md") == 0);
    assert(mkdir("notes/First.md", 0700) == 0);
    file = fopen("notes/First.md/keep", "w"); assert(file); fclose(file);
    assert(notes_service_delete_note(first) != 0);
    assert(notes_service_note_count() == 2);
    assert(unlink("notes/First.md/keep") == 0);
    assert(rmdir("notes/First.md") == 0);
    assert(notes_service_delete_note(first) == 0); /* already absent is idempotent */
    assert(notes_service_delete_note(second) == 0);
    notes_service_shutdown();
    puts("PASS: note write failure, atomic update, order, and deletion failure");
    return 0;
}
