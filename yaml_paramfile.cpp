#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#include "ch_frb_l1.hpp"

using namespace std;

namespace ch_frb_l1 {
#if 0
}   // compiler pacifier
#endif


static bool file_exists(const string &filename)
{
    struct stat s;

    int err = stat(filename.c_str(), &s);
    if (err >= 0)
        return true;
    if (errno == ENOENT)
        return false;

    throw runtime_error(filename + ": " + strerror(errno));
}


yaml_paramfile::yaml_paramfile(const string &filename_) :
    filename(filename_)
{
    if (!file_exists(filename))
	_die("file not found");

    ifstream f(filename);
    if (f.fail())
	_die("miscellaneous I/O failure (file exists, but ifstream constructor failed)");

    try {
	this->yaml = YAML::Load(f);
    } catch (exception &e) {
	_die(string("couldn't parse yaml file (") + e.what() + ")");
    }

    if (!yaml.IsMap())
	_die("file parsed successfully, but toplevel yaml node is not a Map");

    for (const auto &kv: yaml) {
	try {
	    string k = kv.first.as<string> ();
	    this->all_keys.insert(k);
	}
	catch (...) {
	    _die("file parsed successfully, and toplevel yaml node is a Map, but not every key in the map is a string");
	}
    }
}


bool yaml_paramfile::has_param(const string &k) const
{
    return all_keys.count(k) > 0;
}


void yaml_paramfile::check_for_unused_params(bool fatal) const
{
    bool flag = false;
    
    stringstream ss;
    ss << filename << "unrecognized params";

    for (const string &k: all_keys) {
	if (requested_keys.count(k) == 0) {
	    ss << (flag ? ", " : " ") << k;
	    flag = true;
	}
    }

    if (!flag)
	return;
    if (fatal)
	_die(ss.str());

    cerr << filename << ": " << ss.str() << endl;
}


// Virtual member function; this is the default implementation
void yaml_paramfile::_die(const string &txt) const
{
    throw runtime_error(filename + ": " + txt);
}


}   // namespace ch_frb_l1
