#ifndef _CH_FRB_L1_HPP
#define _CH_FRB_L1_HPP

#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace ch_frb_l1 {
#if 0
}  // compiler pacifier
#endif


// -------------------------------------------------------------------------------------------------
//
// file_utils.cpp


extern bool file_exists(const std::string &filename);
extern bool is_directory(const std::string &filename);
extern bool is_empty_directory(const std::string &dirname);

extern std::vector<std::string> listdir(const std::string &dirname);

// Note: umask will be applied to 'mode'
extern void makedir(const std::string &filename, bool throw_exception_if_directory_exists=true, mode_t mode=0777);

extern std::string acqname_to_filename_pattern(const std::string &acqname,
                                               const std::vector<int> &beam_ids);
// -------------------------------------------------------------------------------------------------
//
// Inlines


inline int xdiv(int num, int den)
{
    assert((num > 0) && (den > 0) && (num % den == 0));
    return num / den;
}

inline bool is_power_of_two(int n)
{
    assert(n >= 1);
    return (n & (n-1)) == 0;
}

inline int round_up_to_power_of_two(double x)
{
    assert(x > 0.0 && x < 1.0e9);
    return 1 << int(ceil(log2(x)));
}

inline int round_down_to_power_of_two(double x)
{
    assert(x >= 1.0 && x < 1.0e9);
    return 1 << int(floor(log2(x)));
}

inline std::vector<int> vrange(int lo, int hi)
{
    assert(lo <= hi);

    std::vector<int> ret(hi-lo);
    for (int i = lo; i < hi; i++)
	ret[i-lo] = i;

    return ret;
}

inline std::vector<int> vrange(int n)
{
    return vrange(0,n);
}

template<typename T>
inline std::vector<T> vconcat(const std::vector<T> &v1, const std::vector<T> &v2)
{
    size_t n1 = v1.size();
    size_t n2 = v2.size();

    std::vector<T> ret(n1+n2);
    memcpy(&ret[0], &v1[0], n1 * sizeof(T));
    memcpy(&ret[n1], &v2[0], n2 * sizeof(T));
    return ret;
}


// -------------------------------------------------------------------------------------------------
//
// yaml_paramfile


struct yaml_paramfile {
    const std::string filename;
    YAML::Node yaml;
    int verbosity;

    std::unordered_set<std::string> all_keys;
    mutable std::unordered_set<std::string> requested_keys;

    // The 'verbosity' constructor argument has the following meaning:
    //   0 = quiet
    //   1 = announce default values for all unspecified parameters
    //   2 = announce all parameters
    yaml_paramfile(const std::string &filename, int verbosity=0);

    bool has_param(const std::string &k) const;
    bool check_for_unused_params(bool fatal=true) const;

    // For debugging and message-printing
    virtual void _die(const std::string &txt) const;    // by default, throws an exception
    virtual void _print(const std::string &txt) const;  // by default, prints to cout
    
    template<typename T> static inline std::string type_name();
    template<typename T> static inline std::string stringify(const T &x);
    template<typename T> static inline std::string stringify(const std::vector<T> &x);


    // _read_scalar1(): helper for read_scalar(), assumes key exists
    template<typename T>
    T _read_scalar1(const std::string &k) const
    {
	try {
	    return yaml[k].as<T> ();
	}
	catch (...) { }

	_die(std::string("expected '") + k + std::string("' to have type ") + type_name<T>());
	throw std::runtime_error("yaml_paramfile::_die() returned?!");
    }

    // _read_scalar2(): helper for read_scalar(), assumes key exists
    template<typename T>
    T _read_scalar2(const std::string &k) const
    {
	T ret = _read_scalar1<T> (k);
	requested_keys.insert(k);

	if (verbosity >= 2)
	    _print(k + " = " + stringify(ret) + "\n");

	return ret;
    }

    // "Vanilla" version of read_scalar()
    template<typename T>
    T read_scalar(const std::string &k) const
    {
	if (!has_param(k))
	    _die("parameter '" + k + "' not found");

	return _read_scalar2<T> (k);
    }

    // This version of read_scalar() has a default value, which is returned if the key is not found.
    template<typename T>
    T read_scalar(const std::string &k, T default_val) const
    {
	if (!has_param(k)) {
	    if (verbosity >= 1)
		_print("parameter '" + k + "' not found, using default value " + stringify(default_val) + "\n");
	    return default_val;
	}

	return _read_scalar2<T> (k);
    }


    // _read_vector1(): helper for read_vector(), assumes key exists
    // Automatically converts a scalar to length-1 vector.
    template<typename T>
    std::vector<T> _read_vector1(const std::string &k) const
    {
	try {
	    return yaml[k].as<std::vector<T>> ();
	}
	catch (...) { }

	try {
	    return { yaml[k].as<T>() };
	}
	catch (...) { }

	_die("expected '" + k + "' to have type " + type_name<T>() + ", or be a list of " + type_name<T>() + "s");
	throw std::runtime_error("yaml_paramfile::_die() returned?!");	
    }

    // _read_vector2(): helper for read_vector(), assumes key exists
    template<typename T>
    std::vector<T> _read_vector2(const std::string &k) const
    {
	std::vector<T> ret = _read_vector1<T> (k);
	requested_keys.insert(k);

	if (verbosity >= 2)
	    _print(k + " = " + stringify(ret) + "\n");

	return ret;
    }

    // "Vanilla" version of read_vector().
    // Automatically converts a scalar to length-1 vector.
    template<typename T>
    std::vector<T> read_vector(const std::string &k) const
    {
	if (!has_param(k))
	    _die("parameter '" + k + "' not found");

	return _read_vector2<T> (k);
    }

    // This version of read_vector() has a default value, which is returned if the key is not found.
    template<typename T>
    std::vector<T> read_vector(const std::string &k, const std::vector<T> default_val) const
    {
	if (!has_param(k)) {
	    if (verbosity >= 1)
		_print("parameter '" + k + "' not found, using default value " + stringify(default_val) + "\n");
	    return default_val;
	}

	return _read_vector2<T> (k);
    }
};


template<> inline std::string yaml_paramfile::type_name<int> () { return "int"; }
template<> inline std::string yaml_paramfile::type_name<bool> () { return "bool"; }
template<> inline std::string yaml_paramfile::type_name<float> () { return "float"; }
template<> inline std::string yaml_paramfile::type_name<double> () { return "double"; }
template<> inline std::string yaml_paramfile::type_name<std::string> () { return "string"; }


template<typename T> inline std::string yaml_paramfile::stringify(const T &x)
{
    std::stringstream ss;
    ss << x;
    return ss.str();
}

template<typename T> inline std::string yaml_paramfile::stringify(const std::vector<T> &x)
{
    std::stringstream ss;
    ss << "[ ";
    for (size_t i = 0; i < x.size(); i++)
	ss << (i ? ", " : "") << x[i];
    ss << " ]";
    return ss.str();
}


}  // namespace ch_frb_l1

#endif  // _CH_FRB_L1_HPP
