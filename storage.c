// #define __USE_XOPEN 1
// #define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#include <ctype.h>
#include "utstring.h"
#include "config.h"
#include "storage.h"
#include "geohash.h"

#include "udata.h"

int ghash_readcache(struct udata *ud, char *ghash, UT_string *addr, UT_string *cc);

void get_geo(JsonNode *o, char *ghash)
{
	static UT_string *addr = NULL, *cc = NULL;
	static struct udata udata;

	/* FIXME!!!! */
	udata.usefiles = 1;

	utstring_renew(addr);
	utstring_renew(cc);

	if (ghash_readcache(&udata, ghash, addr, cc) == 1) {
		json_append_member(o, "addr", json_mkstring(utstring_body(addr)));
		json_append_member(o, "cc", json_mkstring(utstring_body(cc)));
	}
}

/*
 * `s' has a time string in it. Try to convert into time_t
 * using a variety of formats from higher to lower precision.
 * Return 1 on success, 0 on failure.
 */

static int str_time_to_secs(char *s, time_t *secs)
{
	static char **f, *formats[] = {
			"%Y-%m-%dT%H:%M:%S",
			"%Y-%m-%dT%H:%M",
			"%Y-%m-%dT%H",
			"%Y-%m-%d",
			"%Y-%m",
			NULL
		};
	struct tm tm;
	int success = 0;

	memset(&tm, 0, sizeof(struct tm));
	tm.tm_isdst = 0; 		/* A negative value for tm_isdst causes
					 * the mktime() function to attempt to
					 * divine whether summer time is in
					 * effect for the specified time. */
	for (f = formats; f && *f; f++) {
		if (strptime(s, *f, &tm) != NULL) {
			success = 1;
			fprintf(stderr, "str_time_to_secs succeeds with %s\n", *f);
			break;
		}
	}

	if (!success)
		return (0);

	// tm.tm_mday = tm.tm_hour = 0;
	// tm.tm_hour = tm.tm_min = tm.tm_sec = 1;
	*secs = mktime(&tm);
	printf("str_time_to_secs: %s becomes %04d-%02d-%02d %02d:%02d:%02d\n",
		s,
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);
	return (1);
}

int make_times(char *time_from, time_t *s_lo, char *time_to, time_t *s_hi)
{
	time_t now;

	setenv("TZ", "UTC", 1);  // FIXME: really?

	time(&now);
	if (!time_from || !*time_from) {
		*s_lo = now - (60 * 60 * 6);
	} else {
		if (str_time_to_secs(time_from, s_lo) == 0)
			return (0);
	}

	if (!time_to || !*time_to) {
		*s_hi = now;
	} else {
		if (str_time_to_secs(time_to, s_hi) == 0)
			return (0);
	}
	return (*s_lo > *s_hi ? 0 : 1);
}

/*
 * List the directories in the directory at `path' and put
 * the names into a JSON array which is in obj.
 */

static void ls(char *path, JsonNode *obj)
{
        DIR *dirp;
        struct dirent *dp;
	JsonNode *jarr = json_mkarray();

	printf("opendir %s\n", path);
        if ((dirp = opendir(path)) == NULL) {
		json_append_member(obj, "error", json_mkstring("Cannot open requested directory"));
                return;
        }

        while ((dp = readdir(dirp)) != NULL) {
                if ((*dp->d_name != '.') && (dp->d_type == DT_DIR)) {
                        // char *s = strdup(dp->d_name);
			json_append_element(jarr, json_mkstring(dp->d_name));
                }
        }

	json_append_member(obj, "results", jarr);
        closedir(dirp);
}

/*
 * List the files (glob pattern) in the directory at `pathpat' and
 * put the names into a new JSON array in obj. Filenames (2015-08.rec)
 * are checked whether they fall (time-wise) into the seconds between
 * s_lo and s_hi.
 */

static time_t t_lo, t_hi;	/* must be global so that filter() can access them */

static int filter_filename(const struct dirent *d)
{
	struct tm tmfile, *tm;
	int lo_months, hi_months, file_months;

	/* if the filename doesn't look like YYYY-MM.rec we can safely ignore it.
	 * Needs modifying after the year 2999 ;-) */
	if (fnmatch("2[0-9][0-9][0-9]-[0-3][0-9].rec", d->d_name, 0) != 0)
		return (0);

	/* Try converting filename to seconds; normalize other bits of `tm' */
	memset(&tmfile, 0, sizeof(struct tm));
	if (strptime(d->d_name, "%Y-%m", &tmfile) == NULL) {
		fprintf(stderr, "filter: convert err");
		return (0);
	}
	file_months = (tmfile.tm_year + 1900) * 12 + tmfile.tm_mon;

	tm = gmtime(&t_lo);
	lo_months = (tm->tm_year + 1900) * 12 + tm->tm_mon;

	tm = gmtime(&t_hi);
	hi_months = (tm->tm_year + 1900) * 12 + tm->tm_mon;

	/*
	printf("filter: file %s has %04d-%02d-%02d %02d:%02d:%02d\n",
		d->d_name,
		tmfile.tm_year + 1900, tmfile.tm_mon + 1, tmfile.tm_mday,
		tmfile.tm_hour, tmfile.tm_min, tmfile.tm_sec);
	*/

	if (file_months >= lo_months && file_months <= hi_months) {
		// fprintf(stderr, "filter: returns: %s\n", d->d_name);
		return (1);
	}
	return (0);
}

