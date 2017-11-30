#include <dirent.h>
#include "ch_frb_l1.hpp"

#include <sys/stat.h>

using namespace std;

namespace ch_frb_l1 {
#if 0
}   // compiler pacifier
#endif


bool file_exists(const string &filename)
{
    struct stat s;

    int err = stat(filename.c_str(), &s);
    if (err >= 0)
        return true;
    if (errno == ENOENT)
        return false;

    throw runtime_error(filename + ": " + strerror(errno));
}


bool is_directory(const string &filename)
{
    struct stat s;

    int err = stat(filename.c_str(), &s);
    if (err < 0)
	throw runtime_error(filename + ": " + strerror(errno));

    return S_ISDIR(s.st_mode);
}


bool is_empty_directory(const string &dirname)
{
    DIR *dir = opendir(dirname.c_str());
    if (!dir)
	throw runtime_error(dirname + ": opendir() failed: " + strerror(errno));

    ssize_t name_max = pathconf(dirname.c_str(), _PC_NAME_MAX);
    name_max = min(name_max, (ssize_t)4096);

    vector<char> buf(sizeof(struct dirent) + name_max + 1);
    struct dirent *entry = reinterpret_cast<struct dirent *> (&buf[0]);
    
    for (;;) {
	struct dirent *result = nullptr;

	int err = readdir_r(dir, entry, &result);	
	if (err)
	    throw runtime_error(dirname + ": readdir_r() failed");
	if (!result)
	    return true;
	if (!strcmp(entry->d_name, "."))
	    continue;
	if (!strcmp(entry->d_name, ".."))
	    continue;
	
	return false;
    }
}


}   // namespace ch_frb_l1
