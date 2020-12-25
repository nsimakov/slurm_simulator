#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "sim/sim.h"

int sim_n_users = 0;
char **sim_user = NULL;
uid_t *sim_user_id = NULL;
char **sim_groupname = NULL;
gid_t *sim_group_id = NULL;
struct passwd *sim_passwd = NULL;

char default_passwd[]="hashed passphrase";
char default_dir[]="/home/user";
char default_shell[]="/bin/bash";

int sim_n_groups = 0;
struct group *sim_group = NULL;

int __real_getpwnam_r (const char *__restrict __name,
		       struct passwd *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct passwd **__restrict __result);

int __wrap_getpwnam_r (const char *__restrict __name,
		       struct passwd *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct passwd **__restrict __result)
{
	int iuser;
	for(iuser=0;iuser<sim_n_users;++iuser){
		if(xstrcmp(sim_user[iuser], __name) == 0) {
			*__result = sim_passwd + iuser;
			return 0;
		}
	}
	return __real_getpwnam_r(__name,__resultbuf,__buffer,__buflen,__result);
}

int __real_getpwuid_r (__uid_t __uid,
		       struct passwd *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct passwd **__restrict __result);
int __wrap_getpwuid_r (__uid_t __uid,
		       struct passwd *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct passwd **__restrict __result)
{
	int iuser;
	for(iuser=0;iuser<sim_n_users;++iuser){
		if(sim_user_id[iuser] == __uid) {
			*__result = sim_passwd + iuser;
			return 0;
		}
	}
	return __real_getpwuid_r(__uid,__resultbuf,__buffer,__buflen,__result);
}

int __real_getgrnam_r (const char *__restrict __name,
		       struct group *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct group **__restrict __result);
int __wrap_getgrnam_r (const char *__restrict __name,
		       struct group *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct group **__restrict __result)
{
	for(int igroup = 0; igroup < sim_n_groups; ++igroup) {
		if(xstrcmp(sim_group[igroup].gr_name , __name) == 0) {
			*__result = sim_group + igroup;
			return 0;
		}
	}
	return __real_getgrnam_r(__name, __resultbuf, __buffer, __buflen, __result);
}

int __real_getgrgid_r (__gid_t __gid, struct group *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct group **__restrict __result);
int __wrap_getgrgid_r (__gid_t __gid, struct group *__restrict __resultbuf,
		       char *__restrict __buffer, size_t __buflen,
		       struct group **__restrict __result)
{
	for(int igroup = 0; igroup < sim_n_groups; ++igroup) {
		if(sim_group[igroup].gr_gid == __gid) {
			*__result = sim_group + igroup;
			return 0;
		}
	}
	return __real_getgrgid_r(__gid, __resultbuf, __buffer, __buflen, __result);
}


