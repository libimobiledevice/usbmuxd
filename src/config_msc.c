#include "config_msc.h"

char *dirname(char const *file)
{
	char* dir[256];
	_splitpath(file, 0, dir, 0, 0);
	return dir;
}