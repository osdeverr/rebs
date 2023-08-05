#include "process_util.h"
#include "target.h"

#include <fmt/format.h>

#include <reproc++/reproc.hpp>

#ifdef WIN32
#include <Windows.h>
#endif

namespace re
{
    // // run_target->module, exe_path, run_args, true, false, working_dir
    #ifdef WIN32

        static std::set<reproc::process *> gHandledProcesses;
        static bool isCtrlHandlerAttached = false;

        BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
        {
            if (fdwCtrlType == CTRL_C_EVENT)
            {
                for (auto proc : gHandledProcesses)
                {
                    proc->terminate();
                }

                gHandledProcesses.clear();
            }

            return FALSE;
        }

    #endif

#if TRUE

    int RunProcessOrThrow(std::string_view program_name, const fs::path &path, std::vector<std::string> cmdline,
                          bool output, bool throw_on_bad_exit, std::optional<fs::path> working_directory)
    {

        std::string workdir = "";
        if (working_directory)
            workdir = working_directory->u8string();

        reproc::options options;
        options.redirect.parent = output;
        options.working_directory = !workdir.empty() ? (decltype(options.working_directory))workdir.c_str() : nullptr;

        if (!path.empty())
            cmdline.insert(cmdline.begin(), path.u8string());

        reproc::process process;
        auto start_ec = process.start(cmdline, options);
        if (start_ec)
        {
            RE_THROW ProcessRunException("{} failed to start: {} (ec={})", program_name, start_ec.message(),
                                         start_ec.value());
        }

        #ifdef WIN32
                if (!isCtrlHandlerAttached)
                {
                    SetConsoleCtrlHandler(CtrlHandler, TRUE);
                    isCtrlHandlerAttached = true;
                }

                gHandledProcesses.insert(&process);
        #endif

        auto [exit_code, end_ec] = process.wait(reproc::infinite);

        #ifdef WIN32
                gHandledProcesses.erase(&process);
        #endif

        // process.read(reproc::stream::out, );

        if (end_ec)
        {
            RE_THROW ProcessRunException("{} failed to run: {} (ec={} exit_code={})", program_name, end_ec.message(),
                                         end_ec.value(), exit_code);
        }

        if (throw_on_bad_exit && exit_code != 0)
        {
            RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
        }

        return exit_code;
    }

#endif

#ifdef WIN32_FALSE
    namespace detail
    {
        bool IsInJob()
        {
            BOOL result;
            if (!IsProcessInJob(GetCurrentProcess(), NULL, &result))
                throw ProcessRunException("IsProcessInJob failed. WinError: 0x{:X}", (uint32_t)GetLastError());

            return result;
        }

        bool CheckIsAutokillByJobEnabled()
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
            DWORD len = 0;
            if (!QueryInformationJobObject(NULL, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli), &len))
                return false;

            return jeli.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        }

        bool IsAutokillByJobEnabled()
        {
            if (!IsInJob())
                return false;

            return CheckIsAutokillByJobEnabled();
        }
    } // namespace detail

    int RunProcessOrThrow(std::string_view program_name, const fs::path &path, std::vector<std::string> cmdline,
                          bool output, bool throw_on_bad_exit, std::optional<fs::path> working_directory)
    {
        HANDLE hJob = NULL;
        HANDLE hProcess = NULL;
        HANDLE hThread = NULL;

        auto finish = [&]() {
            if (hJob)
            {
                ::CloseHandle(hJob);
                hJob = NULL;
            }

            if (hProcess)
            {
                ::CloseHandle(hProcess);
                hProcess = NULL;
            }

            if (hThread)
            {
                ::CloseHandle(hThread);
                hThread = NULL;
            }
        };

        try
        {
            bool useJob = false;

            if (useJob)
            {
                hJob = CreateJobObjectW(NULL, NULL);
                if (!hJob)
                    RE_THROW ProcessRunException("{} failed to start: failed to create Win32 job object", program_name);

                JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
                jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

                if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
                    RE_THROW ProcessRunException("{} failed to start: failed to set Win32 job object information",
                                                 program_name);
            }

            std::wstring args = path.wstring();
            for (auto &s : cmdline)
            {
                std::string arg;
                for (auto &c : s)
                {
                    if (c == '\"')
                    {
                        arg.push_back('"');
                        arg.push_back('"');
                    }
                    else
                    {
                        arg.push_back(c);
                    }
                }

                args.append(L"\"");
                args.append(fs::path{arg}.wstring());
                args.append(L"\" ");
            }

            args.pop_back();

            STARTUPINFOW info = {sizeof(info)};
            PROCESS_INFORMATION pi;

            fmt::print("[Process] args:\n");
            for (auto &arg : cmdline)
            {
                fmt::print("[arg]: {}\n", arg);
            }

            fmt::print("[Process] command line: {}\n", fs::path{args}.u8string());

            DWORD dwFlags = useJob ? (CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB) : NULL;

            if (::CreateProcessW(NULL, args.data(), nullptr, nullptr, true, dwFlags, nullptr,
                                 working_directory ? working_directory->wstring().c_str() : nullptr, &info, &pi))
            {
                hProcess = pi.hProcess;
                hThread = pi.hThread;

                if (useJob)
                {
                    if (!AssignProcessToJobObject(hJob, pi.hProcess))
                        RE_THROW ProcessRunException(
                            "{} failed to start: failed to assign Win32 job object. WinError: 0x{:X}", program_name,
                            (uint32_t)GetLastError());

                    ::ResumeThread(hThread);
                }

                ::WaitForSingleObject(hProcess, INFINITE);

                DWORD exit_code = 0;
                ::GetExitCodeProcess(hProcess, &exit_code);

                if (throw_on_bad_exit && exit_code != 0)
                {
                    RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
                }

                finish();
                return (int)exit_code;
            }
            else
            {
                RE_THROW ProcessRunException("{} failed to start: CreateProcessW failed. WinError: 0x{:X}",
                                             program_name, (uint32_t)GetLastError());
            }
        }
        catch (const ProcessRunException &ex)
        {
            finish();
            throw ex;
        }
        catch (const std::exception &ex)
        {
            finish();
            throw ex;
        }
    }

#endif

} // namespace re
