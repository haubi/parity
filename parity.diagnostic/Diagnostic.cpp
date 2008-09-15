/****************************************************************\
*                                                                *
* Copyright (C) 2008 by Markus Duft <markus.duft@salomon.at>     *
*                                                                *
* This file is part of parity.                                   *
*                                                                *
* parity is free software: you can redistribute it and/or modify *
* it under the terms of the GNU Lesser General Public License as *
* published by the Free Software Foundation, either version 3 of *
* the License, or (at your option) any later version.            *
*                                                                *
* parity is distributed in the hope that it will be useful,      *
* but WITHOUT ANY WARRANTY; without even the implied warranty of *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  *
* GNU Lesser General Public License for more details.            *
*                                                                *
* You should have received a copy of the GNU Lesser General      *
* Public License along with parity. If not,                      *
* see <http://www.gnu.org/licenses/>.                            *
*                                                                *
\****************************************************************/

#include <windows.h>
#include <iostream>
#include <string>
#include <map>

#include <psapi.h>

void Diagnose(FILE* out, DWORD dwTopProcess) {
	DWORD dwContinuationStatus = DBG_CONTINUE;
	DEBUG_EVENT ev;
	BOOL bDiagnosting = TRUE;
	std::map<DWORD, HANDLE> process_map;

	fprintf(out, "starting diagnostic on toplevel process %d...\n", dwTopProcess);

	while (bDiagnosting) {
		char fn[MAX_PATH];
		WaitForDebugEvent(&ev, INFINITE);

		memset(fn, 0, MAX_PATH);

		switch(ev.dwDebugEventCode) {
			case CREATE_PROCESS_DEBUG_EVENT:
				if(!GetProcessImageFileName(ev.u.CreateProcessInfo.hProcess, (LPSTR)&fn, MAX_PATH)) {
					fprintf(out, "WARNING: cannot obtain image filename!\n");
					memset(fn, 0, MAX_PATH);
				}
				fprintf(out, "[%d:%d] Creating Process: %s, Base: %p, Entry: %p\n", ev.dwProcessId, ev.dwThreadId, fn, ev.u.CreateProcessInfo.lpBaseOfImage, ev.u.CreateProcessInfo.lpStartAddress);
				process_map[ev.dwProcessId] = ev.u.CreateProcessInfo.hProcess;
				break;
			case CREATE_THREAD_DEBUG_EVENT:
				fprintf(out, "[%d:%d] Creating Thread: Start Address: %d\n", ev.dwProcessId, ev.dwThreadId, ev.u.CreateThread.lpStartAddress);
				break;
			case EXCEPTION_DEBUG_EVENT:
				fprintf(out, "[%d:%d] %sException: %x at %p\n", ev.dwProcessId, ev.dwThreadId, ev.u.Exception.dwFirstChance ? "First Chance " : "", ev.u.Exception.ExceptionRecord.ExceptionCode, ev.u.Exception.ExceptionRecord.ExceptionAddress);
				break;
			case EXIT_PROCESS_DEBUG_EVENT:
				fprintf(out, "[%d:%d] Exiting Process: Exit Code: %d\n", ev.dwProcessId, ev.dwThreadId, ev.u.ExitProcess.dwExitCode);
				process_map[ev.dwProcessId] = INVALID_HANDLE_VALUE;

				if(ev.dwProcessId == dwTopProcess) {
					fprintf(out, "Toplevel Process exited, stopping...\n");
					bDiagnosting = FALSE;
				}
				break;
			case EXIT_THREAD_DEBUG_EVENT:
				fprintf(out, "[%d:%d] Exiting Thread: Exit Code: %d\n", ev.dwProcessId, ev.dwThreadId, ev.u.ExitThread.dwExitCode);
				break;
			case LOAD_DLL_DEBUG_EVENT:
				fprintf(out, "[%d:%d] Loading DLL: Base: %p\n", ev.dwProcessId, ev.dwThreadId, ev.u.LoadDll.lpBaseOfDll);
				break;
			case OUTPUT_DEBUG_STRING_EVENT:
				if(ev.u.DebugString.nDebugStringLength > 0)
				{
					char* localbuffer = (char*)malloc(ev.u.DebugString.nDebugStringLength + 2);
					if(!ReadProcessMemory(process_map[ev.dwProcessId], ev.u.DebugString.lpDebugStringData, localbuffer, ev.u.DebugString.nDebugStringLength, NULL)) {
						fprintf(out, "WARNING: cannot read memory of process %d\n", ev.dwProcessId);
						break;
					}
					if(ev.u.DebugString.fUnicode) {
						wchar_t* lb = (wchar_t*)localbuffer;
						if(wcslen(lb) > 0) {
							// can we do something about newlines at the end of the string?
							fprintf(out, "[%d:%d] Debug String: %*S\n", ev.dwProcessId, ev.dwThreadId, ev.u.DebugString.nDebugStringLength, localbuffer);
						}
					} else {
						if(localbuffer[0] != '\0') {
							if(localbuffer[ev.u.DebugString.nDebugStringLength - 2] == '\n')
								localbuffer[ev.u.DebugString.nDebugStringLength - 2] = '\0';
							fprintf(out, "[%d:%d] Debug String: %*s\n", ev.dwProcessId, ev.dwThreadId, ev.u.DebugString.nDebugStringLength, localbuffer);
						}
					}
				}
				break;
			case RIP_EVENT:
				fprintf(out, "[%d:%d] RIP (System Debugging Error): Error: %d, Type: %d\n", ev.dwProcessId, ev.dwThreadId, ev.u.RipInfo.dwError, ev.u.RipInfo.dwType);
				break;
			case UNLOAD_DLL_DEBUG_EVENT:
				fprintf(out, "[%d:%d] Unloading DLL: Base: %p\n", ev.dwProcessId, ev.dwThreadId, ev.u.UnloadDll.lpBaseOfDll);
				break;
			default:
				fprintf(out, "[%d:%d] Received unkonwn Debug Event: %d\n", ev.dwProcessId, ev.dwThreadId, ev.dwDebugEventCode);
		}

		fflush(out);
		ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, dwContinuationStatus);
	}
}

// CONFIX:EXENAME('parity.diagnostic.exe')
int main(int argc, char** argv) {
	// for adding additional parameters in the future.
	int app_offset = 1;

	if(argc < 2) {
		std::cerr << "usage: " << argv[0] << " <executable> [arguments ...]" << std::endl;
		exit(1);
	}

	std::string application(argv[app_offset]);
	std::string quoted_args;

	for(int i = app_offset+1; i < argc; ++i) {
		quoted_args.append(" \"");
		quoted_args.append(argv[i]);
		quoted_args.append("\"");
	}

	//
	// TODO: PATH lookup of application.
	//

	char * command_line = _strdup(quoted_args.c_str());

	if(!command_line) {
		std::cerr << "cannot copy arguments!" << std::cerr;
		exit(1);
	}

	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;

	if(!CreateProcess(application.c_str(), command_line, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &si, &pi)) {
		std::cerr << "cannot start process!" << std::cerr;
		exit(1);
	}

	// allow process to continue if we dies.
	DebugSetProcessKillOnExit(FALSE);

	free(command_line);

	Diagnose(stderr, pi.dwProcessId);
}