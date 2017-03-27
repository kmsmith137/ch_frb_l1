#ifndef _CH_FRB_L1_HPP
#define _CH_FRB_L1_HPP

#include <string>
#include <vector>
#include <stdexcept>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace ch_frb_l1 {
#if 0
}  // compiler pacifier
#endif


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

    std::unordered_set<std::string> all_keys;
    mutable std::unordered_set<std::string> requested_keys;

    yaml_paramfile(const std::string &filename);

    bool has_param(const std::string &k) const;
    void check_for_unused_params(bool fatal=true) const;

    // By default, throws an exception
    virtual void _die(const std::string &txt) const;

    // For debugging
    template<typename T> static inline std::string type_name();
    
    template<typename T>
    T read_scalar(const std::string &k) const
    {
	requested_keys.insert(k);

	if (!has_param(k))
	    _die("parameter '" + k + "' not found");

	try {
	    return yaml[k].as<T> ();
	}
	catch (...) { }

	_die(std::string("expected '") + k + std::string("' to have type ") + type_name<T>());
	return 0;   // compiler pacifier
    }

    // Automatically converts a scalar to length-1 vector.
    template<typename T>
    std::vector<T> read_vector(const std::string &k) const
    {
	requested_keys.insert(k);

	if (!has_param(k))
	    _die("parameter '" + k + "' not found");

	try {
	    std::vector<T> ret = yaml[k].as<std::vector<T>> ();
	    if (ret.size() > 0)
		return ret;
	}
	catch (...) { }

	try {
	    return { yaml[k].as<T>() };
	}
	catch (...) { }

	_die("expected '" + k + "' to have type " + type_name<T>() + ", or be a list of " + type_name<T>() + "s");
	return { };   // compiler pacifier
    }
};


template<> inline std::string yaml_paramfile::type_name<int> () { return "int"; }
template<> inline std::string yaml_paramfile::type_name<double> () { return "double"; }
template<> inline std::string yaml_paramfile::type_name<std::string> () { return "string"; }


}  // namespace ch_frb_l1

#endif  // _CH_FRB_L1_HPP
