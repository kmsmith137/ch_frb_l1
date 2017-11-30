#include <cstring>
#include <fstream>
#include <iostream>

#include "ch_frb_l1.hpp"

using namespace std;

namespace ch_frb_l1 {
#if 0
}   // compiler pacifier
#endif


yaml_paramfile::yaml_paramfile(const string &filename_, int verbosity_) :
    filename(filename_), verbosity(verbosity_)
{
    if (verbosity < 0 || verbosity > 2)
	_die("verbosity constructor argument (=" + stringify(verbosity) + ") must be 0, 1, or 2");

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


bool yaml_paramfile::check_for_unused_params(bool fatal) const
{
    vector<string> unused_keys;

    for (const string &k: all_keys)
	if (requested_keys.count(k) == 0)
	    unused_keys.push_back(k);
    
    if (unused_keys.size() == 0)
	return true;
    
    stringstream ss;
    ss << ((unused_keys.size() > 1) ? "unused parameters " : "unused parameter ");

    for (unsigned int i = 0; i < unused_keys.size(); i++) {
	if (i > 0)
	    ss << ", ";
	ss << "'" << unused_keys[i] << "'";
    }

    if (fatal)
	_die(ss.str());

    cerr << filename << ": " << ss.str() << endl;
    return false;
}


// Virtual member function; this is the default implementation
void yaml_paramfile::_die(const string &txt) const
{
    throw runtime_error(filename + ": " + txt);
}

// Virtual member function; this is the default implementation
void yaml_paramfile::_print(const string &txt) const
{
    cout << filename << ": " << txt << flush;
}


}   // namespace ch_frb_l1
