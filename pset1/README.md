CS 61 Problem Set 1
===================

**Fill out both this file and `AUTHORS.md` before submitting.** We grade
anonymously, so put all personally identifying information, including
collaborators and citations, in `AUTHORS.md`.

Grading notes (if any)
----------------------
I tracked metadata externally using a hashmap. To bound the metadata from growing infinitely, we remove pointers when we free them. However, we don't delete them immediately; rather, we keep them in a list of constant size ordered by when they were freed and eventually delete them after the pointer gets removed from the list. This list allows us to keep a "recent record" of frees so we can detect double frees.



Extra credit attempted (if any)
-------------------------------
I implemented `61_realloc()` and added 3 tests testing m61_realloc. I also made `m61_print_heavy_hitter_report()` print out both frequent and heavy heavy-hitters.