extern int sim_read_users(void) {
	int i;
	char *users_path = NULL;
	users_path = get_extra_conf_path("users.sim");

	FILE *f_in = fopen(users_path, "rt");
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	if (f_in == NULL) {
		error("Can not open users.sim file %s!", users_path);
		exit(1);
	}


	// count users
	sim_n_users = 0;
	while ((read = getline(&line, &len, f_in)) != -1) {
		int comment = 0;
		int not_white_space=0;
		for(int i=0; i < read;++i) {
			if(!(line[i]==' ' || line[i]=='\t' || line[i]=='\n')) {
				not_white_space += 1;
			}
			if(line[i]=='#' && not_white_space==0) {
				comment = 1;
			}
		}
		if(!comment && not_white_space>0) {
			printf("%s", line);
			sim_n_users += 1;
		}
	}
	rewind(f_in);

	// allocate arrays
	sim_user = xcalloc(sim_n_users,sizeof(char*));
	sim_user[0] = xcalloc(sim_n_users,32*sizeof(char));
	sim_user_id = xcalloc(sim_n_users,sizeof(uid_t));
	sim_groupname = xcalloc(sim_n_users,sizeof(char*));
	sim_groupname[0] = xcalloc(sim_n_users,32*sizeof(char));
	sim_group_id = xcalloc(sim_n_users,sizeof(gid_t));
	for(i=1;i<sim_n_users;++i){
		sim_user[i] = sim_user[0] + i*32;
		sim_groupname[i] = sim_groupname[0] + i*32;
	}
	sim_passwd = xcalloc(sim_n_users,sizeof(struct passwd));
	error("sizeof(struct passwd): %ld", sizeof(struct passwd));


	// set values
	int iuser=0;
	//int user_id,group_id;
	//char username[32], groupname[32];
	while ((read = getline(&line, &len, f_in)) != -1) {
		int comment = 0;
		int not_white_space=0;
		for(int i=0; i < read;++i) {
			if(!(line[i]==' ' || line[i]=='\t' || line[i]=='\n')) {
				not_white_space += 1;
			}
			if(line[i]=='#' && not_white_space==0) {
				comment = 1;
			}
		}
		if(!comment && not_white_space>0) {
			printf("%s", line);
			if(sscanf(line, "%32[^:]:%d:%32[^:]:%d", sim_user[iuser], sim_user_id + iuser,
					sim_groupname[iuser], sim_group_id + iuser)!=4) {
				error("can read users from line: %s", line);
			}
			iuser += 1;
		}
	}
	fclose(f_in);

	for(iuser=0;iuser<sim_n_users;++iuser) {
		sim_passwd[iuser].pw_name = sim_user[iuser];
		sim_passwd[iuser].pw_passwd = default_passwd;
		sim_passwd[iuser].pw_uid = sim_user_id[iuser];
		sim_passwd[iuser].pw_gid = sim_group_id[iuser];
		sim_passwd[iuser].pw_gecos = sim_user[iuser];
		sim_passwd[iuser].pw_dir = default_dir;
		sim_passwd[iuser].pw_shell = default_shell;
	}

	int igroup;
	int group_exists;
	sim_n_groups = 0;
	gid_t * tmp_gid = xcalloc(sim_n_users,sizeof(gid_t));
	for(iuser=0;iuser<sim_n_users;++iuser) {
		group_exists = 0;
		for(igroup = 0; igroup < sim_n_groups; ++igroup) {
			if(sim_group_id[iuser] == tmp_gid[igroup]) {
				group_exists=1;
				break;
			}
		}
		if(group_exists == 0) {
			tmp_gid[sim_n_groups] = sim_group_id[iuser];
			sim_n_groups++;
		}
	}

	sim_group = xcalloc(sim_n_groups,sizeof(struct group));
	for(igroup = 0; igroup < sim_n_groups; ++igroup) {
		int iuser_in_group;
		int users_in_group=0;
		for(iuser=0;iuser<sim_n_users;++iuser) {
			if(sim_group_id[iuser]==tmp_gid[igroup]) {
				users_in_group++;
				iuser_in_group=iuser;
			}
		}

		sim_group[igroup].gr_name = xstrdup(sim_groupname[iuser_in_group]);
		sim_group[igroup].gr_passwd = xstrdup("password");
		sim_group[igroup].gr_gid = tmp_gid[igroup];

		sim_group[igroup].gr_mem = xcalloc(users_in_group + 1, sizeof(char*));
		sim_group[igroup].gr_mem[users_in_group] = NULL;
		sim_group[igroup].gr_mem[0] = xcalloc(32*users_in_group, sizeof(char));
		for(i=1;i<users_in_group;++i){
			sim_group[igroup].gr_mem[i] = sim_group[igroup].gr_mem[0] + i*32;
		}
		iuser_in_group=0;
		for(iuser=0;iuser<sim_n_users;++iuser) {
			if(sim_group_id[iuser]==tmp_gid[igroup]) {
				strcat(sim_group[igroup].gr_mem[iuser_in_group], sim_user[iuser]);
				iuser_in_group++;
			}
		}
	}

	for(iuser=0;iuser<sim_n_users;++iuser){
		for(igroup = 0; igroup < sim_n_groups; ++igroup) {
			if(sim_group[igroup].gr_gid ==sim_group_id[iuser]) {
				if(xstrcmp(sim_group[igroup].gr_name , sim_groupname[iuser]) != 0) {
					error("group name for user %s doesn't match name with same gid from other users!", sim_user[iuser]);
				}
				break;
			}
		}
		printf("%5d %16s %7d %16s %7d\n", iuser,
				sim_user[iuser], sim_user_id[iuser],
				sim_groupname[iuser], sim_group_id[iuser]);
	}

	xfree(tmp_gid);

	return 0;
}

extern int sim_print_users(void)
{
	int iuser;
	printf("Users (from user.sim):\n");
	for(iuser=0;iuser<sim_n_users;++iuser){
		printf("%5d %16s %7d %16s %7d\n", iuser,
				sim_user[iuser], sim_user_id[iuser],
				sim_groupname[iuser], sim_group_id[iuser]);
	}
	printf("Users (passwd sim):\n");
	for(iuser=0;iuser<sim_n_users;++iuser){
		printf("%5d %16s %16s %7d %7d %16s %16s %16s\n", iuser,
				sim_passwd[iuser].pw_name,
				sim_passwd[iuser].pw_passwd,
				sim_passwd[iuser].pw_uid,
				sim_passwd[iuser].pw_gid,
				sim_passwd[iuser].pw_gecos,
				sim_passwd[iuser].pw_dir,
				sim_passwd[iuser].pw_shell);
	}
	printf("Groups (passwd sim):\n");
	int igroup;
	for(igroup = 0; igroup < sim_n_groups; ++igroup) {
		printf("%5d %16s %16s %7d\n", igroup,
				sim_group[igroup].gr_name,
				sim_group[igroup].gr_passwd,
				sim_group[igroup].gr_gid);
		printf("    ");
		for(iuser=0;sim_group[igroup].gr_mem[iuser];++iuser) {
			printf("%s ",sim_group[igroup].gr_mem[iuser]);
		}
		printf("\n");
	}
	return 0;
}

