#include "BreakpointManager.h"
#include <cstdint>
#include <regex>
#include "Utilities.h"
#include "RuntimeEvents.h"
#include "GameInterfaces.h"

namespace DebugServer {

int64_t GetBreakpointID(int scriptReference, int lineNumber) {
  return (((int64_t) scriptReference) << 32) + lineNumber;
}

// std::string GetInstructionReference(const DebugInfo::FunctionInfo& finfo) {
// 	return fmt::format("{}:{}:{}", finfo.getObjectName().asString(), finfo.getStateName().asString(), finfo.getFunctionName().asString());
// }

dap::ResponseOrError<dap::SetBreakpointsResponse>
BreakpointManager::SetBreakpoints(const dap::Source &source, const dap::SetBreakpointsRequest &request) {
  RETURN_COND_DAP_ERROR (!request.breakpoints.has_value(), "SetBreakpoints: No breakpoints provided!");
  auto &srcBreakpoints = request.breakpoints.value();
  dap::SetBreakpointsResponse response;
  std::set<int> breakpointLines;
  auto scriptPath = source.name.value("");
  auto binary = m_pexCache->GetScript(source);
  RETURN_COND_DAP_ERROR(!binary, StringFormat("SetBreakpoints: Could not find script %s in loaded sources!", scriptPath.c_str()).c_str());

  // TODO: need to somehow get all the VMFunctions for a given script; right now, we just say that all breakpoints are valid if the script is loaded.
  // possibly trawl through all the classes and see which have a SourceFileLump that equals the scriptPath?
  //  bool hasDebugInfo = false;
  //  RETURN_COND_DAP_ERROR(!hasDebugInfo, fmt::format("SetBreakpoints: No debug data for script {}. Ensure that `bLoadDebugInformation=1` is set under `[ZScript]` in {}", scriptName, iniName));
  auto ref = GetSourceReference(source);
  ScriptBreakpoints info{
          .ref = ref,
          .source = source,
          .modificationTime = 0};
  std::map<int, BreakpointInfo> foundBreakpoints;

  for (const auto &srcBreakpoint: srcBreakpoints) {
    auto foundLine = true;
    int line = static_cast<int>(srcBreakpoint.line);
    int instructionNum = -1;
    int foundFunctionInfoIndex{-1};
    int64_t breakpointId = -1;
    breakpointId = GetBreakpointID(ref, line);

    if (foundLine) {
      auto bpoint = BreakpointInfo{
              .breakpointId = breakpointId,
              .instructionNum = instructionNum,
              .lineNum = line,
              .debugFuncInfoIndex = foundFunctionInfoIndex,
              .isNative = false
      };
      info.breakpoints[line] = bpoint;
    }
    auto bpoint_info = dap::Breakpoint{
            .id = foundLine ? dap::integer(breakpointId) : dap::optional<dap::integer>(),
            // .instructionReference = foundLine ? GetInstructionReference(debugfinfo) : dap::optional<dap::string>(),
            .line = dap::integer(line),
            // .offset = foundLine ? dap::integer(instructionNum) : dap::optional<dap::integer>(),
            .source = source,
            .verified = foundLine};
    response.breakpoints.push_back(bpoint_info);
  }

  m_breakpoints[ref] = info;
  return response;
}

dap::ResponseOrError<dap::SetFunctionBreakpointsResponse>
BreakpointManager::SetFunctionBreakpoints(const dap::SetFunctionBreakpointsRequest &request) {
  auto &breakpoints = request.breakpoints;
  dap::SetFunctionBreakpointsResponse response;
  // each request clears the previous function breakpoints
  m_functionBreakpoints.clear();
  for (const auto &breakpoint: breakpoints) {
    auto fullFuncName = breakpoint.name;
    // function names are `class.function`
    auto func_name_parts = Split(fullFuncName, ".");
    RETURN_COND_DAP_ERROR(func_name_parts.size() != 2, StringFormat("SetFunctionBreakpoints: Invalid function name %s!", fullFuncName.c_str()).c_str());
    auto className = FName(func_name_parts[0]);
    auto functionName = FName(func_name_parts[1]);
    auto func = PClass::FindFunction(className, functionName);
    RETURN_COND_DAP_ERROR(!func, StringFormat("SetFunctionBreakpoints: Could not find function %s in loaded sources!",
                                              fullFuncName.c_str()).c_str());
    if (IsFunctionNative(func)) {
      auto bpoint_info = BreakpointInfo{
              .breakpointId = GetBreakpointID(GetScriptReference(func->QualifiedName), 1),
              .instructionNum = 0,
              .lineNum = 1,
              .debugFuncInfoIndex = 0,
              .isNative = true
      };
      m_functionBreakpoints[func->QualifiedName] = bpoint_info;
      response.breakpoints.push_back(dap::Breakpoint{
              .id = bpoint_info.breakpointId,
              .line = 1,
      });
      continue;
    }
    // script function
    auto scriptFunction = static_cast<VMScriptFunction *>(func);
    auto scriptName = scriptFunction->SourceFileName.GetChars();
    dap::Source source;
    RETURN_COND_DAP_ERROR(!m_pexCache->GetSourceData(scriptName, source), StringFormat("SetFunctionBreakpoints: Could not find script %s in loaded sources!", scriptName).c_str());
    auto ref = GetSourceReference(source);
    RETURN_COND_DAP_ERROR(scriptFunction->LineInfoCount == 0, StringFormat("SetFunctionBreakpoints: Could not find line info for function %s!",
                                    fullFuncName.c_str()).c_str());
    auto lineNum = scriptFunction->LineInfo[0].LineNumber;
    auto instructionNum = scriptFunction->LineInfo[0].InstructionIndex;
    auto breakpointId = GetBreakpointID(ref, lineNum);
    auto bpoint_info = BreakpointInfo{
            .breakpointId = breakpointId,
            .instructionNum = instructionNum,
            .lineNum = lineNum,
            .debugFuncInfoIndex = 0,
            .isNative = false
    };
    response.breakpoints.push_back(dap::Breakpoint{
            .id = breakpointId,
            .instructionReference = StringFormat("%p", scriptFunction->Code),
            .line = lineNum,
            .source = source,
            .verified = true,
    });
    m_functionBreakpoints[func->QualifiedName] = bpoint_info;
  }
  return response;
}

void BreakpointManager::ClearBreakpoints(bool emitChanged) {
  if (emitChanged) {
    for (auto &kv: m_breakpoints) {
      InvalidateAllBreakpointsForScript(kv.first);
    }
  }
  m_breakpoints.clear();
}

void BreakpointManager::InvalidateAllBreakpointsForScript(int ref) {
  if (m_breakpoints.find(ref) != m_breakpoints.end()) {
    return;
  }
  for (auto &KV: m_breakpoints[ref].breakpoints) {
    auto bpinfo = KV.second;
    RuntimeEvents::EmitBreakpointChangedEvent(dap::Breakpoint{
                                                      .id = bpinfo.breakpointId,
                                                      .line = bpinfo.lineNum,
                                                      .source = m_breakpoints[ref].source,
                                                      .verified = false},
                                              "changed");
  }
  m_breakpoints.erase(ref);
}


bool
BreakpointManager::GetExecutionIsAtValidBreakpoint(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc) {
#define CLEAR_AND_RETURN   m_last_seen = nullptr; return false
  if (m_breakpoints.empty()) {
    CLEAR_AND_RETURN;
  }
  auto func = stack->TopFrame()->Func;
  if (!func) {
    LogError("No function in stack frame");
    CLEAR_AND_RETURN;
  }
  // check if we have a function breakpoint
  if (m_functionBreakpoints.find(stack->TopFrame()->Func->QualifiedName) != m_functionBreakpoints.end()) {
    auto bpoint_info = &m_functionBreakpoints[stack->TopFrame()->Func->QualifiedName];
    if (HasSeenBreakpoint(bpoint_info)) {
      return false;
    }
    m_last_seen = bpoint_info;
    return true;
  }

  if (IsFunctionNative(func)) {
    CLEAR_AND_RETURN;
  }
  auto scriptFunction = static_cast<VMScriptFunction *>(func);
  auto scriptName = scriptFunction->SourceFileName.GetChars();
  const auto sourceReference = GetScriptReference(scriptName);

  if (m_breakpoints.find(sourceReference) == m_breakpoints.end()) {
    CLEAR_AND_RETURN;
  }

  auto &scriptBreakpoints = m_breakpoints[sourceReference];

  if (scriptBreakpoints.breakpoints.empty()) {
    CLEAR_AND_RETURN;
  }

  int lineNo = -1;
  auto ip = pc;
  lineNo = scriptFunction->PCToLine(pc);
  if (lineNo != -1 && scriptBreakpoints.breakpoints.find(lineNo) != scriptBreakpoints.breakpoints.end()) {
    // we found a match, now check if we should pause
    if (!HasSeenBreakpoint(&scriptBreakpoints.breakpoints[lineNo])){
      m_last_seen = &scriptBreakpoints.breakpoints[lineNo];
      return true;
    }
    // seen it before, don't clear and don't break again
    return false;
  }
  return CLEAR_AND_RETURN;
#undef CLEAR_AND_RETURN
}


bool BreakpointManager::HasSeenBreakpoint(BreakpointManager::BreakpointInfo *info) {
  if (!m_last_seen || m_last_seen != info) {
    return false;
  }
  return true;
}

dap::ResponseOrError<dap::SetInstructionBreakpointsResponse>
BreakpointManager::SetInstructionBreakpoints(const dap::SetInstructionBreakpointsRequest &request) {
  RETURN_DAP_ERROR("Instruction breakpoints are not supported yet!");
}



// //TODO: WIP
// bool BreakpointManager::CheckIfFunctionWillWaitOrExit(RE::BSScript::Internal::CodeTasklet* tasklet) {
// 	auto& func = tasklet->topFrame->owningFunction;

// 	if (func->GetIsNative())
// 	{
// 		return true;
// 	}
// 	auto realfunc = dynamic_cast<RE::BSScript::Internal::ScriptFunction*>(func.get());

// 	int instNum = GetInstructionNumberForOffset(&realfunc->instructions, tasklet->topFrame->STACK_FRAME_IP);

// 	std::string scriptName(tasklet->topFrame->owningObjectType->GetName());
// 	const auto sourceReference = GetScriptReference(scriptName);
// 	if (m_breakpoints.find(sourceReference) != m_breakpoints.end())
// 	{
// 		auto& scriptBreakpoints = m_breakpoints[sourceReference];
// 		auto binary = m_pexCache->GetScript(scriptName);
// 		if (!binary || binary->getDebugInfo().getModificationTime() != scriptBreakpoints.modificationTime) {
// 			return true;
// 		}
// 		if (scriptBreakpoints.breakpoints.find(instNum) != scriptBreakpoints.breakpoints.end())
// 		{

// 			auto& breakpointInfo = scriptBreakpoints.breakpoints[instNum];
// 			auto& debugFuncInfo = binary->getDebugInfo().getFunctionInfos()[breakpointInfo.debugFuncInfoIndex];
// 			auto& lineNumbers = debugFuncInfo.getLineNumbers();
// 			auto funcData = GetFunctionData(binary, debugFuncInfo.getObjectName(), debugFuncInfo.getStateName(), debugFuncInfo.getFunctionName());
// 			auto& instructions = funcData->getInstructions();
// 			for (int i = instNum+1; i < instructions.size(); i++) {
// 				auto& instruction = instructions[i];
// 				auto opcode = instruction.getOpCode();
// 				// TODO: The rest of this

// 			}
// 		}
// 	}
// 	return true;
// }
}
