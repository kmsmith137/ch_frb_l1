#include <iostream>
#include <iomanip>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fts.h>

#include "ch_frb_l1.hpp"

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
static void check_acqdir(const string &acqdir_base, const string &acqname, const vector<int> &beam_ids, bool new_acq)
{
    string acqdir = acqdir_base + "/" + acqname;
    makedir(acqdir, false);  // throw_exception_if_directory_exists=false

    for (int beam_id: beam_ids) {
	// Create per-beam directory.
	// FIXME it would be better to do directory creation on-the-fly in ch_frb_io.
	// (At some point this will be necessary, to implement on-the-fly beam_ids.)

	stringstream beamdir_s;
	beamdir_s << acqdir << "/beam_" << setfill('0') << setw(4) << beam_id;
	
	string beamdir = beamdir_s.str();

	if (!file_exists(beamdir))
	    makedir(beamdir, false);   // throw_exception_if_directory_exists=false
	else if (!is_directory(beamdir))
	    throw runtime_error("ch-frb-l1: acqdir " + acqdir + " exists, but is not a directory?!");
	else if (new_acq && !is_empty_directory(beamdir))
	    // This is the important case to check, to avoid overwriting a previous acquisition with the same name.
	    throw runtime_error("ch-frb-l1: acqdir " + acqdir + " already exists and is nonempty");
	
	// If we get here, then the per-beam directory exists and is empty.  
	// This is OK, so just fall through.
    }
}


string acqname_to_filename_pattern(const string &devname, const string &acqname, const vector<int> &stream_ids, const vector<int> &beam_ids, bool new_acq)
{
    if ((acqname.size() == 0) || (beam_ids.size() == 0))
	return string();  // no acquisition requested

    if (!strcasecmp(devname.c_str(), "ssd")) {
	check_acqdir_base("/local/acq_data");
	check_acqdir("/local/acq_data", acqname, beam_ids, new_acq);
	return "/local/acq_data/" + acqname + "/beam_(BEAM)/chunk_(CHUNK).msg";
    }
    else if (!strcasecmp(devname.c_str(), "nfs")) {
	for (int stream_id: stream_ids)
	    check_acqdir_base("/frb-archiver-" + to_string(stream_id) + "/acq_data");
	check_acqdir("/frb-archiver-1/acq_data", acqname, beam_ids, new_acq);
	return "/frb-archiver-(STREAM)/acq_data/" + string(acqname) + "/beam_(BEAM)/chunk_(CHUNK).msg";
    }
    else
	throw runtime_error("ch-frb-l1::acqname_to_filename_pattern(): 'devname' must be either 'ssd' or 'nfs'");
}

// Converts "/local/acq_data/(ACQNAME)/beam_(BEAM)/chunk_(CHUNK).msg"
// to "/local/acq_data/(ACQNAME)"
string acq_pattern_to_dir(const string &pattern) {
    // and drop two path components
    size_t ind1 = pattern.rfind("/");
    if (ind1 == std::string::npos || ind1 == 0) {
        throw runtime_error("ch-frb-l1::acq_pattern_to_dir: last '/' not found in \"" + pattern + "\"");
    }
    size_t ind2 = pattern.rfind("/", ind1-1);
    if (ind2 == std::string::npos || ind2 == 0) {
        throw runtime_error("ch-frb-l1::acq_pattern_to_dir: second-last '/' not found in \"" + pattern + "\"");
    }
    return pattern.substr(0, ind2);
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


size_t disk_space_used(const string &dirname) {
    FTS* hierarchy;
    char** paths;
    size_t totalsize = 0;

    paths = (char**)alloca(2 * sizeof(char*));
    paths[0] = (char*)dirname.c_str();
    paths[1] = NULL;
    hierarchy = fts_open(paths, FTS_LOGICAL, NULL);
    if (!hierarchy) {
        throw runtime_error(dirname + ": fts_open() failed: " + strerror(errno));
    }
    while (1) {
        FTSENT *entry = fts_read(hierarchy);
        if (!entry && (errno == 0))
            break;
        if (!entry)
            throw runtime_error(dirname + ": fts_read() failed: " + strerror(errno));
        if (entry->fts_info & FTS_F) {
            // The entry is a file.
            struct stat *st = entry->fts_statp;
            totalsize += st->st_size;
            cout << "path " << entry->fts_path << " size " << st->st_size << endl;
        }
        
    }
    fts_close(hierarchy);
    return totalsize;
}


}   // namespace ch_frb_l1
