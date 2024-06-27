#pragma once

#include <string>
#include <sstream>
#include <regex>
#include <dap/protocol.h>
#include <common/engine/printf.h>
#include <map>
namespace DebugServer
{

  // Caseless comparison, required for script identifiers (ZScript/ACS/DECORATE are all case-insensitive)
	struct ci_less
	{
		// case-independent (ci) compare_less binary function
		struct nocase_compare
		{
		bool operator() (const unsigned char& c1, const unsigned char& c2) const {
			return tolower (c1) < tolower (c2); 
		}
		};
		bool operator() (const std::string & s1, const std::string & s2) const {
		return std::lexicographical_compare 
			(s1.begin (), s1.end (),   // source range
			s2.begin (), s2.end (),   // dest range
			nocase_compare ());  // comparison
		}
	};

  template <typename V>
  using caseless_path_map = typename std::map<std::string, V, ci_less>;

  inline bool CaseInsensitiveEquals(const std::string & s1, const std::string & s2)
  {
    return std::lexicographical_compare
            (s1.begin (), s1.end (),   // source range
             s2.begin (), s2.end (),   // dest range
             [] (const unsigned char& c1, const unsigned char& c2) {
              return tolower (c1) == tolower (c2);
            });  // comparison
  }

	template<typename... Args>
	std::string StringFormat(const char* fmt, Args... args)
	{
		const size_t size = snprintf(nullptr, 0, fmt, args...);
		std::string buf;
		buf.reserve(size + 1);
		buf.resize(size);
		snprintf(&buf[0], size + 1, fmt, args...);
		return buf;
	}

	template<typename... Args>
	void LogError(const char* fmt, Args... args)
	{
		Printf(TEXTCOLOR_RED "%s\n", StringFormat(fmt, args...).c_str());
	}

#define RETURN_DAP_ERROR(message) \
	LogError("%s", message); \
	return dap::Error(message);


	template <typename T>
	T ByteSwap(T val)
	{
		T retVal;
		const auto pVal = reinterpret_cast<char*>(& val);
		const auto pRetVal = reinterpret_cast<char*>(& retVal);
		const int size = sizeof(T);
		for (auto i = 0; i < size; i++)
		{
			pRetVal[size - 1 - i] = pVal[i];
		}

		return retVal;
	}

	inline std::vector<std::string> Split(const std::string& s, const std::string& delimiter) {
		size_t posStart = 0, posEnd;
		const auto delimLen = delimiter.length();
		std::vector<std::string> res;

		while ((posEnd = s.find(delimiter, posStart)) != std::string::npos) {
			auto token = s.substr(posStart, posEnd - posStart);
			posStart = posEnd + delimLen;
			res.push_back(token);
		}

		res.push_back(s.substr(posStart));
		return res;
	}

	inline std::string Join(const std::vector<std::string>& elements, const char* const separator)
	{
		switch (elements.size())
		{
		case 0:
			return "";
		case 1:
			return elements[0];
		default:
			std::ostringstream os;
			std::copy(elements.begin(), elements.end() - 1, std::ostream_iterator<std::string>(os, separator));
			os << *elements.rbegin();
			return os.str();
		}
	}

	inline bool ParseInt(const std::string& str, int* value, std::size_t* pos = nullptr, const int base = 10)
	{
		try
		{
			*value = std::stoi(str, pos, base);
		}
		catch (void*)
		{
			return false;
		}

		return true;
	}

	inline void ToLower(std::string &p_str){
		for (size_t i = 0; i < p_str.size(); i++){
				p_str[i] = tolower(p_str[i]);
		}
	}

	inline std::string ToLowerCopy(const std::string &p_str){
		std::string r_str = p_str;
		ToLower(r_str);
		return r_str;
	}

	inline std::string DemangleName(std::string name)
	{
		if (name.front() == ':')
		{
			return name.substr(2, name.length() - 6);
		}

		return name;
	}
	// TODO: do something here
	inline std::string NormalizeScriptName(std::string name)
	{
		return name;
	}

	inline int GetScriptReference(const std::string& scriptName)
	{
		constexpr std::hash<std::string> hasher{};
		std::string name = scriptName;
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);

		return std::abs(static_cast<int>(hasher(name))) + 1;
	}

	inline int GetSourceReference(const dap::Source& src) {
		// If the source reference <= 0, it's invalid
		if (src.sourceReference.value(0) > 0) {
			return static_cast<int>(src.sourceReference.value());
		}
		if (!src.path.has_value()) {
			return -1;
		}
    std::string path = src.path.value();
    if (src.origin.has_value()) {
      path = src.origin.value() + ":" + path;
    }
		return GetScriptReference(path);
	}

	inline std::string GetSourceModfiedTime(const dap::Source & src) {
		if (!src.checksums.has_value()) {
			return "";
		}
		for (auto & checksum : src.checksums.value()) {
			if (checksum.algorithm == "timestamp") {
				return checksum.checksum;
			}
		}
		return "";
	}

	inline bool CompareSourceModifiedTime(const dap::Source& src1, const dap::Source& src2) {
		if (GetSourceModfiedTime(src1) != GetSourceModfiedTime(src2)) {
			return false;
		}
		return true;
	}

  inline std::string StringJoin(const std::vector<std::string>& strings, const char* delim) {
    std::ostringstream imploded;
    std::copy(strings.begin(), strings.end(),
              std::ostream_iterator<std::string>(imploded, delim));
    return imploded.str().substr(0, imploded.str().size() - strlen(delim));
  }
}
