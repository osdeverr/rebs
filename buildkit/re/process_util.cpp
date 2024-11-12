#include "process_util.h"
#include "target.h"
#include "ulib/encodings/utf8/string.h"
#include "ulib/impl/win32/process.h"
#include "ulib/process_exceptions.h"

#include <fmt/format.h>
#include <ulib/format.h>
#include <ulib/fmt/list.h>

#include <ulib/process.h>

namespace re
{
    int RunProcessOrThrow(ulib::string_view program_name, const fs::path &path, const ulib::list<ulib::string>& cmdline,
                          bool output, bool throw_on_bad_exit, std::optional<fs::path> working_directory)
    {
        ulib::list<ulib::u8string> cmdline_u8;
        for (auto &s : cmdline)
            cmdline_u8.push_back(s);

        auto path_proc = path;
        if (path_proc.empty())
        {
            path_proc = fs::path{cmdline.front()};
            cmdline_u8.erase(0, 1);
        }

        ulib::process process;

        try
        {
            process.run(path_proc, cmdline_u8, ulib::process::die_with_parent, working_directory);
        }
        catch (const ulib::process_error &ex)
        {
            RE_THROW ProcessRunException("{} failed to start: [{}] {}", program_name, typeid(ex).name(), ex.what());
        }

        try
        {
            auto exit_code = process.wait();

            if (throw_on_bad_exit && exit_code != 0)
            {
                RE_THROW ProcessRunException("{} {} failed: exit_code={}", program_name, cmdline_u8, exit_code);
            }

            return exit_code;
        }
        catch (const ulib::process_error &ex)
        {
            RE_THROW ProcessRunException("{} failed to run: [{}] {}", program_name, typeid(ex).name(), ex.what());
        }

        return -1;
    }

    // namespace detail
    // {
    //     bool IsInJob()
    //     {
    //         BOOL result;
    //         if (!IsProcessInJob(GetCurrentProcess(), NULL, &result))
    //             throw ProcessRunException("IsProcessInJob failed. WinError: 0x{:X}", (uint32_t)GetLastError());

    //         return result;
    //     }

    //     bool CheckIsAutokillByJobEnabled()
    //     {
    //         JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
    //         DWORD len = 0;
    //         if (!QueryInformationJobObject(NULL, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli), &len))
    //             return false;

    //         return jeli.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    //     }

    //     bool IsAutokillByJobEnabled()
    //     {
    //         if (!IsInJob())
    //             return false;

    //         return CheckIsAutokillByJobEnabled();
    //     }
    // } // namespace detail

    // int RunProcessOrThrow(std::string_view program_name, const fs::path &path, std::vector<std::string> cmdline,
    //                       bool output, bool throw_on_bad_exit, std::optional<fs::path> working_directory)
    // {
    //     HANDLE hJob = NULL;
    //     HANDLE hProcess = NULL;
    //     HANDLE hThread = NULL;

    //     auto finish = [&]() {
    //         if (hJob)
    //         {
    //             ::CloseHandle(hJob);
    //             hJob = NULL;
    //         }

    //         if (hProcess)
    //         {
    //             ::CloseHandle(hProcess);
    //             hProcess = NULL;
    //         }

    //         if (hThread)
    //         {
    //             ::CloseHandle(hThread);
    //             hThread = NULL;
    //         }
    //     };

    //     try
    //     {
    //         bool useJob = false;

    //         if (useJob)
    //         {
    //             hJob = CreateJobObjectW(NULL, NULL);
    //             if (!hJob)
    //                 RE_THROW ProcessRunException("{} failed to start: failed to create Win32 job object",
    //                 program_name);

    //             JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
    //             jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    //             if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
    //                 RE_THROW ProcessRunException("{} failed to start: failed to set Win32 job object information",
    //                                              program_name);
    //         }

    //         std::wstring args = path.wstring();
    //         for (auto &s : cmdline)
    //         {
    //             std::string arg;
    //             for (auto &c : s)
    //             {
    //                 if (c == '\"')
    //                 {
    //                     arg.push_back('"');
    //                     arg.push_back('"');
    //                 }
    //                 else
    //                 {
    //                     arg.push_back(c);
    //                 }
    //             }

    //             args.append(L"\"");
    //             args.append(fs::path{arg}.wstring());
    //             args.append(L"\" ");
    //         }

    //         args.pop_back();

    //         STARTUPINFOW info = {sizeof(info)};
    //         PROCESS_INFORMATION pi;

    //         fmt::print("[Process] args:\n");
    //         for (auto &arg : cmdline)
    //         {
    //             fmt::print("[arg]: {}\n", arg);
    //         }

    //         fmt::print("[Process] command line: {}\n", fs::path{args}.u8string());

    //         DWORD dwFlags = useJob ? (CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB) : NULL;

    //         if (::CreateProcessW(NULL, args.data(), nullptr, nullptr, true, dwFlags, nullptr,
    //                              working_directory ? working_directory->wstring().c_str() : nullptr, &info, &pi))
    //         {
    //             hProcess = pi.hProcess;
    //             hThread = pi.hThread;

    //             if (useJob)
    //             {
    //                 if (!AssignProcessToJobObject(hJob, pi.hProcess))
    //                     RE_THROW ProcessRunException(
    //                         "{} failed to start: failed to assign Win32 job object. WinError: 0x{:X}", program_name,
    //                         (uint32_t)GetLastError());

    //                 ::ResumeThread(hThread);
    //             }

    //             ::WaitForSingleObject(hProcess, INFINITE);

    //             DWORD exit_code = 0;
    //             ::GetExitCodeProcess(hProcess, &exit_code);

    //             if (throw_on_bad_exit && exit_code != 0)
    //             {
    //                 RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
    //             }

    //             finish();
    //             return (int)exit_code;
    //         }
    //         else
    //         {
    //             RE_THROW ProcessRunException("{} failed to start: CreateProcessW failed. WinError: 0x{:X}",
    //                                          program_name, (uint32_t)GetLastError());
    //         }
    //     }
    //     catch (const ProcessRunException &ex)
    //     {
    //         finish();
    //         throw ex;
    //     }
    //     catch (const std::exception &ex)
    //     {
    //         finish();
    //         throw ex;
    //     }
    // }

} // namespace re
