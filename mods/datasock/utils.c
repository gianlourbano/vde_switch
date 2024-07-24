#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "debug.h"

char *vde_realpath(const char *name, char *resolved)
{
	char *dest, *buf=NULL, *extra_buf=NULL;
	const char *start, *end, *resolved_limit; 
	char *resolved_root = resolved + 1;
	char *ret_path = NULL;
	int num_links = 0;
	struct stat *pst = NULL;

	if (!name || !resolved)
	{
		errno = EINVAL;
		goto abort;
	}

	if (name[0] == '\0')
	{
		/* As per Single Unix Specification V2 we must return an error if
		   the name argument points to an empty string.  */
		errno = ENOENT;
		goto abort;
	}

	if ((buf=(char *)calloc(PATH_MAX, sizeof(char)))==NULL) {
		errno = ENOMEM;
		goto abort;
	}
	if ((extra_buf=(char *)calloc(PATH_MAX, sizeof(char)))==NULL) {
		errno = ENOMEM;
		goto abort;
	}
	if ((pst=(struct stat *)calloc(1, sizeof(struct stat)))==NULL) {
		errno = ENOMEM;
		goto abort;
	}

	resolved_limit = resolved + PATH_MAX;

	/* relative path, the first char is not '/' */
	if (name[0] != '/')
	{
		if (!getcwd(resolved, PATH_MAX))
		{
			resolved[0] = '\0';
			goto abort;
		}

		dest = strchr (resolved, '\0');
	}
	else
	{
		/* absolute path */
		dest = resolved_root;
		resolved[0] = '/';

		/* special case "/" */
		if (name[1] == 0)
		{
			*dest = '\0';
			ret_path = resolved;
			goto cleanup;
		}
	}

	/* now resolved is the current wd or "/", navigate through the path */
	for (start = end = name; *start; start = end)
	{
		int n;

		/* Skip sequence of multiple path-separators.  */
		while (*start == '/')
			++start;

		/* Find end of path component.  */
		for (end = start; *end && *end != '/'; ++end);

		if (end - start == 0)
			break;
		else if (end - start == 1 && start[0] == '.')
			/* nothing */;
		else if (end - start == 2 && start[0] == '.' && start[1] == '.')
		{
			/* Back up to previous component, ignore if at root already.  */
			if (dest > resolved_root)
				while ((--dest)[-1] != '/');
		}
		else
		{
			if (dest[-1] != '/')
				*dest++ = '/';

			if (dest + (end - start) >= resolved_limit)
			{
				errno = ENAMETOOLONG;
				if (dest > resolved_root)
					dest--;
				*dest = '\0';
				goto abort;
			}

			/* copy the component, don't use mempcpy for better portability */
			dest = (char*)memcpy(dest, start, end - start) + (end - start);
			*dest = '\0';

			/*check the dir along the path */
			if (lstat(resolved, pst) < 0)
				goto abort;
			else
			{
				/* this is a symbolic link, thus restart the navigation from
				 * the symlink location */
				if (S_ISLNK (pst->st_mode))
				{
					size_t len;

					if (++num_links > MAXSYMLINKS)
					{
						errno = ELOOP;
						goto abort;
					}

					/* symlink! */
					n = readlink (resolved, buf, PATH_MAX);
					if (n < 0)
						goto abort;

					buf[n] = '\0';

					len = strlen (end);
					if ((long) (n + len) >= PATH_MAX)
					{
						errno = ENAMETOOLONG;
						goto abort;
					}

					/* Careful here, end may be a pointer into extra_buf... */
					memmove (&extra_buf[n], end, len + 1);
					name = end = memcpy (extra_buf, buf, n);

					if (buf[0] == '/')
						dest = resolved_root;	/* It's an absolute symlink */
					else
						/* Back up to previous component, ignore if at root already: */
						if (dest > resolved + 1)
							while ((--dest)[-1] != '/');
				}
				else if (*end == '/' && !S_ISDIR(pst->st_mode))
				{
					errno = ENOTDIR;
					goto abort;
				}
				else if (*end == '/')
				{
					if (access(resolved, X_OK) != 0)
					{
						errno = EACCES;
						goto abort;
					}
				}
			}
		}
	}
	if (dest > resolved + 1 && dest[-1] == '/')
		--dest;
	*dest = '\0';

	ret_path = resolved;
	goto cleanup;

abort:
	ret_path = NULL;
cleanup:
	if (buf) free(buf);
	if (extra_buf) free(extra_buf);
	if (pst) free(pst);
	return ret_path;
}

int still_used(struct sockaddr_un *sun)
{
	int test_fd, ret = 1;

	if((test_fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0){
		ERROR("socket %s",strerror(errno));
		return(1);
	}
	if(connect(test_fd, (struct sockaddr *) sun, sizeof(*sun)) < 0){
		if(errno == ECONNREFUSED){
			if(unlink(sun->sun_path) < 0){
				ERROR("Failed to removed unused socket '%s': %s",
						sun->sun_path,strerror(errno));
			}
			ret = 0;
		}
		else ERROR("connect %s",strerror(errno));
	}
	close(test_fd);
	return(ret);
}

