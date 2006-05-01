#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "helpfunctions.h"
#include "zsfunctions.h"
#include "race-file.h"
#include "objects.h"
#include "macros.h"
#include "convert.h"
#include "dizreader.h"
#include "stats.h"
#include "ng-version.h"

#include "zsconfig.h"
#include "zsconfig.defaults.h"

#include "postdel.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef HAVE_STRLCPY
# include "strlcpy.h"
#endif

int 
main(int argc, char **argv)
{
	char		fileext[4];
	char		*name_p = 0;
	char		*temp_p;
	char		*target = NULL;
	char		*fname;
	char		*env_user;
	char		*env_group;
	char	        *inc_point[2];
	int		n;
	unsigned char	empty_dir = 0;
	unsigned char	incomplete = 0;
	
	GLOBAL		g;
	
	DIR		*dir, *parent;

	if (argc == 1) {
		d_log(1, "postdel: no param specified\n");
		return 0;
	}

	if ((int)strlen(argv[1]) < 6 || strncmp(argv[1], "DELE ", 5)) {
		printf("pzs-ng postdel script.\n");
		printf(" - this is supposed to be run from glftpd.\n");
		printf(" - if you wish to run it yourself from chroot, \n");
		printf(" - use /bin/postdel \"DELE <filename>\"\n");
		printf(" - thank you. (remember the quotes!)\n");
		return 0;
	}

	d_log(1, "postdel: Project-ZS Next Generation (pzs-ng) v%s debug log for postdel.\n", NG_VERSION);

#ifdef _ALT_MAX
	d_log(1, "postdel: PATH_MAX not found - using predefined settings! Please report to the devs!\n");
#endif

	fname = argv[1] + 5;	/* This way we simply skip the required
				 * 'DELE'-part of the argument (so we get
				 * filename) */

	d_log(1, "postdel: Reading user name from env\n");
	if ((env_user = getenv("USER")) == NULL) {
		d_log(1, "postdel: postdel: Could not find environment variable 'USER', setting value to 'Nobody'\n");
		env_user = "Nobody";
	}
	d_log(1, "postdel: Reading group name from env\n");
	if ((env_group = getenv("GROUP")) == NULL) {
		d_log(1, "postdel: Could not find environment variable 'GROUP', setting value to 'NoGroup'\n");
		env_group = "NoGroup";
	}
#if ( program_uid > 0 )
	d_log(1, "postdel: Trying to change effective gid\n");
	setegid(program_gid);
	d_log(1, "postdel: Trying to change effective uid\n");
	seteuid(program_uid);
#endif

	if (!strcmp(fname, "debug"))
		d_log(1, "postdel: Reading directory structure\n");

	dir = opendir(".");
	parent = opendir("..");

	if (fileexists(fname)) {
		d_log(1, "postdel: File (%s) still exists\n", fname);
#if (remove_dot_debug_on_delete == TRUE)
		if (strcmp(fname, "debug"))
			unlink(fname);
#endif
		closedir(dir);
		closedir(parent);
		return 0;
	}
	umask(0666 & 000);

	d_log(1, "postdel: Clearing arrays\n");
	bzero(&g.v.total, sizeof(struct race_total));
	g.v.misc.slowest_user[0] = 30000;
	g.v.misc.fastest_user[0] = 0;

	g.v.misc.write_log = TRUE;

	/* YARR; THE PAIN OF MAGIC NUMBERS! */
	d_log(1, "postdel: Copying env/predefined username to g.v. (%s)\n", env_user);
	strlcpy(g.v.user.name, env_user, 24);
	
	d_log(1, "postdel: Copying env/predefined groupname to g.v. (%s)\n", env_group);
	strlcpy(g.v.user.group, env_group, 24);
	g.v.user.group[23] = 0;

	d_log(1, "postdel: File to remove is: %s\n", fname);

	if (!*g.v.user.group)
		memcpy(g.v.user.group, "NoGroup", 8);

	getcwd(g.l.path, PATH_MAX);

	d_log(1, "postdel: Creating directory to store racedata in\n");
	maketempdir(g.l.path);

	if (matchpath(nocheck_dirs, g.l.path) || matchpath(speedtest_dirs, g.l.path) || (!matchpath(zip_dirs, g.l.path) && !matchpath(sfv_dirs, g.l.path) && !matchpath(group_dirs, g.l.path))) {
		d_log(1, "postdel: Dir matched with nocheck_dirs, or is not in the zip/sfv/group-dirs\n");
		d_log(1, "postdel: Freeing memory and exiting\n");
		ng_free(g.ui);
		ng_free(g.gi);

		if (remove_dot_debug_on_delete)
			unlink(".debug");

		return 0;

	}
	g.l.race = ng_realloc(g.l.race, n = (int)strlen(g.l.path) + 10 + sizeof(storage), 1, 1, 1);
	g.l.sfv = ng_realloc(g.l.sfv, n, 1, 1, 1);
	g.l.leader = ng_realloc(g.l.leader, n, 1, 1, 1);
	target = ng_realloc(target, 4096, 1, 1, 1);

	if (getenv("SECTION") == NULL)
		sprintf(g.v.sectionname, "DEFAULT");
	else
		snprintf(g.v.sectionname, 127, getenv("SECTION"));

	d_log(1, "postdel: Copying data &g.l into memory\n");
	strlcpy(g.v.file.name, fname, NAME_MAX);
	sprintf(g.l.sfv, storage "/%s/sfvdata", g.l.path);
	sprintf(g.l.leader, storage "/%s/leader", g.l.path);
	sprintf(g.l.race, storage "/%s/racedata", g.l.path);

	d_log(1, "postdel: Caching release name\n");
	getrelname(&g);
	d_log(1, "postdel: DEBUG 0: incomplete: '%s', path: '%s'\n", g.l.incomplete, g.l.path);

	d_log(1, "postdel: Parsing file extension from filename...\n");

	temp_p = find_last_of(g.v.file.name, ".");

	if (*temp_p != '.') {
		d_log(1, "postdel: Got: no extension\n");
		temp_p = name_p;
	} else {
		d_log(1, "postdel: Got: %s\n", temp_p);
		temp_p++;
	}
	name_p++;

	if (temp_p) {
		if (sizeof(temp_p) - 4 > 0)
			temp_p = temp_p + sizeof(temp_p) - 4;
		snprintf(fileext, 4, "%s", temp_p);
	} else
		*fileext = '\0';

	switch (get_filetype_postdel(&g, fileext)) {
	case 0:
		d_log(1, "postdel: File type is: ZIP\n");
//		if (matchpath(zip_dirs, g.l.path)) {
//			if (matchpath(group_dirs, g.l.path)) {
//				g.v.misc.write_log = 0;
//			} else {
//				g.v.misc.write_log = 1;
//			}
//		} else if (matchpath(sfv_dirs, g.l.path) && strict_path_match) {
		if (!matchpath(sfv_dirs, g.l.path) && !strict_path_match &&
			!matchpath(group_dirs, g.l.path)) {
//				g.v.misc.write_log = 0;
//			} else {
			d_log(1, "postdel: Directory matched with sfv_dirs\n");
			break;
//			}
		}

		if (!fileexists("file_id.diz")) {
			temp_p = findfileext(".", ".zip");
			if (temp_p != NULL) {
				d_log(1, "postdel: file_id.diz does not exist, trying to extract it from %s\n", temp_p);
				execute(4, unzip_bin, "-qqjnCLL", temp_p, "file_id.diz");
				chmod("file_id.diz", 0666);
			}
		}
		d_log(1, "postdel: Reading diskcount from diz\n");
		g.v.total.files = read_diz("file_id.diz");
		if (g.v.total.files == 0) {
			d_log(1, "postdel: Could not get diskcount from diz\n");
			g.v.total.files = 1;
			
		}
		g.v.total.files_missing = g.v.total.files;

		d_log(1, "postdel: Reading race data from file to memory\n");
		readrace(g.l.race, &g.v, g.ui, g.gi);

		d_log(1, "postdel: Caching progress bar\n");
		buffer_progress_bar(&g.v);

		if (del_completebar) {
			d_log(1, "postdel: Removing old complete bar, if any\n");
			removecomplete();
		}
		if (g.v.total.files_missing < 0) {
			g.v.total.files -= g.v.total.files_missing;
			g.v.total.files_missing = 0;
		}
		if (!g.v.total.files_missing) {
			d_log(1, "postdel: Creating complete bar\n");
			createstatusbar(convert(&g.v, g.ui, g.gi, zip_completebar));
		} else if (g.v.total.files_missing < g.v.total.files) {
			if (g.v.total.files_missing == 1) {
				d_log(1, "postdel: Writing INCOMPLETE to %s\n", log);
				writelog(&g, convert(&g.v, g.ui, g.gi, incompletemsg), general_incomplete_type);
			}
			incomplete = 1;
		} else if (!findfileextcount(".", ".sfv"))
			empty_dir = 1;
		remove_from_race(g.l.race, g.v.file.name);
		break;
	case 1: /* SFV */
		d_log(1, "postdel: Reading file count from sfvdata\n");
		readsfv(g.l.sfv, &g.v, 0);

		if (fileexists(g.l.race)) {
			d_log(1, "postdel: Reading race data from file to memory\n");
			readrace(g.l.race, &g.v, g.ui, g.gi);
		}
		d_log(1, "postdel: Caching progress bar\n");
		buffer_progress_bar(&g.v);

		if ((g.v.total.files_missing == g.v.total.files) && !findfileextcount(".", ".sfv"))
			empty_dir = 1;
		d_log(1, "postdel: SFV was removed - removing progressbar/completebar and -missing pointers.\n");
		if (del_completebar)
			removecomplete();

		d_log(1, "postdel: removing files created\n");
		if (fileexists(g.l.sfv)) {
			delete_sfv(g.l.sfv);
			unlink(g.l.sfv);	
		}
		if (g.l.nfo_incomplete)
			unlink(g.l.nfo_incomplete);
		if (g.l.incomplete)
			unlink(g.l.incomplete);
#if (sfv_cleanup)
		d_log(1, "postdel: removing backup sfv.\n");
		fname = 0;
		fname = ng_realloc(fname, PATH_MAX, 1, 1, 1);
		sprintf(fname, "%s/%s/%s", storage, g.l.path, g.v.file.name);
		unlink(fname);
		ng_free(fname);
#endif
		d_log(1, "postdel: removing progressbar, if any\n");
		move_progress_bar(1, &g.v, g.ui, g.gi);
		break;
	case 3:
		if (del_completebar) {
			d_log(1, "postdel: Removing old complete bar, if any\n");
			removecomplete();
		}

		if (fileexists(g.l.race)) {
			d_log(1, "postdel: Reading race data from file to memory\n");
			readrace(g.l.race, &g.v, g.ui, g.gi);
		} else if (!findfileextcount(".", ".sfv"))
			empty_dir = 1;
		if (fileexists(g.l.sfv)) {
#if ( create_missing_files == TRUE )
#if ( sfv_cleanup_lowercase == TRUE )
			strtolower(g.v.file.name);
#endif
			create_missing(g.v.file.name);
#endif
			d_log(1, "postdel: Reading file count from SFV\n");
			readsfv(g.l.sfv, &g.v, 0);

			d_log(1, "postdel: Caching progress bar\n");
			buffer_progress_bar(&g.v);
		}
		if (g.v.total.files_missing < g.v.total.files) {
			if (g.v.total.files_missing == 1) {
				d_log(1, "postdel: Writing INCOMPLETE to %s\n", log);
				writelog(&g, convert(&g.v, g.ui, g.gi, incompletemsg), general_incomplete_type);
			}
			incomplete = 1;
		} else {
			d_log(1, "postdel: Removing old race data\n");
			unlink(g.l.race);
			if (!findfileext(".", ".sfv")) {
				empty_dir = 1;
			} else {
				incomplete = 1;
			}
		}
		remove_from_race(g.l.race, g.v.file.name);
		break;
	case 4:
		if (!fileexists(g.l.race) && !findfileextcount(".", ".sfv"))
			empty_dir = 1;
		break;
	case 255:
		if (!fileexists(g.l.race) && !findfileextcount(".", ".sfv"))
			empty_dir = 1;
		break;
	case 2:
		if (!fileexists(g.l.race) && !findfileextcount(".", ".sfv"))
			empty_dir = 1;
		else {
			d_log(1, "postdel: Reading race data from file to memory\n");
			readrace(g.l.race, &g.v, g.ui, g.gi);
			d_log(1, "postdel: Caching progress bar\n");
			buffer_progress_bar(&g.v);
			if ((g.v.total.files_missing == g.v.total.files) && !findfileextcount(".", ".sfv")) {
				empty_dir = 1;
			}
		}
		break;
	}

	if (empty_dir == 1 && !findfileext(".", ".sfv")) {
		
		d_log(1, "postdel: Removing all files and directories created by zipscript\n");
		if (del_completebar)
			removecomplete();
		if (fileexists(g.l.sfv))
			delete_sfv(g.l.sfv);
		if (g.l.nfo_incomplete)
			unlink(g.l.nfo_incomplete);
		if (g.l.incomplete)
			unlink(g.l.incomplete);
#if (sfv_cleanup)
		d_log(1, "postdel: removing backup sfv.\n");
		fname = 0;
		fname = ng_realloc(fname, PATH_MAX, 1, 1, 1);
		sprintf(fname, "%s/%s/%s", storage, g.l.path, g.v.file.name);
		unlink(fname);
		ng_free(fname);
#endif
		unlink("file_id.diz");
		unlink(g.l.sfv);
		unlink(g.l.race);
		unlink(g.l.leader);
		
		move_progress_bar(1, &g.v, g.ui, g.gi);
		
#if (remove_dot_files_on_delete == TRUE)
		removedotfiles(dir);
#endif

	}

	if (incomplete == 1 && g.v.total.files > 0) {

		getrelname(&g);
		if (g.l.nfo_incomplete) {
			if (findfileext(".", ".nfo")) {
				d_log(1, "postdel: Removing missing-nfo indicator (if any)\n");
				remove_nfo_indicator(&g);
			} else {
				if (check_for_missing_nfo_filetypes) {
					switch (g.v.misc.release_type) {
						case RTYPE_RAR:
							if (strcomp(check_for_missing_nfo_filetypes, "rar"))
								n = 1;
							break;
						case RTYPE_OTHER:
							if (strcomp(check_for_missing_nfo_filetypes, "other"))
								n = 1;
							break;
						case RTYPE_AUDIO:
							if (strcomp(check_for_missing_nfo_filetypes, "audio"))
								n = 1;
							break;
						case RTYPE_VIDEO:
							if (strcomp(check_for_missing_nfo_filetypes, "video"))
								n = 1;
							break;
						case RTYPE_NULL:
							if (strcomp(check_for_missing_nfo_filetypes, "zip"))
								n = 1;
							break;
					}
				}
				if ((matchpath(check_for_missing_nfo_dirs, g.l.path) || n) && (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs)) {
					if (!g.l.in_cd_dir) {
						d_log(1, "postdel: Creating missing-nfo indicator %s.\n", g.l.nfo_incomplete);
						create_incomplete_nfo();
					} else {
						if (findfileext("..", ".nfo")) {
							d_log(1, "postdel: Removing missing-nfo indicator (if any)\n");
							remove_nfo_indicator(&g);
						} else {
							d_log(1, "postdel: Creating missing-nfo indicator (base) %s.\n", g.l.nfo_incomplete);
		 					/* This is not pretty, but should be functional. */
							if ((inc_point[0] = find_last_of(g.l.path, "/")) != g.l.path)
								*inc_point[0] = '\0';
							if ((inc_point[1] = find_last_of(g.v.misc.release_name, "/")) != g.v.misc.release_name)
								*inc_point[1] = '\0';
							create_incomplete_nfo();
							if (*inc_point[0] == '\0')
								*inc_point[0] = '/';
							if (*inc_point[1] == '\0')
								*inc_point[1] = '/';
						}
					}
				}
			}
		}
		if (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs) {
			d_log(1, "postdel: Creating incomplete indicator\n");
			d_log(1, "postdel:    incomplete: '%s', path: '%s'\n", g.l.incomplete, g.l.path);
			create_incomplete();
		}
		d_log(1, "postdel: Moving progress bar\n");
		move_progress_bar(0, &g.v, g.ui, g.gi);
	}
	
	d_log(1, "postdel: Releasing memory and exiting.\n");
	closedir(dir);
	closedir(parent);
	ng_free(target);
	ng_free(g.l.race);
	ng_free(g.l.sfv);
	ng_free(g.l.leader);

	d_log(1, "postdel: Exit 0\n");

	if ((empty_dir == 1) && (fileexists(".debug")) && (remove_dot_debug_on_delete == TRUE))
		unlink(".debug");

	return 0;
}

unsigned char 
get_filetype_postdel(GLOBAL *g, char *ext)
{
	if (!(*ext))
		return 255;
	if (!strcasecmp(ext, "sfv"))
		return 1;
	if (!clear_file(g->l.race, g->v.file.name))
		return 4;
	if (!strcasecmp(ext, "zip"))
		return 0;
	if (!strcasecmp(ext, "nfo"))
		return 2;
	if (!strcomp(ignored_types, ext) || !strcomp(allowed_types, ext))
		return 3;

	return 255;
}