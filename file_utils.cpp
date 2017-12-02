#include <dirent.h>
#include <iomanip>
#include "ch_frb_l1.hpp"

#include <sys/stat.h>

using namespace std;

namespace ch_frb_l1 {
#if 0
}   // compiler pacifier
#endif


// acqname_to_filename_pattern(devname, acqname, beam_ids)
//
// The L1 server defines a parameter 'stream_acqname'.  If this is a nonempty string,
// then the L1 server will stream all of its incoming data to filenames of the form
//
//   /local/acq_data/(ACQNAME)/beam_(BEAM)/chunk_(CHUNK).msg            (*)
//   /frb-archiver-(STREAM)/(ACQNAME)/beam_(BEAM)/chunk_(CHUNK).msg     (**)
//
// This filename pattern is expected by the script 'ch-frb-make-acq-inventory', which
// is the first step in postprocessing the captured data with our "offline" pipeline.
//
// The stream_acqname is implemented using a more general feature of ch_frb_io, which
// allows a general stream_filename_pattern of the form (*) or (**) to be specified.
//
// The helper function acqname_to_filename_pattern() does three things:
//
//   - does some sanity checks on the acqname and throws an exception if anything goes wrong
//     (most importantly, checking that an acquisition by the same name doesn't already
//     exist, and that the proper filesystem is mounted)
// 
//   - creates acqdir if it doesn't already exist, and beam subdirs corresponding
//     to the server's beam ids
//
//   - returns the ch_frb_io 'stream_filename_pattern' corresponding to the given
//     ch_frb_l1 acqname.


// Helper for acqname_to_filename_pattern()
static void check_acqdir_base(const string &acqdir_base)
{
    if (!file_exists(acqdir_base))
	throw runtime_error("ch-frb-l1: acqdir_base " + acqdir_base + " does not exist (probably need to mount device)");
    if (!is_directory(acqdir_base))
	throw runtime_error("ch-frb-l1: acqdir_base " + acqdir_base + " exists but is not a directory?!");
}
	

// Helper for acqname_to_filename_pattern()
static void check_acqdir(const string &acqdir_base, const string &acqname, const vector<int> &beam_ids)
{
    string acqdir = acqdir_base + "/" + acqname;
    makedir(acqdir, false);  // throw_exception_if_directory_exists=false
    
    if (!is_empty_directory(acqdir)) {
	// We throw an exception if acqdir exists and is a nonempty directory,
	// unless it just contains some empty directories.

	for (const auto &d: listdir(acqdir)) {
	    if ((d == ".") || (d == ".."))
		continue;
	    
	    string subdir = acqdir + "/" + d;

	    if (!is_directory(subdir) || !is_empty_directory(subdir))
		throw runtime_error("ch-frb-l1: acqdir " + acqdir + " already exists and is nonempty");
	}
    }

    for (int beam_id: beam_ids) {
	// FIXME it would be better to do directory creation on-the-fly in ch_frb_io.
	// (At some point this will be necessary, to implement on-the-fly beam_ids.)

	stringstream beamdir_s;
	beamdir_s << acqdir << "/beam_" << setfill('0') << setw(4) << beam_id;
	
	string beamdir = beamdir_s.str();
	makedir(beamdir, false);   // throw_exception_if_directory_exists=false
    }
}


string acqname_to_filename_pattern(const string &devname, const string &acqname, const vector<int> &beam_ids)
{
    if ((acqname.size() == 0) || (beam_ids.size() == 0))
	return string();  // no acquisition requested

    if (!strcasecmp(devname.c_str(), "ssd")) {
	check_acqdir_base("/local/acq_data");
	check_acqdir("/local/acq_data", acqname, beam_ids);
	return "/local/acq_data/" + acqname + "/beam_(BEAM)/chunk_(CHUNK).msg";
    }
    else if (!strcasecmp(devname.c_str(), "nfs")) {
	check_acqdir_base("/frb-archiver-1/acq_data");	
	check_acqdir_base("/frb-archiver-2/acq_data");	
	check_acqdir_base("/frb-archiver-3/acq_data");	
	check_acqdir_base("/frb-archiver-4/acq_data");
	check_acqdir("/frb-archiver-1/acq_data", acqname, beam_ids);
	return "/frb-archiver-(STREAM)/acq_data/" + string(acqname) + "/beam_(BEAM)/chunk_(CHUNK).msg";
    }
    else
	throw runtime_error("ch-frb-l1::acqname_to_filename_pattern(): 'devname' must be either 'ssd' or 'nfs'");
}


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


void makedir(const string &filename, bool throw_exception_if_directory_exists, mode_t mode)
{
    int err = mkdir(filename.c_str(), mode);

    if (err >= 0)
	return;
    if (throw_exception_if_directory_exists || (errno != EEXIST))
	throw runtime_error(filename + ": mkdir() failed: " + strerror(errno));
    
    // If we get here, then mkdir() failed with EEXIST, and throw_exception_if_directory_exists=false.
    // We still throw an exception if the file is not a directory.

    struct stat s;
    err = stat(filename.c_str(), &s);

    // A weird corner case.
    if (err < 0)
	throw runtime_error(filename + ": mkdir() returned EEXIST but stat() failed, not sure what is going on");

    if (!S_ISDIR(s.st_mode))
	throw runtime_error(filename + ": file exists but is not a directory");
}


vector<string> listdir(const string &dirname)
{
    vector<string> filenames;

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
	    break;

	filenames.push_back(entry->d_name);
    }

    return filenames;
}


}   // namespace ch_frb_l1
