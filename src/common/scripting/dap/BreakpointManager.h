#pragma once
#include <map>
#include <set>
#include <dap/protocol.h>
#include <dap/session.h>

#include "GameInterfaces.h"

#include "PexCache.h"

namespace DebugServer
{
	class BreakpointManager
	{

	public:
		struct BreakpointInfo
		{
			int64_t breakpointId;
			int instructionNum;
			int lineNum;
			int debugFuncInfoIndex;
      bool isNative;
		};

		struct ScriptBreakpoints
		{
			int ref{-1};
			dap::Source source;
			std::time_t modificationTime{0};
			std::map<int, BreakpointInfo> breakpoints;
		};

		explicit BreakpointManager(PexCache *pexCache)
			: m_pexCache(pexCache)
		{

    }
		dap::ResponseOrError<dap::SetBreakpointsResponse> SetBreakpoints(const dap::Source &src, const dap::SetBreakpointsRequest &request);
    dap::ResponseOrError<dap::SetFunctionBreakpointsResponse> SetFunctionBreakpoints(const dap::SetFunctionBreakpointsRequest &request);
    dap::ResponseOrError<dap::SetInstructionBreakpointsResponse> SetInstructionBreakpoints(const dap::SetInstructionBreakpointsRequest &request);
		void ClearBreakpoints(bool emitChanged = false);
		// bool CheckIfFunctionWillWaitOrExit(RE::BSScript::Internal::CodeTasklet* tasklet);
		void InvalidateAllBreakpointsForScript(int ref);
		bool GetExecutionIsAtValidBreakpoint(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc);

	private:
    bool HasSeenBreakpoint(BreakpointInfo *info);
		PexCache *m_pexCache;
		std::map<int, ScriptBreakpoints> m_breakpoints;
    // set of case-insensitive strings
    caseless_path_map<BreakpointInfo> m_functionBreakpoints;
    std::map<uint64_t, ScriptBreakpoints> m_InstructionBreakpoints;
		BreakpointInfo *m_last_seen = nullptr;
		size_t times_seen = 0;
	};
}
