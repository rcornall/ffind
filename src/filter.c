/**
 * fuzzy filtering and ranking of list lines.
 */

#include <filter.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static bool has_upper(const char *s)
{
	for (; *s; s++)
		if (isupper((unsigned char)*s))
			return true;
	return false;
}

static char fold(char c, bool icase)
{
	return icase ? tolower((unsigned char)c) : c;
}

static bool word_start(const char *text, const char *t)
{
	return t == text || !isalnum((unsigned char)t[-1]) ||
	       (isupper((unsigned char)t[0]) && islower((unsigned char)t[-1]));
}

/*
 * subsequent fuzzy matching. smartcase: ignore case unless pat has uppercase.
 * score favours tight, consecutive matches starting on word boundaries.
 */
static bool fuzzy_match(const char *text, const char *pat, int *score) {
	bool icase = !has_upper(pat);
	int plen = strlen(pat);

	// forward: find where the first full match ends.
	const char *t, *end = NULL;
	int p = 0;
	for (t = text; *t; t++) {
		if (fold(*t, icase) == pat[p] && ++p == plen) {
			end = t;
			break;
		}
	}
	if (end == NULL)
		return false;

	// backward from the end: latest start gives the tightest window.
	const char *start = end;
	p = plen - 1;
	for (t = end; t >= text; t--) {
		if (fold(*t, icase) == pat[p] && --p < 0) {
			start = t;
			break;
		}
	}

	const char *prev = NULL;
	*score = 0;
	p = 0;
	for (t = start; t <= end; t++) {
		if (fold(*t, icase) == pat[p]) {
			*score += 16;
			if (prev && t == prev + 1)
				*score += 12;
			else if (word_start(text, t))
				*score += 8;
			prev = t;
			p++;
		} else {
			*score -= 3;
		}
	}
	return true;
}

/* plain substring matching, smartcase. */
static bool substr_match(const char *text, const char *pat) {
	bool icase = !has_upper(pat);
	for (; *text; text++) {
		const char *t = text, *p = pat;
		while (*t && *p && fold(*t, icase) == *p) {
			t++;
			p++;
		}
		if (*p == '\0')
			return true;
	}
	return false;
}

bool filter_match(const char *text, const char *pat, int *score) {
	char tokens[100];
	strncpy(tokens, pat, sizeof(tokens) - 1);
	tokens[sizeof(tokens) - 1] = '\0';

	*score = 0;
	char *saveptr;
	char *tok = strtok_r(tokens, " ", &saveptr);
	while (tok) {
		int s = 0;
		if (tok[0] == '!') {
			if (tok[1] && substr_match(text, tok + 1))
				return false;
		} else if (!fuzzy_match(text, tok, &s)) {
			return false;
		}
		*score += s;
		tok = strtok_r(NULL, " ", &saveptr);
	}
	return true;
}

static int *sort_score;

/* best score first, ties keep the original rg order. */
static int cmp_score(const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;
	if (sort_score[ia] != sort_score[ib])
		return sort_score[ib] - sort_score[ia];
	return ia - ib;
}

void filter_list(struct list *l, const char *pat, bool narrow)
{
	int line_no = 1;
	if (narrow) {
		// compact the map in place, line_no never passes i.
		for (int i = 1; i <= l->visible_lines; i++) {
			int idx = l->map_filtered_to_line[i];
			if (filter_match(l->buf[idx], pat, &l->score[idx])) {
				l->map_filtered_to_line[line_no] = idx;
				line_no++;
			}
		}
	} else {
		for (int i = 0; i < l->total_lines; i++) {
			if (filter_match(l->buf[i], pat, &l->score[i])) {
				l->map_filtered_to_line[line_no] = i;
				line_no++;
			}
		}
	}
	l->visible_lines = line_no - 1;

	if (l->visible_lines > 1) {
		sort_score = l->score;
		qsort(&l->map_filtered_to_line[1], l->visible_lines, sizeof(int), cmp_score);
	}
}

bool filter_narrows(const char *pat)
{
	// extending a !token excludes less, so needs a full rescan.
	const char *last = strrchr(pat, ' ');
	last = last ? last + 1 : pat;
	return !(last[0] == '!' && strlen(last) > 2);
}
