#ifndef FILTER_H
#define FILTER_H

/**
 * fuzzy filtering and ranking of list lines.
 */

#include <stdbool.h>
#include <list.h>

/**
 * match all space-separated fuzzy tokens against text (AND logic). !token
 * excludes lines containing token. score is higher for better matches.
 */
bool filter_match(const char *text, const char *pat, int *score);

/**
 * filter and rank the list by pat, best first. `narrow` when pat only got
 * stricter, so just the currently visible lines need checking.
 */
void filter_list(struct list *l, const char *pat, bool narrow);

/**
 * pat was just extended by one char: true if it only got stricter.
 */
bool filter_narrows(const char *pat);

#endif
