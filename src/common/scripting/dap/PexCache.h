#pragma once

#include <map>

#include <dap/protocol.h>
#include <mutex>
#include <string>

namespace Pex
{
	struct Binary
	{
		std::string archiveName;
		std::string scriptName;
		std::string scriptPath;
		std::string source;
		dap::Source sourceData;
	};
}

namespace DebugServer

{
	class PexCache
	{
	public:
		PexCache() = default;

		bool HasScript(int scriptReference);
		bool HasScript(const std::string &scriptName);

		std::shared_ptr<Pex::Binary> GetCachedScript(const int ref);
		std::shared_ptr<Pex::Binary> GetScript(const dap::Source &source);

		std::shared_ptr<Pex::Binary> GetScript(const std::string &fqsn);
		bool GetDecompiledSourceByRef(int ref, std::string &decompiledSource);
		bool GetDecompiledSource(const dap::Source &source, std::string &decompiledSource);

		bool GetDecompiledSource(const std::string &fqpn, std::string &decompiledSource);
		bool GetSourceData(const std::string &scriptName, dap::Source &data);
		void Clear();

	private:
		std::mutex m_scriptsMutex;
		std::map<int, std::shared_ptr<Pex::Binary>> m_scripts;
	};
}