static int cmp( const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}

#if 0
static time_t month_part(time_t secs)
{
	struct tm *tm;

	tm = gmtime(&secs);
	// tm->tm_mday = 1;
	// tm->tm_min = tm->tm_sec = 1;
	tm->tm_isdst = -1;

	printf("month_part: s becomes %04d-%02d-%02d %02d:%02d:%02d\n",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	return mktime(tm);

}
#endif

static void lsscan(char *pathpat, time_t s_lo, time_t s_hi, JsonNode *obj)
{
	struct dirent **namelist;
	int i, n;
	JsonNode *jarr = json_mkarray();
	static UT_string *path = NULL;

	utstring_renew(path);

	/* Set global t_ values */
	t_lo = s_lo; //month_part(s_lo);
	t_hi = s_hi; //month_part(s_hi);

	if ((n = scandir(pathpat, &namelist, filter_filename, cmp)) < 0) {
		json_append_member(obj, "error", json_mkstring("Cannot lsscan requested directory"));
                return;
	}

	for (i = 0; i < n; i++) {
		utstring_clear(path);
		utstring_printf(path, "%s/%s", pathpat, namelist[i]->d_name);
		json_append_element(jarr, json_mkstring(utstring_body(path)));
		free(namelist[i]);
	}
	free(namelist);

	json_append_member(obj, "results", jarr);
}

/*
 * If `user' and `device' are both NULL, return list of users.
 * If `user` is specified, and device is NULL, return user's devices
 * If both user and device are specified, return list of .rec files;
 * in that case, limit with `s_lo` and `s_hi`
 */

JsonNode *lister(char *user, char *device, time_t s_lo, time_t s_hi)
{
	JsonNode *json = json_mkobject();
	UT_string *path = NULL;
	char *bp;

	utstring_renew(path);

	// printf("%ld %ld\n", s_lo, s_hi);

	for (bp = user; bp && *bp; bp++) {
		if (isupper(*bp))
			*bp = tolower(*bp);
	}
	for (bp = device; bp && *bp; bp++) {
		if (isupper(*bp))
			*bp = tolower(*bp);
	}

	if (!user && !device) {
		utstring_printf(path, "%s/rec", STORAGEDIR);
		ls(utstring_body(path), json);
	} else if (!device) {
		utstring_printf(path, "%s/rec/%s", STORAGEDIR, user);
		ls(utstring_body(path), json);
	} else {
		utstring_printf(path, "%s/rec/%s/%s",
			STORAGEDIR, user, device);
		lsscan(utstring_body(path), s_lo, s_hi, json);
	}

	return (json);
}

/*
 * Read the file at `filename' (- is stdin) and store location
 * objects at the JSON array `arr`. `obj' is a JSON object which
 * contains `arr'.
 */

void locations(char *filename, JsonNode *obj, JsonNode *arr)
{
	JsonNode *o, *json, *j;
	FILE *fp;
	int doclose;
	char buf[BUFSIZ], **element;
	long counter = 0L;
	static char *numbers[] = { "lat", "lon", "batt", "vel", "cog", "tst", "alt", "dist", "trip", NULL };
	static char *strings[] = { "tid", "t", NULL };
	extern int errno;


	if (!strcmp(filename, "-")) {
		fp = stdin;
		doclose = 0;
	} else {
		if ((fp = fopen(filename, "r")) == NULL) {
			fprintf(stderr, "Cannot open %s: %s\n", filename, strerror(errno));
			return;
		}
		doclose = 1;
	}

	/* Initialize our counter to what the JSON obj currently has */

	if ((j = json_find_member(obj, "count")) != NULL) {
		counter = j->number_;
		json_delete(j);
	}


	while (fgets(buf, sizeof(buf)-1, fp) != NULL) {
		char *bp, *ghash;
		double lat, lon;

		if ((bp = strstr(buf, "Z\t* ")) != NULL) {
			if ((bp = strrchr(bp, '\t')) == NULL) {
				continue;
			}
			++counter;
			json = json_decode(bp + 1);
			if (json == NULL) {
				puts("Cannot decode JSON");
				continue;
			}

			o = json_mkobject();

			/* Start adding stuff to the object, then copy over the elements
			 * from the decoded original locations. */

			for (element = numbers; element && *element; element++) {
				if ((j = json_find_member(json, *element)) != NULL) {
					if (j->tag == JSON_NUMBER) {
						json_append_member(o, *element, json_mknumber(j->number_));
					} else {
						double d = atof(j->string_);
						json_append_member(o, *element, json_mknumber(d));
					}
				}
			}

			for (element = strings; element && *element; element++) {
				if ((j = json_find_member(json, *element)) != NULL) {
					json_append_member(o, *element, json_mkstring(j->string_));
				}
			}

			lat = lon = 0.0;
			if ((j = json_find_member(o, "lat")) != NULL) {
				lat = j->number_;
			}
			if ((j = json_find_member(o, "lon")) != NULL) {
				lon = j->number_;
			}

			ghash = geohash_encode(lat, lon, GEOHASH_PREC);
			json_append_member(o, "ghash", json_mkstring(ghash));

			get_geo(o, ghash);


			json_append_element(arr, o);
		}
	}

	/* Add the counter back into `obj' */

	json_append_member(obj, "count", json_mknumber(counter));


	if (doclose)
		fclose(fp);

}
